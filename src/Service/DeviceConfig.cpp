/*
    设备身份与网络配置的持久化与命令解析。
    每个字段各自存放在 NVS 的独立键中：
      - 每条命令只改一个字段，不存在跨字段事务
      - NVS 对每个键自身有 CRC 和原子提交，无需再套自定义记录头
    启动时一次性读入 RAM 缓存，之后的读取不访问 NVS、不分配堆内存，
    避免 AP 低内存自愈任务在堆紧张时拿到不完整的配置。
*/
#include "DeviceConfig.h"
#include "Config.h"
#include "Service/DualPrint.h"

#include <Preferences.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace
{

    constexpr char kNamespace[] = "wand_cfg";
    constexpr char kKeyBle[] = "ble_name";
    constexpr char kKeyApSsid[] = "ap_ssid";
    constexpr char kKeyApPass[] = "ap_pass";
    constexpr char kKeyWifiSsid[] = "wifi_ssid";
    constexpr char kKeyWifiPass[] = "wifi_pass";
    constexpr char kKeyWifiCh[] = "wifi_ch";
    constexpr char kKeyMqttHost[] = "mqtt_host";
    constexpr char kKeyMqttPort[] = "mqtt_port";
    constexpr char kKeyMqttUser[] = "mqtt_user";
    constexpr char kKeyMqttPass[] = "mqtt_pass";

    constexpr size_t kSsidMax = 32;
    constexpr size_t kPassMin = 8;
    constexpr size_t kPassMax = 63;
    constexpr size_t kBleNameMax = 29;
    constexpr size_t kMqttHostMax = 63;
    constexpr size_t kMqttUserMax = 31;
    constexpr uint16_t kMqttPortDefault = 1883;

    portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

    char s_ble_name[kBleNameMax + 1];
    char s_ap_ssid[kSsidMax + 1];
    char s_ap_pass[kPassMax + 1];
    char s_wifi_ssid[kSsidMax + 1];
    char s_wifi_pass[kPassMax + 1];
    // 上次成功连接的信道，用于开机从该信道开始扫描
    uint8_t s_wifi_channel = 0;
    bool s_wifi_channel_cached = false;
    char s_mqtt_host[kMqttHostMax + 1];
    char s_mqtt_user[kMqttUserMax + 1];
    char s_mqtt_pass[kPassMax + 1];
    uint16_t s_mqtt_port = kMqttPortDefault;

    void copy_text(char *destination, size_t capacity, const char *source)
    {
        if (destination == nullptr || capacity == 0)
            return;

        if (source == nullptr)
            source = "";

        size_t length = strlen(source);
        if (length >= capacity)
            length = capacity - 1;

        memcpy(destination, source, length);
        destination[length] = '\0';
    }

    size_t text_length(const char *value)
    {
        return value == nullptr ? 0 : strlen(value);
    }

    bool valid_ssid(const char *value)
    {
        const size_t length = text_length(value);
        return length >= 1 && length <= kSsidMax;
    }

    bool valid_ap_pass(const char *value)
    {
        const size_t length = text_length(value);
        return length >= kPassMin && length <= kPassMax;
    }

    bool valid_ble_name(const char *value)
    {
        const size_t length = text_length(value);
        return length >= 1 && length <= kBleNameMax;
    }

    // WiFi SSID / MQTT host 允许为空：空表示该功能未启用
    bool valid_wifi_ssid(const char *value)
    {
        return text_length(value) <= kSsidMax;
    }

    bool valid_wifi_pass(const char *value)
    {
        const size_t length = text_length(value);
        return length == 0 || (length >= kPassMin && length <= kPassMax);
    }

    bool valid_wifi_channel(uint8_t channel)
    {
        return channel >= 1 && channel <= 14;
    }

    bool valid_mqtt_host(const char *value)
    {
        const size_t length = text_length(value);
        if (length > kMqttHostMax)
            return false;

        // broker 地址不允许空白字符，避免把误输入的空格带进 DNS 解析
        for (size_t i = 0; i < length; ++i)
        {
            const unsigned char c = (unsigned char)value[i];
            if (c <= 0x20 || c == 0x7F)
                return false;
        }
        return true;
    }

    bool valid_mqtt_user(const char *value)
    {
        return text_length(value) <= kMqttUserMax;
    }

    bool valid_mqtt_pass(const char *value)
    {
        return text_length(value) <= kPassMax;
    }

    // 键缺失或内容非法时使用默认值；每个字段独立回退，互不影响
    bool load_string(Preferences &preferences, const char *key,
                     const char *fallback, char *destination,
                     size_t destination_size, bool (*is_valid)(const char *))
    {
        String value = preferences.getString(key, fallback);
        if (!is_valid(value.c_str()))
        {
            DualSerial.printf("[Config] Invalid '%s' in NVS, using default\n", key);
            copy_text(destination, destination_size, fallback);
            return false;
        }

        copy_text(destination, destination_size, value.c_str());
        return true;
    }

    uint16_t load_mqtt_port(Preferences &preferences)
    {
        const uint16_t value =
            preferences.getUShort(kKeyMqttPort, kMqttPortDefault);
        if (value == 0)
        {
            DualSerial.println("[Config] Invalid 'mqtt_port' in NVS, using default");
            return kMqttPortDefault;
        }
        return value;
    }

    bool save_value(const char *key, const char *value, char *cache,
                    size_t cache_size)
    {
        if (value == nullptr)
            value = "";

        const size_t length = strlen(value);

        Preferences preferences;
        if (!preferences.begin(kNamespace, false))
        {
            DualSerial.println("Error: failed to open NVS");
            return false;
        }

        const size_t written = preferences.putString(key, value);
        preferences.end();

        if (written != length)
        {
            DualSerial.println("Error: failed to save config");
            return false;
        }

        portENTER_CRITICAL(&s_lock);
        copy_text(cache, cache_size, value);
        portEXIT_CRITICAL(&s_lock);
        return true;
    }

    bool save_mqtt_port(uint16_t value)
    {
        Preferences preferences;
        if (!preferences.begin(kNamespace, false))
        {
            DualSerial.println("Error: failed to open NVS");
            return false;
        }

        const size_t written = preferences.putUShort(kKeyMqttPort, value);
        preferences.end();

        if (written != sizeof(uint16_t))
        {
            DualSerial.println("Error: failed to save config");
            return false;
        }

        portENTER_CRITICAL(&s_lock);
        s_mqtt_port = value;
        portEXIT_CRITICAL(&s_lock);
        return true;
    }

    // 删除一组键；键不存在不算错误（首次配置时本来就没有）
    bool remove_keys(const char *const *keys, size_t count)
    {
        Preferences preferences;
        if (!preferences.begin(kNamespace, false))
        {
            DualSerial.println("Error: failed to open NVS");
            return false;
        }

        for (size_t i = 0; i < count; ++i)
            preferences.remove(keys[i]);

        preferences.end();
        return true;
    }

    bool parse_port(const char *value, uint16_t &port)
    {
        if (value == nullptr || *value == '\0')
            return false;

        char *end = nullptr;
        const unsigned long parsed = strtoul(value, &end, 10);
        if (end == value)
            return false;

        while (*end != '\0' && isspace((unsigned char)*end))
            ++end;
        if (*end != '\0')
            return false;

        if (parsed == 0 || parsed > 65535)
            return false;

        port = (uint16_t)parsed;
        return true;
    }

    void print_config_usage()
    {
        DualSerial.println("Device config:");
        DualSerial.println("  config list");
        DualSerial.println("  config ble <name>");
        DualSerial.println("  config ap <ssid>");
        DualSerial.println("  config ap-pass <password>");
        DualSerial.println("  config wifi <ssid>");
        DualSerial.println("  config wifi-pass <password>");
        DualSerial.println("  config mqtt <host>");
        DualSerial.println("  config mqtt-port <port>");
        DualSerial.println("  config mqtt-user <user>");
        DualSerial.println("  config mqtt-pass <password>");
        DualSerial.println("  config clear wifi|mqtt");
        DualSerial.println("  config reset");
        DualSerial.println("Note: value is the rest of the line (spaces allowed).");
    }

    void print_config_values()
    {
        char ble_name[kBleNameMax + 1];
        char ap_ssid[kSsidMax + 1];
        char ap_pass[kPassMax + 1];
        char wifi_ssid[kSsidMax + 1];
        char wifi_pass[kPassMax + 1];
        DeviceMqttConfig mqtt;

        device_config_get_ble_name(ble_name, sizeof(ble_name));
        device_config_get_ap(ap_ssid, sizeof(ap_ssid), ap_pass, sizeof(ap_pass));
        device_config_get_wifi(wifi_ssid, sizeof(wifi_ssid),
                               wifi_pass, sizeof(wifi_pass));
        device_config_get_mqtt(&mqtt);

        DualSerial.println("Device config:");
        DualSerial.printf("  BLE name : \"%s\"\n", ble_name);
        DualSerial.printf("  AP SSID  : \"%s\"\n", ap_ssid);
        DualSerial.printf("  AP PASS  : \"%s\"\n", ap_pass);
        DualSerial.printf("  WiFi SSID: \"%s\"\n", wifi_ssid);
        DualSerial.printf("  WiFi PASS: \"%s\"\n", wifi_pass);

        uint8_t cached_channel = 0;
        if (device_config_get_wifi_channel(&cached_channel))
            DualSerial.printf("  Cached channel: %u\n", (unsigned)cached_channel);

        DualSerial.printf("  MQTT host: \"%s\"\n", mqtt.host);
        DualSerial.printf("  MQTT port: %u\n", (unsigned)mqtt.port);
        DualSerial.printf("  MQTT user: \"%s\"\n", mqtt.user);
        DualSerial.printf("  MQTT pass: \"%s\"\n", mqtt.pass);
        DualSerial.println("Note: changes take effect after reboot.");
    }

    const char *skip_spaces(const char *cursor)
    {
        while (*cursor != '\0' && isspace((unsigned char)*cursor))
            ++cursor;
        return cursor;
    }

    bool consume_word(const char *&cursor, char *word, size_t capacity)
    {
        cursor = skip_spaces(cursor);
        if (*cursor == '\0')
            return false;

        const char *start = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor))
            ++cursor;

        size_t length = (size_t)(cursor - start);
        if (length == 0 || length >= capacity)
            return false;

        memcpy(word, start, length);
        word[length] = '\0';
        return true;
    }

    // 子命令与值之间保留一个空白作为分隔，分隔符之后的整行原样作为值。
    // 因此值内部可以包含空格，不需要引号或转义。
    const char *rest_of_line(const char *cursor)
    {
        if (*cursor == '\0' || !isspace((unsigned char)*cursor))
            return nullptr;
        return cursor + 1;
    }

} // namespace

