#include "APP_HomeAssistant.h"

#include "Config.h"
#include "GestureBinding.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimFlatline.hpp"
#include "HAL/WS2812_Animation/AnimFlow.hpp"
#include "Service/AP.h"
#include "Service/DeviceConfig.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace
{

    constexpr size_t kEventNameMax = 31;
    constexpr size_t kBaseTopicSize = 48;
    constexpr size_t kNodeIdSize = 32;
    constexpr size_t kClientIdSize = 32;
    constexpr size_t kTopicSize = 96;
    constexpr size_t kJsonBufferSize = 2048;
    constexpr uint16_t kMqttBufferSize = 4096;
    constexpr uint16_t kKeepAliveSeconds = 30;
    constexpr uint16_t kSocketTimeoutSeconds = 3;
    constexpr uint8_t kOutboxDepth = 8;
    // 每次发现时清理到固定槽位数，避免模型类别数减少后旧 device trigger
    // 的 retained 配置永远留在 broker 上
    constexpr size_t kDiscoveryGestureSlots = 32;
    constexpr uint32_t kReconnectMinMs = 1000;
    constexpr uint32_t kReconnectMaxMs = 30000;
    constexpr uint32_t kBatteryIntervalMs = 60000;
    constexpr uint32_t kLoopDelayMs = 20;
    // WiFi/MQTT 未就绪时的轮询周期：尽量缩短 GOT_IP 到 MQTT connect 的等待
    constexpr uint32_t kConnectPollMs = 50;
    // 开机 / WiFi 恢复后的快速重试：首个 SYN 有概率被 AP 丢弃，
    // 窗口内失败后不等正常退避（1s 起），改用短间隔再试，避免白白拖慢开机。
    constexpr uint32_t kFastRetryDelayMs = 150;
    constexpr uint32_t kFastRetryWindowMs = 5000;

    struct HaEvent
    {
        char name[kEventNameMax + 1];
    };

    WiFiClient s_wifi_client;
    PubSubClient s_mqtt(s_wifi_client);

    QueueHandle_t s_outbox = nullptr;
    QueueHandle_t s_ctrl_queue = nullptr;

    volatile bool s_mqtt_connected = false;
    bool s_enabled = false;
    bool s_discovery_dirty = true;
    bool s_map_changed = false;
    bool s_ha_signature_valid = false;
    uint32_t s_ha_signature = 0;
    bool s_first_discovery_logged = false;
    bool s_battery_dirty = false;
    uint32_t s_next_reconnect_ms = 0;
    uint32_t s_backoff_ms = kReconnectMinMs;
    uint32_t s_last_battery_ms = 0;
    // 本次 WiFi 会话的快速重试截止时刻。0 = 尚未开始（下次尝试时开启），
    // 非 0 后即使过期也保留时间戳，避免每次失败都重新开启窗口。
    uint32_t s_fast_retry_until_ms = 0;

    char s_host[64];
    uint16_t s_port = 1883;
    char s_user[32];
    char s_pass[64];

    char s_node_id[kNodeIdSize];
    char s_client_id[kClientIdSize];
    char s_base_topic[kBaseTopicSize];
    char s_availability_topic[kTopicSize];
    char s_event_topic[kTopicSize];
    char s_gesture_topic[kTopicSize];
    char s_battery_topic[kTopicSize];
    char s_device_name[30];

    bool is_valid_event_name(const char *value)
    {
        if (value == nullptr || value[0] == '\0')
            return false;

        const size_t length = strlen(value);
        if (length > kEventNameMax)
            return false;

        for (size_t i = 0; i < length; ++i)
        {
            const char c = value[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                return false;
        }
        return true;
    }

    void hash_mix(uint32_t &hash, const void *data, size_t length)
    {
        const uint8_t *bytes = (const uint8_t *)data;
        for (size_t i = 0; i < length; ++i)
        {
            hash ^= bytes[i];
            hash *= 16777619u;
        }
    }

    // 发现配置只受 HA 绑定影响：app == "ha" 且事件名合法的手势才会写入
    // device trigger / event_types。映射变更时用该签名判断是否真的需要
    // 重发，避免绑定 lumos、espnow 等无关 app 也触发整表 retained 重发。
    uint32_t ha_binding_signature(void)
    {
        uint32_t hash = 2166136261u;
        const size_t total = APP_binding_count();
        hash_mix(hash, &total, sizeof(total));

        for (size_t i = 0; i < total; ++i)
        {
            GestureBinding binding;
            if (!APP_binding_get(i, &binding))
                continue;
            if (strcmp(binding.app, "ha") != 0 ||
                !is_valid_event_name(binding.parameter))
                continue;

            hash_mix(hash, &i, sizeof(i));
            const size_t length = strlen(binding.parameter);
            hash_mix(hash, &length, sizeof(length));
            hash_mix(hash, binding.parameter, length);
        }
        return hash;
    }

    void clear_outbox(void)
    {
        if (s_outbox == nullptr)
            return;

        HaEvent event;
        while (xQueueReceive(s_outbox, &event, 0) == pdPASS)
        {
        }
    }

    bool publish_json(const char *topic, JsonDocument &document)
    {
        const size_t required = measureJson(document);
        if (required == 0 || required >= kJsonBufferSize)
        {
            DualSerial.printf("[HA] Discovery payload too large for %s\n", topic);
            return false;
        }

        char payload[kJsonBufferSize];
        serializeJson(document, payload, sizeof(payload));

        if (!s_mqtt.publish(topic, payload, true))
        {
            DualSerial.printf("[HA] Failed to publish discovery: %s\n", topic);
            return false;
        }
        return true;
    }

    void add_device_block(JsonDocument &document)
    {
        JsonObject device = document["device"].to<JsonObject>();
        JsonArray identifiers = device["identifiers"].to<JsonArray>();
        identifiers.add(s_node_id);

        device["name"] = s_device_name;
        device["manufacturer"] = "Muggles' Wand";
        device["model"] = "Muggles' Wand";
        device["sw_version"] = FIRMWARE_VER;

        // HA 设备卡片的 "Visit" 链接。使用当前 IP，保证 DHCP 变化后
        // 每次 MQTT 重连都会刷新为最新地址。
        char url[32];
        snprintf(url, sizeof(url), "http://%s/",
                 WiFi.localIP().toString().c_str());
        device["configuration_url"] = url;
    }

    void publish_battery_discovery(void)
    {
        char topic[kTopicSize];
        char unique_id[kNodeIdSize + 16];

        {
            JsonDocument document;
            document["name"] = "Battery";
            snprintf(unique_id, sizeof(unique_id), "%s_battery", s_node_id);
            document["unique_id"] = unique_id;
            document["state_topic"] = s_battery_topic;
            document["value_template"] = "{{ value_json.soc | round(1) }}";
            document["unit_of_measurement"] = "%";
            document["device_class"] = "battery";
            document["state_class"] = "measurement";
            document["entity_category"] = "diagnostic";
            document["availability_topic"] = s_availability_topic;
            document["payload_available"] = "online";
            document["payload_not_available"] = "offline";
            add_device_block(document);

            snprintf(topic, sizeof(topic),
                     "homeassistant/sensor/%s/battery/config", s_node_id);
            publish_json(topic, document);
        }

        {
            JsonDocument document;
            document["name"] = "Charging";
            snprintf(unique_id, sizeof(unique_id), "%s_charging", s_node_id);
            document["unique_id"] = unique_id;
            document["state_topic"] = s_battery_topic;
            document["value_template"] =
                "{{ 'ON' if value_json.charging else 'OFF' }}";
            document["device_class"] = "battery_charging";
            document["entity_category"] = "diagnostic";
            document["availability_topic"] = s_availability_topic;
            document["payload_available"] = "online";
            document["payload_not_available"] = "offline";
            add_device_block(document);

            snprintf(topic, sizeof(topic),
                     "homeassistant/binary_sensor/%s/charging/config", s_node_id);
            publish_json(topic, document);
        }
    }

    // 重新发布全部 retained 发现配置：
    //   - 一个 Event 实体，event_types 为当前 HA 绑定的事件名
    //   - 每个手势一个稳定的 device trigger，绑定 HA 时写入自定义名，
    //     未绑定时发空 retained 清除旧配置
    //   - 电池 / 充电状态传感器
    void publish_discovery(void)
    {
        JsonDocument event_document;
        event_document["name"] = "Gesture";

        char unique_id[kNodeIdSize + 16];
        snprintf(unique_id, sizeof(unique_id), "%s_gesture", s_node_id);
        event_document["unique_id"] = unique_id;
        event_document["state_topic"] = s_event_topic;
        // 注意：state_topic 的载荷已经是 {"event_type":"..."} 形式的完整 JSON，
        // 不能再设置 value_template。HA 的 event 实体要求 value_template
        // 渲染出完整 JSON 对象；若渲染成裸事件名会导致消息被丢弃。
        event_document["icon"] = "mdi:magic-staff";
        event_document["availability_topic"] = s_availability_topic;
        event_document["payload_available"] = "online";
        event_document["payload_not_available"] = "offline";
        add_device_block(event_document);

        JsonArray event_types = event_document["event_types"].to<JsonArray>();

        size_t ha_binding_count = 0;
        const size_t total = APP_binding_count();
        const size_t slots =
            total > kDiscoveryGestureSlots ? total : kDiscoveryGestureSlots;

        for (size_t i = 0; i < slots; ++i)
        {
            char trigger_topic[kTopicSize];
            snprintf(trigger_topic, sizeof(trigger_topic),
                     "homeassistant/device_automation/%s/gesture_%u/config",
                     s_node_id, (unsigned)i);

            GestureBinding binding;
            if (i >= total || !APP_binding_get(i, &binding))
            {
                // 超出当前模型类别数或映射表不可用：清除旧配置
                s_mqtt.publish(trigger_topic, "", true);
                continue;
            }

            const bool is_ha =
                strcmp(binding.app, "ha") == 0 &&
                is_valid_event_name(binding.parameter);

            if (!is_ha)
            {
                // 清除该手势以前可能留下的 device trigger 保留配置
                s_mqtt.publish(trigger_topic, "", true);
                continue;
            }

            ++ha_binding_count;

            bool duplicate = false;
            for (JsonVariant existing : event_types)
            {
                const char *name = existing.as<const char *>();
                if (name != nullptr && strcmp(name, binding.parameter) == 0)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
                event_types.add(binding.parameter);
            else
                DualSerial.printf("[HA] Warning: duplicate event name '%s'\n",
                                  binding.parameter);

            JsonDocument trigger_document;
            trigger_document["automation_type"] = "trigger";
            trigger_document["topic"] = s_gesture_topic;
            trigger_document["payload"] = binding.parameter;
            trigger_document["type"] = "gesture";
            trigger_document["subtype"] = binding.parameter;
            add_device_block(trigger_document);

            publish_json(trigger_topic, trigger_document);
        }

        char event_topic[kTopicSize];
        snprintf(event_topic, sizeof(event_topic),
                 "homeassistant/event/%s/gesture/config", s_node_id);

        if (ha_binding_count > 0)
        {
            publish_json(event_topic, event_document);
        }
        else
        {
            // 没有任何 HA 手势绑定：移除 Event 实体，
            // 设备仍然通过电池/充电实体出现在 HA 中
            s_mqtt.publish(event_topic, "", true);
        }

        publish_battery_discovery();

        s_ha_signature = ha_binding_signature();
        s_ha_signature_valid = true;

        DualSerial.printf("[HA] Discovery published %u HA gesture(s)\n",
                          (unsigned)ha_binding_count);

        // 开机后第一次发现完成：打印从复位到 HA 可用的总耗时。
        // 只打一次；后续重连/映射变更不再重复。
        if (!s_first_discovery_logged)
        {
            s_first_discovery_logged = true;
            DualSerial.printf("[HA] First discovery completed in %u ms since boot\n",
                              (unsigned)millis());
        }
    }

    void publish_event(const char *name)
    {
        char payload[64];
        const int length = snprintf(payload, sizeof(payload),
                                    "{\"event_type\":\"%s\"}", name);
        if (length <= 0 || length >= (int)sizeof(payload))
            return;

        const bool event_ok = s_mqtt.publish(s_event_topic, payload, false);
        const bool trigger_ok = s_mqtt.publish(s_gesture_topic, name, false);

        if (event_ok && trigger_ok)
            DualSerial.printf("[HA] Event sent: %s\n", name);
        else
            DualSerial.printf("[HA] Publish failed: %s\n", name);
    }

    void publish_battery(void)
    {
        char payload[96];
        const int length = snprintf(
            payload, sizeof(payload),
            "{\"soc\":%.1f,\"voltage\":%.2f,\"charging\":%s}",
            HAL::MAX17048_getSOC(),
            HAL::MAX17048_getVoltage(),
            HAL::power_getChargeStatus() ? "true" : "false");

        if (length <= 0 || length >= (int)sizeof(payload))
            return;

        s_mqtt.publish(s_battery_topic, payload, true);
    }

    void mqtt_connect(void)
    {
        // 预热：向网关（AP）发一个 UDP 包，让 AP 刷新该 MAC 的 bridge/省电状态。
        // 某些 AP 在客户端快速重启后会短暂丢弃上行单播，导致首个 TCP SYN 丢失。
        // 注意：这里不能调用 WiFi.setSleep(false)，ESP32-S3 在 BLE 共存下
        // 会触发异常重启（已实测）。
        {
            const IPAddress gateway = WiFi.gatewayIP();
            if ((uint32_t)gateway != 0)
            {
                WiFiUDP warmup;
                if (warmup.beginPacket(gateway, 9))
                {
                    const uint8_t payload = 0;
                    warmup.write(&payload, sizeof(payload));
                    warmup.endPacket();
                }
            }
        }

        // 先用两个短超时尝试，让被 AP 丢弃的首个 SYN 能更早重发；
        // 两次都失败则交给 PubSubClient 用默认超时再试。
        bool tcp_ok = s_wifi_client.connected();
        if (!tcp_ok)
        {
            for (uint8_t attempt = 0; attempt < 2; ++attempt)
            {
                tcp_ok = s_wifi_client.connect(s_host, s_port, 600);
                if (tcp_ok)
                    break;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        if (!tcp_ok)
            DualSerial.println("[HA] TCP pre-connect failed, MQTT will retry");

        bool connected;
        if (s_user[0] != '\0')
        {
            connected = s_mqtt.connect(
                s_client_id, s_user, s_pass,
                s_availability_topic, 1, true, "offline");
        }
        else
        {
            connected = s_mqtt.connect(
                s_client_id,
                s_availability_topic, 1, true, "offline");
        }

        if (connected)
        {
            s_mqtt_connected = true;
            s_backoff_ms = kReconnectMinMs;
            s_next_reconnect_ms = millis();
            s_last_battery_ms = 0; // 连接后立即发布一次电量
            s_discovery_dirty = true;

            s_mqtt.publish(s_availability_topic, "online", true);
            DualSerial.printf("[HA] MQTT connected, base topic: %s\n",
                              s_base_topic);
            return;
        }

        s_mqtt_connected = false;
        const uint32_t now = millis();
        const bool fast_retry =
            s_fast_retry_until_ms != 0 &&
            (int32_t)(now - s_fast_retry_until_ms) < 0;
        const uint32_t retry_delay_ms =
            fast_retry ? kFastRetryDelayMs : s_backoff_ms;

        s_next_reconnect_ms = now + retry_delay_ms;
        DualSerial.printf("[HA] MQTT connect failed (state %d), retry in %u ms\n",
                          s_mqtt.state(), (unsigned)retry_delay_ms);

        if (fast_retry)
        {
            // 窗口内不累加退避，窗口结束后仍从最小间隔开始
            s_backoff_ms = kReconnectMinMs;
        }
        else
        {
            s_backoff_ms *= 2;
            if (s_backoff_ms > kReconnectMaxMs)
                s_backoff_ms = kReconnectMaxMs;
        }
    }

    void ha_task(void *param)
    {
        (void)param;

        for (;;)
        {
            // 映射变更：仅当 HA 绑定集合变化时才重新发布发现配置
            SystemEvent control;
            while (s_ctrl_queue != nullptr &&
                   xQueueReceive(s_ctrl_queue, &control, 0) == pdPASS)
            {
                if (control.id == EVENT_MAP_CHANGED)
                    s_map_changed = true;
                else if (control.id == EVENT_POWER_CHARGING ||
                         control.id == EVENT_POWER_NOT_CHARGING)
                    s_battery_dirty = true;
            }

            if (!network_is_sta_connected())
            {
                // 不主动发送 MQTT DISCONNECT：保留 LWT，让 broker 在
                // keepalive 超时后自动把 availability 置为 offline。
                if (s_mqtt_connected)
                {
                    s_mqtt_connected = false;
                    DualSerial.println("[HA] WiFi lost, MQTT offline");
                }

                clear_outbox();
                s_backoff_ms = kReconnectMinMs;
                // WiFi 恢复后立即尝试 MQTT；这里若用 millis()+1000，
                // 每 50ms 一轮的轮询会把等待不断推迟，导致 WiFi 连上后固定多等约 1s。
                s_next_reconnect_ms = millis();
                // 本次 WiFi 恢复后重新开启快速重试窗口
                s_fast_retry_until_ms = 0;
                vTaskDelay(pdMS_TO_TICKS(kConnectPollMs));
                continue;
            }

            if (!s_mqtt.connected())
            {
                s_mqtt_connected = false;
                clear_outbox();

                if ((int32_t)(millis() - s_next_reconnect_ms) >= 0)
                {
                    // 本次 WiFi 会话的第一次 MQTT 尝试：开启快速重试窗口
                    if (s_fast_retry_until_ms == 0)
                        s_fast_retry_until_ms = millis() + kFastRetryWindowMs;
                    mqtt_connect();
                }

                vTaskDelay(pdMS_TO_TICKS(kConnectPollMs));
                continue;
            }

            s_mqtt.loop();

            if (s_discovery_dirty)
            {
                // 首次连接 / MQTT 重连：强制完整重发
                s_discovery_dirty = false;
                s_map_changed = false;
                publish_discovery();
            }
            else if (s_map_changed)
            {
                // 仅当 HA 绑定集合真的变化时才重发发现配置
                s_map_changed = false;
                if (!s_ha_signature_valid ||
                    ha_binding_signature() != s_ha_signature)
                {
                    publish_discovery();
                }
            }

            HaEvent event;
            while (s_outbox != nullptr &&
                   xQueueReceive(s_outbox, &event, 0) == pdPASS)
            {
                publish_event(event.name);
            }

            if (s_battery_dirty || s_last_battery_ms == 0 ||
                millis() - s_last_battery_ms >= kBatteryIntervalMs)
            {
                s_battery_dirty = false;
                s_last_battery_ms = millis();
                publish_battery();
            }

            vTaskDelay(pdMS_TO_TICKS(kLoopDelayMs));
        }
    }

} // namespace

void APP_HomeAssistant_init(void)
{
    DeviceMqttConfig mqtt;
    device_config_get_mqtt(&mqtt);

    if (mqtt.host[0] == '\0')
    {
        DualSerial.println("[HA] Not configured (set `config mqtt <host>`)");
        return;
    }

    char wifi_ssid[33];
    char wifi_pass[64];
    device_config_get_wifi(wifi_ssid, sizeof(wifi_ssid),
                           wifi_pass, sizeof(wifi_pass));
    if (wifi_ssid[0] == '\0')
    {
        DualSerial.println("[HA] MQTT configured but WiFi SSID is empty; HA disabled");
        return;
    }

    strncpy(s_host, mqtt.host, sizeof(s_host) - 1);
    s_host[sizeof(s_host) - 1] = '\0';
    strncpy(s_user, mqtt.user, sizeof(s_user) - 1);
    s_user[sizeof(s_user) - 1] = '\0';
    strncpy(s_pass, mqtt.pass, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';
    s_port = mqtt.port;

    if (s_user[0] == '\0' && s_pass[0] != '\0')
    {
        DualSerial.println("[HA] Warning: MQTT password set without user; using anonymous access");
    }

    device_config_get_ble_name(s_device_name, sizeof(s_device_name));

    uint8_t mac[6] = {};
    WiFi.macAddress(mac);
    snprintf(s_node_id, sizeof(s_node_id), "muggleswand_%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    snprintf(s_client_id, sizeof(s_client_id), "muggleswand-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    snprintf(s_base_topic, sizeof(s_base_topic), "muggleswand/%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    snprintf(s_availability_topic, sizeof(s_availability_topic),
             "%s/availability", s_base_topic);
    snprintf(s_event_topic, sizeof(s_event_topic), "%s/event", s_base_topic);
    snprintf(s_gesture_topic, sizeof(s_gesture_topic), "%s/gesture", s_base_topic);
    snprintf(s_battery_topic, sizeof(s_battery_topic), "%s/battery", s_base_topic);

    s_outbox = xQueueCreate(kOutboxDepth, sizeof(HaEvent));
    s_ctrl_queue = xQueueCreate(8, sizeof(SystemEvent));
    if (s_outbox == nullptr || s_ctrl_queue == nullptr)
    {
        DualSerial.println("[HA] Failed to create queues; HA disabled");
        return;
    }

    EventBus::subscribe(EVENT_MAP_CHANGED, s_ctrl_queue);
    EventBus::subscribe(EVENT_POWER_CHARGING, s_ctrl_queue);
    EventBus::subscribe(EVENT_POWER_NOT_CHARGING, s_ctrl_queue);

    s_mqtt.setServer(s_host, s_port);
    if (!s_mqtt.setBufferSize(kMqttBufferSize))
    {
        DualSerial.println("[HA] Warning: failed to enlarge MQTT buffer");
    }
    s_mqtt.setKeepAlive(kKeepAliveSeconds);
    s_mqtt.setSocketTimeout(kSocketTimeoutSeconds);

    s_enabled = true;

    if (xTaskCreatePinnedToCore(ha_task, "ha_task", 8192, nullptr, 1, nullptr, 0) != pdPASS)
    {
        s_enabled = false;
        DualSerial.println("[HA] Failed to create task; HA disabled");
        return;
    }
}

bool APP_HomeAssistant_validate(const char *parameter)
{
    if (parameter == nullptr || parameter[0] == '\0')
    {
        DualSerial.println("[HA] Event name required, e.g. map bind 3 ha swipe_up");
        return false;
    }

    if (strlen(parameter) > kEventNameMax)
    {
        DualSerial.printf("[HA] Event name must be 1-%u characters\n",
                          (unsigned)kEventNameMax);
        return false;
    }

    if (!is_valid_event_name(parameter))
    {
        DualSerial.println("[HA] Event name only allows a-z, 0-9 and _");
        return false;
    }

    return true;
}

bool APP_HomeAssistant_execute(const char *parameter)
{
    if (!APP_HomeAssistant_validate(parameter))
        return false;

    if (!s_enabled || s_outbox == nullptr)
    {
        DualSerial.println("[HA] MQTT not configured, event ignored");
        HAL::ws2812_start_fx(new AnimFlatline());
        return false;
    }

    if (!s_mqtt_connected)
    {
        DualSerial.printf("[HA] MQTT offline, event '%s' dropped\n", parameter);
        HAL::ws2812_start_fx(new AnimFlatline());
        return false;
    }

    HaEvent event = {};
    strncpy(event.name, parameter, kEventNameMax);
    event.name[kEventNameMax] = '\0';

    if (xQueueSend(s_outbox, &event, 0) != pdPASS)
    {
        // 队列满时丢弃最旧的事件，保证最新手势能发出去
        HaEvent dropped;
        xQueueReceive(s_outbox, &dropped, 0);

        if (xQueueSend(s_outbox, &event, 0) != pdPASS)
        {
            DualSerial.println("[HA] Event queue full, event dropped");
            HAL::ws2812_start_fx(new AnimFlatline());
            return false;
        }
    }

    // 与 ESP-NOW 的发送成功反馈一致：白色光流
    HAL::ws2812_start_fx(new AnimFlow(CRGB::White));
    return true;
}

void APP_HomeAssistant_console(const char *args)
{
    const char *cursor = args == nullptr ? "" : args;
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;

    size_t command_length = strlen(cursor);
    while (command_length > 0 &&
           isspace((unsigned char)cursor[command_length - 1]))
    {
        --command_length;
    }

    if (command_length != 0 &&
        (command_length != 6 || strncmp(cursor, "status", 6) != 0))
    {
        DualSerial.println("Usage: ha status");
        return;
    }

    DualSerial.println("Home Assistant status:");

    if (!s_enabled)
    {
        DualSerial.println("  MQTT     : not configured");
        DualSerial.println("  Note: use `config wifi ...`, `config mqtt ...`, then reboot.");
    }
    else
    {
        DualSerial.printf("  Broker   : %s:%u\n", s_host, (unsigned)s_port);
        DualSerial.printf("  Client ID: %s\n", s_client_id);
        DualSerial.printf("  Base topic: %s\n", s_base_topic);
        DualSerial.printf("  MQTT     : %s\n",
                          s_mqtt_connected ? "connected" : "disconnected");
    }

    const bool sta_configured = network_is_sta_configured();
    const bool sta_enabled = network_is_sta_enabled();
    const bool sta_connected = network_is_sta_connected();
    if (!sta_configured)
    {
        DualSerial.println("  WiFi     : not configured");
    }
    else if (!sta_enabled)
    {
        DualSerial.println("  WiFi     : disabled (use `wifi` to enable)");
    }
    else
    {
        DualSerial.printf("  WiFi     : %s",
                          sta_connected ? "connected" : "disconnected");
        if (sta_connected)
        {
            DualSerial.printf(", IP %s, ch %u, RSSI %d dBm",
                              WiFi.localIP().toString().c_str(),
                              (unsigned)WiFi.channel(), (int)WiFi.RSSI());
        }
        DualSerial.println();
    }

    const char *hostname = network_get_mdns_hostname();
    DualSerial.printf("  mDNS     : %s\n",
                      (hostname != nullptr && hostname[0] != '\0')
                          ? hostname
                          : "(off)");

    size_t ha_bindings = 0;
    const size_t total = APP_binding_count();
    for (size_t i = 0; i < total; ++i)
    {
        GestureBinding binding;
        if (APP_binding_get(i, &binding) &&
            strcmp(binding.app, "ha") == 0)
        {
            ++ha_bindings;
        }
    }

    DualSerial.printf("  Bindings : %u HA gesture(s) / %u total\n",
                      (unsigned)ha_bindings, (unsigned)total);
}