void device_config_init(void)
{
    char loaded_ble_name[kBleNameMax + 1];
    char loaded_ap_ssid[kSsidMax + 1];
    char loaded_ap_pass[kPassMax + 1];
    char loaded_wifi_ssid[kSsidMax + 1];
    char loaded_wifi_pass[kPassMax + 1];
    char loaded_mqtt_host[kMqttHostMax + 1];
    char loaded_mqtt_user[kMqttUserMax + 1];
    char loaded_mqtt_pass[kPassMax + 1];
    uint16_t loaded_mqtt_port = kMqttPortDefault;
    uint8_t loaded_channel = 0;
    bool loaded_cache = false;

    copy_text(loaded_ble_name, sizeof(loaded_ble_name), BLE_DEVICE_NAME);
    copy_text(loaded_ap_ssid, sizeof(loaded_ap_ssid), AP_SSID);
    copy_text(loaded_ap_pass, sizeof(loaded_ap_pass), AP_PASS);
    copy_text(loaded_wifi_ssid, sizeof(loaded_wifi_ssid), "");
    copy_text(loaded_wifi_pass, sizeof(loaded_wifi_pass), "");
    copy_text(loaded_mqtt_host, sizeof(loaded_mqtt_host), "");
    copy_text(loaded_mqtt_user, sizeof(loaded_mqtt_user), "");
    copy_text(loaded_mqtt_pass, sizeof(loaded_mqtt_pass), "");

    Preferences preferences;
    if (preferences.begin(kNamespace, true))
    {
        load_string(preferences, kKeyBle, BLE_DEVICE_NAME, loaded_ble_name,
                    sizeof(loaded_ble_name), valid_ble_name);
        load_string(preferences, kKeyApSsid, AP_SSID, loaded_ap_ssid,
                    sizeof(loaded_ap_ssid), valid_ssid);
        load_string(preferences, kKeyApPass, AP_PASS, loaded_ap_pass,
                    sizeof(loaded_ap_pass), valid_ap_pass);
        load_string(preferences, kKeyWifiSsid, "", loaded_wifi_ssid,
                    sizeof(loaded_wifi_ssid), valid_wifi_ssid);
        load_string(preferences, kKeyWifiPass, "", loaded_wifi_pass,
                    sizeof(loaded_wifi_pass), valid_wifi_pass);
        load_string(preferences, kKeyMqttHost, "", loaded_mqtt_host,
                    sizeof(loaded_mqtt_host), valid_mqtt_host);
        load_string(preferences, kKeyMqttUser, "", loaded_mqtt_user,
                    sizeof(loaded_mqtt_user), valid_mqtt_user);
        load_string(preferences, kKeyMqttPass, "", loaded_mqtt_pass,
                    sizeof(loaded_mqtt_pass), valid_mqtt_pass);
        loaded_mqtt_port = load_mqtt_port(preferences);

        loaded_channel = preferences.getUChar(kKeyWifiCh, 0);
        loaded_cache = valid_wifi_channel(loaded_channel);
        preferences.end();
    }
    else
    {
        // 首次启动时命名空间尚不存在，只读打开会返回 NOT_FOUND
        DualSerial.println("[Config] No saved config, using defaults");
    }

    portENTER_CRITICAL(&s_lock);
    copy_text(s_ble_name, sizeof(s_ble_name), loaded_ble_name);
    copy_text(s_ap_ssid, sizeof(s_ap_ssid), loaded_ap_ssid);
    copy_text(s_ap_pass, sizeof(s_ap_pass), loaded_ap_pass);
    copy_text(s_wifi_ssid, sizeof(s_wifi_ssid), loaded_wifi_ssid);
    copy_text(s_wifi_pass, sizeof(s_wifi_pass), loaded_wifi_pass);
    s_wifi_channel = loaded_channel;
    s_wifi_channel_cached = loaded_cache;
    copy_text(s_mqtt_host, sizeof(s_mqtt_host), loaded_mqtt_host);
    copy_text(s_mqtt_user, sizeof(s_mqtt_user), loaded_mqtt_user);
    copy_text(s_mqtt_pass, sizeof(s_mqtt_pass), loaded_mqtt_pass);
    s_mqtt_port = loaded_mqtt_port;
    portEXIT_CRITICAL(&s_lock);

    DualSerial.printf("[Config] BLE name: \"%s\", AP SSID: \"%s\"\n",
                      loaded_ble_name, loaded_ap_ssid);
    if (loaded_wifi_ssid[0] != '\0')
        DualSerial.printf("[Config] WiFi STA: \"%s\"\n", loaded_wifi_ssid);
    else
        DualSerial.println("[Config] WiFi STA: not configured");

    if (loaded_mqtt_host[0] != '\0')
        DualSerial.printf("[Config] MQTT: %s:%u\n",
                          loaded_mqtt_host, (unsigned)loaded_mqtt_port);
    else
        DualSerial.println("[Config] MQTT: not configured");
}

void device_config_get_ble_name(char *name, size_t size)
{
    portENTER_CRITICAL(&s_lock);
    copy_text(name, size, s_ble_name);
    portEXIT_CRITICAL(&s_lock);
}

void device_config_get_ap(char *ssid, size_t ssid_size,
                          char *pass, size_t pass_size)
{
    portENTER_CRITICAL(&s_lock);
    copy_text(ssid, ssid_size, s_ap_ssid);
    copy_text(pass, pass_size, s_ap_pass);
    portEXIT_CRITICAL(&s_lock);
}

void device_config_get_wifi(char *ssid, size_t ssid_size,
                            char *pass, size_t pass_size)
{
    portENTER_CRITICAL(&s_lock);
    copy_text(ssid, ssid_size, s_wifi_ssid);
    copy_text(pass, pass_size, s_wifi_pass);
    portEXIT_CRITICAL(&s_lock);
}

bool device_config_get_wifi_channel(uint8_t *channel)
{
    if (channel == nullptr)
        return false;

    portENTER_CRITICAL(&s_lock);
    const bool cached = s_wifi_channel_cached;
    if (cached)
        *channel = s_wifi_channel;
    portEXIT_CRITICAL(&s_lock);
    return cached;
}

bool device_config_set_wifi_channel(uint8_t channel)
{
    if (!valid_wifi_channel(channel))
        return false;

    portENTER_CRITICAL(&s_lock);
    const bool unchanged = s_wifi_channel_cached && s_wifi_channel == channel;
    portEXIT_CRITICAL(&s_lock);
    if (unchanged)
        return true;

    Preferences preferences;
    if (!preferences.begin(kNamespace, false))
    {
        DualSerial.println("Error: failed to open NVS");
        return false;
    }

    const size_t written = preferences.putUChar(kKeyWifiCh, channel);
    preferences.end();

    if (written != sizeof(uint8_t))
    {
        DualSerial.println("Error: failed to save WiFi channel");
        return false;
    }

    portENTER_CRITICAL(&s_lock);
    s_wifi_channel = channel;
    s_wifi_channel_cached = true;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

void device_config_get_mqtt(DeviceMqttConfig *config)
{
    if (config == nullptr)
        return;

    portENTER_CRITICAL(&s_lock);
    copy_text(config->host, sizeof(config->host), s_mqtt_host);
    copy_text(config->user, sizeof(config->user), s_mqtt_user);
    copy_text(config->pass, sizeof(config->pass), s_mqtt_pass);
    config->port = s_mqtt_port;
    portEXIT_CRITICAL(&s_lock);
}

bool device_config_has_wifi(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool configured = s_wifi_ssid[0] != '\0';
    portEXIT_CRITICAL(&s_lock);
    return configured;
}

bool device_config_has_mqtt(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool configured = s_mqtt_host[0] != '\0';
    portEXIT_CRITICAL(&s_lock);
    return configured;
}

bool device_config_set_ble_name(const char *value)
{
    if (!valid_ble_name(value))
    {
        DualSerial.printf("Error: BLE name must be 1-%u bytes\n",
                          (unsigned)kBleNameMax);
        return false;
    }

    if (!save_value(kKeyBle, value, s_ble_name, sizeof(s_ble_name)))
        return false;

    DualSerial.printf("Saved: BLE name = \"%s\" (reboot to apply)\n", value);
    return true;
}

bool device_config_set_ap_ssid(const char *value)
{
    if (!valid_ssid(value))
    {
        DualSerial.printf("Error: AP SSID must be 1-%u bytes\n",
                          (unsigned)kSsidMax);
        return false;
    }

    if (!save_value(kKeyApSsid, value, s_ap_ssid, sizeof(s_ap_ssid)))
        return false;

    DualSerial.printf("Saved: AP SSID = \"%s\" (reboot to apply)\n", value);
    return true;
}

bool device_config_set_ap_pass(const char *value)
{
    if (!valid_ap_pass(value))
    {
        DualSerial.printf("Error: AP password must be %u-%u bytes\n",
                          (unsigned)kPassMin, (unsigned)kPassMax);
        return false;
    }

    if (!save_value(kKeyApPass, value, s_ap_pass, sizeof(s_ap_pass)))
        return false;

    DualSerial.println("Saved: AP password (reboot to apply)");
    return true;
}

bool device_config_set_wifi_ssid(const char *value)
{
    if (value == nullptr || value[0] == '\0' || !valid_wifi_ssid(value))
    {
        DualSerial.printf("Error: WiFi SSID must be 1-%u bytes\n",
                          (unsigned)kSsidMax);
        return false;
    }

    if (!save_value(kKeyWifiSsid, value, s_wifi_ssid, sizeof(s_wifi_ssid)))
        return false;

    DualSerial.printf("Saved: WiFi SSID = \"%s\" (reboot to apply)\n", value);
    return true;
}

bool device_config_set_wifi_pass(const char *value)
{
    if (!valid_wifi_pass(value))
    {
        DualSerial.printf("Error: WiFi password must be 0 or %u-%u bytes\n",
                          (unsigned)kPassMin, (unsigned)kPassMax);
        return false;
    }

    if (!save_value(kKeyWifiPass, value, s_wifi_pass, sizeof(s_wifi_pass)))
        return false;

    DualSerial.println("Saved: WiFi password (reboot to apply)");
    return true;
}

bool device_config_set_mqtt_host(const char *value)
{
    if (value == nullptr || value[0] == '\0' || !valid_mqtt_host(value))
    {
        DualSerial.printf("Error: MQTT host must be 1-%u bytes without spaces\n",
                          (unsigned)kMqttHostMax);
        return false;
    }

    if (!save_value(kKeyMqttHost, value, s_mqtt_host, sizeof(s_mqtt_host)))
        return false;

    DualSerial.printf("Saved: MQTT host = \"%s\" (reboot to apply)\n", value);
    return true;
}

bool device_config_set_mqtt_port(const char *value)
{
    uint16_t port = 0;
    if (!parse_port(value, port))
    {
        DualSerial.println("Error: MQTT port must be 1-65535");
        return false;
    }

    if (!save_mqtt_port(port))
        return false;

    DualSerial.printf("Saved: MQTT port = %u (reboot to apply)\n",
                      (unsigned)port);
    return true;
}

bool device_config_set_mqtt_user(const char *value)
{
    if (!valid_mqtt_user(value))
    {
        DualSerial.printf("Error: MQTT user must be 0-%u bytes\n",
                          (unsigned)kMqttUserMax);
        return false;
    }

    if (!save_value(kKeyMqttUser, value, s_mqtt_user, sizeof(s_mqtt_user)))
        return false;

    DualSerial.printf("Saved: MQTT user = \"%s\" (reboot to apply)\n", value);
    return true;
}

bool device_config_set_mqtt_pass(const char *value)
{
    if (!valid_mqtt_pass(value))
    {
        DualSerial.printf("Error: MQTT password must be 0-%u bytes\n",
                          (unsigned)kPassMax);
        return false;
    }

    if (!save_value(kKeyMqttPass, value, s_mqtt_pass, sizeof(s_mqtt_pass)))
        return false;

    DualSerial.println("Saved: MQTT password (reboot to apply)");
    return true;
}

bool device_config_clear_wifi(void)
{
    static const char *const keys[] = {kKeyWifiSsid, kKeyWifiPass, kKeyWifiCh};
    if (!remove_keys(keys, sizeof(keys) / sizeof(keys[0])))
        return false;

    portENTER_CRITICAL(&s_lock);
    s_wifi_ssid[0] = '\0';
    s_wifi_pass[0] = '\0';
    s_wifi_channel = 0;
    s_wifi_channel_cached = false;
    portEXIT_CRITICAL(&s_lock);

    DualSerial.println("Cleared: WiFi config (reboot to apply)");
    return true;
}

bool device_config_clear_mqtt(void)
{
    static const char *const keys[] = {
        kKeyMqttHost, kKeyMqttPort, kKeyMqttUser, kKeyMqttPass};
    if (!remove_keys(keys, sizeof(keys) / sizeof(keys[0])))
        return false;

    portENTER_CRITICAL(&s_lock);
    s_mqtt_host[0] = '\0';
    s_mqtt_user[0] = '\0';
    s_mqtt_pass[0] = '\0';
    s_mqtt_port = kMqttPortDefault;
    portEXIT_CRITICAL(&s_lock);

    DualSerial.println("Cleared: MQTT config (reboot to apply)");
    return true;
}

bool device_config_reset(void)
{
    Preferences preferences;
    if (!preferences.begin(kNamespace, false))
    {
        DualSerial.println("Error: failed to open NVS");
        return false;
    }

    const bool cleared = preferences.clear();
    preferences.end();

    if (!cleared)
    {
        DualSerial.println("Error: failed to reset config");
        return false;
    }

    portENTER_CRITICAL(&s_lock);
    copy_text(s_ble_name, sizeof(s_ble_name), BLE_DEVICE_NAME);
    copy_text(s_ap_ssid, sizeof(s_ap_ssid), AP_SSID);
    copy_text(s_ap_pass, sizeof(s_ap_pass), AP_PASS);
    s_wifi_ssid[0] = '\0';
    s_wifi_pass[0] = '\0';
    s_wifi_channel = 0;
    s_wifi_channel_cached = false;
    s_mqtt_host[0] = '\0';
    s_mqtt_user[0] = '\0';
    s_mqtt_pass[0] = '\0';
    s_mqtt_port = kMqttPortDefault;
    portEXIT_CRITICAL(&s_lock);

    DualSerial.println("Device config reset to defaults (reboot to apply)");
    return true;
}

void device_config_console(const char *args)
{
    const char *cursor = args == nullptr ? "" : args;
    char subcommand[16] = {};

    if (!consume_word(cursor, subcommand, sizeof(subcommand)))
    {
        print_config_usage();
        return;
    }

    if (strcmp(subcommand, "list") == 0)
    {
        if (*skip_spaces(cursor) != '\0')
        {
            DualSerial.println("Usage: config list");
            return;
        }
        print_config_values();
    }
    else if (strcmp(subcommand, "ble") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config ble <name>");
            return;
        }
        device_config_set_ble_name(value);
    }
    else if (strcmp(subcommand, "ap") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config ap <ssid>");
            return;
        }
        device_config_set_ap_ssid(value);
    }
    else if (strcmp(subcommand, "ap-pass") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config ap-pass <password>");
            return;
        }
        device_config_set_ap_pass(value);
    }
    else if (strcmp(subcommand, "wifi") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config wifi <ssid>");
            return;
        }
        device_config_set_wifi_ssid(value);
    }
    else if (strcmp(subcommand, "wifi-pass") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config wifi-pass <password>");
            return;
        }
        device_config_set_wifi_pass(value);
    }
    else if (strcmp(subcommand, "mqtt") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config mqtt <host>");
            return;
        }
        device_config_set_mqtt_host(value);
    }
    else if (strcmp(subcommand, "mqtt-port") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config mqtt-port <port>");
            return;
        }
        device_config_set_mqtt_port(value);
    }
    else if (strcmp(subcommand, "mqtt-user") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config mqtt-user <user>");
            return;
        }
        device_config_set_mqtt_user(value);
    }
    else if (strcmp(subcommand, "mqtt-pass") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config mqtt-pass <password>");
            return;
        }
        device_config_set_mqtt_pass(value);
    }
    else if (strcmp(subcommand, "clear") == 0)
    {
        char target[16] = {};
        if (!consume_word(cursor, target, sizeof(target)) ||
            *skip_spaces(cursor) != '\0')
        {
            DualSerial.println("Usage: config clear wifi|mqtt");
            return;
        }

        if (strcmp(target, "wifi") == 0)
            device_config_clear_wifi();
        else if (strcmp(target, "mqtt") == 0)
            device_config_clear_mqtt();
        else
            DualSerial.printf("Unknown config clear target: %s\n", target);
    }
    else if (strcmp(subcommand, "reset") == 0)
    {
        if (*skip_spaces(cursor) != '\0')
        {
            DualSerial.println("Usage: config reset");
            return;
        }
        device_config_reset();
    }
    else
    {
        DualSerial.printf("Unknown config subcommand: %s\n", subcommand);
        print_config_usage();
    }
}
