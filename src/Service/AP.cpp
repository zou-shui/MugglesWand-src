/*
    WiFi 网络管理模块：统一管理 AP / STA / ESP-NOW 的射频资源，避免冲突。
    开机由 network_init() 根据 DeviceConfig 决定接入方式：
      - 配置了 WiFi SSID：连接 STA，不默认开 AP；Web/TCP 走局域网
      - 未配置：沿用原有 AP 模式
    TCP/Web 服务与 AP 射频解耦，ap 命令/双击只切换 AP，不影响已建立的 STA 连接；
    STA 未连接时开启 AP 会暂停自动重连（见 sta_retry_pause），避免 STA 扫描
    把 SoftAP 的信道一起拖走。
*/
#include "AP.h"
#include "AP_Web.h"
#include "Config.h"
#include "Service/Console.h"
#include "Service/DeviceConfig.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

// ==================== 配置与常量定义 ====================
#define TCP_PORT 8080 // TCP 服务器监听端口
#define MAX_CLIENTS 2 // TCP Server 最大连接数

static const uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ==================== 模块内部变量 ====================
static AsyncServer *tcpServer = nullptr;
static AsyncClient *clients[MAX_CLIENTS] = {nullptr};

static bool s_servers_started = false;
static bool s_ap_enabled = false; // 用户意图：AP 服务开关
static bool s_ap_started = false; // softAP 实际是否启动成功
static bool s_espnow_enabled = false;

// ESP-NOW readiness：init+add_peer 完成后置 true，deinit 前先置 false
static volatile bool s_espnow_ready = false;

// ESP-NOW 因信道不为 1 被被动停止；之后回到信道 1 时自动恢复
static bool s_espnow_blocked = false;

// 是否配置了 WiFi STA（开机后不再改变，重启生效）
static bool s_sta_configured = false;

// WiFi STA 服务开关：运行时可 toggle，开机默认跟随配置
static bool s_sta_enabled = false;

// WiFi.begin() 是否已经调用：用于过滤模式切换产生的早期 disconnect 事件
static bool s_sta_begin_called = false;

// 上次成功连接的信道缓存：开机从该信道开始扫描，跳过该信道之前的扫描。
// s_sta_config_uses_cached_channel 表示当前 STA 配置是否使用了缓存信道；
// 连接失败时回退到从信道 1 开始的全信道扫描。
static uint8_t s_cached_channel = 0;
static bool s_has_cached_channel = false;
static bool s_sta_config_uses_cached_channel = false;
static bool s_got_ip_since_begin = false;
static uint8_t s_cached_failures = 0;

// AP 开启期间暂停 STA 自动重连：APSTA 共用一个射频，SoftAP 信道会跟随 STA。
// 配置的 AP 不存在时，自动重连会不断全信道扫描，把 SoftAP 一起拖到随机信道，
// 表现为热点能搜到但无法关联。关闭 AP 后恢复重连。
static bool s_sta_retry_paused = false;

// OTA 接管网络后置 true；此时忽略 ap/espnow 切换
static bool s_network_suspended = false;

// 已下发到 WiFi 驱动的 mode：用于 update_rf_state() 幂等保护，避免重复 WiFi.mode()
static wifi_mode_t s_current_wifi_mode = WIFI_MODE_NULL;

static char s_sta_ssid[33];
static char s_sta_pass[64];
static char s_mdns_hostname[32];
static bool s_mdns_started = false;

// 串行化 AP/ESP-NOW 状态切换：双击切 AP 与堆内存自愈任务可能并发调用
static SemaphoreHandle_t s_net_mutex = nullptr;

static void net_lock(void)
{
    if (s_net_mutex != nullptr)
        xSemaphoreTake(s_net_mutex, portMAX_DELAY);
}

static void net_unlock(void)
{
    if (s_net_mutex != nullptr)
        xSemaphoreGive(s_net_mutex);
}

// ==================== STA 重连刹车 ====================
// APSTA 只有一套射频，SoftAP 的信道跟随 STA。STA 连不上目标 AP 时，
// arduino-esp32 的自动重连会在断开事件里立刻 WiFi.disconnect() + WiFi.begin()，
// 反复全信道扫描，SoftAP 随之频繁换信道，客户端能搜到却完不成认证/关联。
// AP 开启期间先把重连停掉；关闭 AP 后再恢复。以下函数必须在持锁时调用。
static void sta_retry_pause(void)
{
    if (!s_sta_configured)
        return;

    if (!s_sta_retry_paused)
    {
        s_sta_retry_paused = true;
        WiFi.setAutoReconnect(false);
        DualSerial.println("[Net] STA retry paused (AP is on)");
    }

    // 中止正在进行的扫描/连接；已连接时保持链路不动。
    // 用底层接口是为了在未连接时静默失败（Arduino 封装会打错误日志）。
    if (WiFi.status() != WL_CONNECTED)
    {
        esp_wifi_disconnect();
    }
}

static void sta_retry_resume(void)
{
    if (!s_sta_enabled || !s_sta_retry_paused)
        return;

    s_sta_retry_paused = false;
    WiFi.setAutoReconnect(true);

    if (WiFi.status() != WL_CONNECTED)
    {
        DualSerial.printf("[Net] Resuming STA connection to \"%s\"\n", s_sta_ssid);
        s_sta_config_uses_cached_channel = false;
        s_cached_failures = 0;
        WiFi.begin(s_sta_ssid, s_sta_pass);
    }
}

// ==================== 前置声明 ====================
static void update_rf_state(void);
static void start_espnow(void);
static void stop_espnow(bool blocked_by_channel);
static void network_refresh_espnow_gate(void);
static void servers_start(void);
static void servers_stop(void);
static void build_mdns_hostname(void);
static void start_mdns(void);
static void on_wifi_event(arduino_event_id_t event);

// ==================== 核心射频(RF)统一调度器 ====================
// AP 开启时 WiFi 锁定 APSTA；ESP-NOW 切换不触发 WiFi.mode()，
// 避免在 NimBLE 运行期间反复拆装 STA 子接口导致共存状态机崩溃。

// 启动 SoftAP 并把信道固定到 1。成功返回 true；失败只记录，不影响 STA。
static bool start_softap(void)
{
    char ssid[33];
    char pass[64];
    device_config_get_ap(ssid, sizeof(ssid), pass, sizeof(pass));
    if (!WiFi.softAP(ssid, pass, 1))
    {
        DualSerial.println("[Net] Failed to start softAP");
        return false;
    }

    // STA 未连接时把信道钉回 1，避免残留的扫描过程让 AP 在信道间漂移；
    // ESP-NOW 同样依赖信道 1。STA 已连接时不能改，否则会断开 STA。
    if (WiFi.status() != WL_CONNECTED &&
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE) != ESP_OK)
    {
        DualSerial.println("[Net] Failed to pin AP channel to 1");
    }

    return true;
}

static void update_rf_state(void)
{
    wifi_mode_t want;
    if (s_ap_enabled)
    {
        want = WIFI_MODE_APSTA;
    }
    else if (s_sta_enabled || s_espnow_enabled)
    {
        want = WIFI_MODE_STA;
    }
    else
    {
        want = WIFI_MODE_NULL;
    }

    // 幂等保护：避免重复触发 WiFi.mode()
    if (want == s_current_wifi_mode)
    {
        // 低内存时 softAP 可能启动失败：模式已是 APSTA 但热点不存在，补启动一次
        if (want == WIFI_MODE_APSTA && s_ap_enabled && !s_ap_started)
        {
            s_ap_started = start_softap();
        }
        return;
    }

    switch (want)
    {
    case WIFI_MODE_APSTA:
        if (!WiFi.mode(WIFI_AP_STA))
        {
            // 状态不更新，下次调用继续重试
            DualSerial.println("[Net] Failed to switch to WIFI_AP_STA");
            return;
        }
        s_current_wifi_mode = want;
        s_ap_started = start_softap();
        DualSerial.println("[Net] RF Mode -> WIFI_AP_STA");
        break;

    case WIFI_MODE_STA:
        if (!WiFi.mode(WIFI_STA))
        {
            DualSerial.println("[Net] Failed to switch to WIFI_STA");
            return;
        }
        s_current_wifi_mode = want;
        s_ap_started = false; // 切到纯 STA 会停掉 AP 接口
        break;

    default:
        WiFi.softAPdisconnect(true);
        if (!WiFi.mode(WIFI_OFF))
        {
            DualSerial.println("[Net] Failed to switch WiFi off");
            return;
        }
        s_current_wifi_mode = want;
        s_ap_started = false;
        DualSerial.println("[Net] RF Mode -> WIFI_OFF");
        break;
    }
}

// ESP-NOW 依赖信道 1。AP/STA 处于其它信道时不允许开启。
// STA 已启用但尚未拿到 IP 时也暂不允许：开机/重连阶段 WiFi 正在扫描，
// 此时启动 ESP-NOW 会强制信道 1 并占用射频，拖慢首次连接。
// 现有 s_espnow_blocked 机制会在 GOT_IP 且信道为 1 时自动补启。
bool network_espnow_allowed(void)
{
    if (s_network_suspended)
        return false;

    if (s_ap_enabled)
        return WiFi.channel() == 1;

    if (s_sta_enabled)
        return WiFi.status() == WL_CONNECTED && WiFi.channel() == 1;

    // WiFi 开关关闭时不存在 STA 扫描，允许在信道 1 上使用 ESP-NOW
    return true;
}

// 在 GOT_IP / AP 切换后重新评估 ESP-NOW 是否还能工作
static void network_refresh_espnow_gate(void)
{
    const bool allowed = network_espnow_allowed();

    if (s_espnow_enabled && !allowed)
    {
        DualSerial.printf("[Net] WiFi channel %u, stopping ESP-NOW (requires channel 1)\n",
                          (unsigned)WiFi.channel());
        stop_espnow(true);
    }
    else if (!s_espnow_enabled && s_espnow_blocked && allowed)
    {
        start_espnow();
    }
}

// ==================== 1. TCP/IP串口模块实现 ====================
// 回调：当网络收到数据时
static void handleData(void *arg, AsyncClient *client, void *data, size_t len)
{
    if (!console_submit((const char *)data, len))
    {
        DualSerial.println("[Net] TCP command rejected (empty, too long, or busy)");
    }
}
// 回调：当客户端断开连接时
static void handleDisconnect(void *arg, AsyncClient *client)
{
    // 清除对应槽位
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] == client)
        {
            clients[i] = nullptr;
            DualSerial.printf("[Net] TCP Client disconnected from slot [%d]\n", i);
            break;
        }
    }
}
// 回调：当有新客户端连接时
static void handleNewClient(void *arg, AsyncClient *client)
{
    // 寻找空闲的插槽
    int freeIndex = -1;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        // 如果插槽为空，或者原客户端已经断开连接
        if (clients[i] == nullptr || !clients[i]->connected())
        {
            freeIndex = i;
            break;
        }
    }

    // 如果没有空闲插槽，则拒绝连接
    if (freeIndex == -1)
    {
        DualSerial.println("[Net] TCP Connection limit reached, rejecting new connection.");
        client->close();
        return;
    }

    // 保存新客户端到空闲插槽
    clients[freeIndex] = client;

    // 为这个新客户端注册回调
    client->onData(&handleData, nullptr);
    client->onDisconnect(&handleDisconnect, nullptr);

    DualSerial.printf("[Net] TCP Client connected to slot [%d]\n", freeIndex);
}

// ==================== 低内存自愈 ====================
// ESP-IDF SoftAP 在客户端 802.11 省电模式下会偶发缓存单播帧却不置 TIM 位
// （IDFGH-18026），帧滞留到客户端重新关联才释放。下行流量持续时驱动缓冲
// 和 lwIP 重传会不断累积，内部堆耗尽后 WiFi 发不出包——表现为 AP 能搜到/
// 能关联，但网页打不开。
// 保底策略：堆到临界值后只重启 WiFi 射频回收驱动缓冲，HTTP/TCP 监听保留，
// 客户端会自动重连（不能停止 HTTP/TCP 服务：会丢 80 端口监听）。
//
// 阈值按“真的快分配不出缓冲”来定，而不是按 AP-only 时代的宽松值：
// APSTA + BLE + MQTT + 双 WebSocket 会让内部堆长期低于旧阈值，
// 32KB/16KB 在健康状态下也会持续误触发。lwIP pbuf 和 WiFi TX buffer
// 单块约 1.6KB，连续块 8KB 仍有充足余量。
//
// 该缺陷只影响 AP 客户端，因此只有 AP 上真的有客户端关联时才自愈。
#define AP_HEAP_GUARD_FREE_MIN (20 * 1024)
#define AP_HEAP_GUARD_LARGEST_MIN (8 * 1024)
#define AP_HEAP_GUARD_EMERGENCY_FREE (10 * 1024)
#define AP_HEAP_GUARD_EMERGENCY_LARGEST (4 * 1024)
#define AP_HEAP_GUARD_LOW_HOLD_MS 3000   // 低内存持续 3s 才动手
#define AP_HEAP_GUARD_RETRY_MIN_MS 10000 // 两次自愈的最小间隔
#define AP_HEAP_GUARD_RETRY_MAX_MS (5 * 60 * 1000)
#define AP_HEAP_GUARD_HEALTHY_MS 30000 // 恢复健康 30s 后重置退避

static bool s_heap_guard_started = false;

static void ap_rf_restart(void)
{
    net_lock();

    // 关闭现有连接，但不动监听 socket
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] && clients[i]->connected())
        {
            clients[i]->close();
        }
        clients[i] = nullptr;
    }
    ap_web_close_all();

    const bool was_ap = s_ap_enabled;
    s_ap_enabled = false;
    update_rf_state(); // APSTA -> STA(ESP-NOW) / OFF，驱动回收 AP 缓冲
    vTaskDelay(pdMS_TO_TICKS(500));
    s_ap_enabled = was_ap;
    update_rf_state(); // -> APSTA，重新 softAP
    network_refresh_espnow_gate();

    net_unlock();
}

static void ap_heap_guard_task(void *param)
{
    (void)param;
    uint32_t low_since = 0;
    uint32_t healthy_since = 0;
    uint32_t retry_interval_ms = 0;
    uint32_t next_restart_ms = 0;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_ap_enabled)
        {
            // 新一次 AP 会话从最小间隔重新开始
            low_since = 0;
            healthy_since = 0;
            retry_interval_ms = 0;
            next_restart_ms = 0;
            continue;
        }

        const uint32_t int_free =
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const uint32_t largest =
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const bool low = int_free < AP_HEAP_GUARD_FREE_MIN ||
                         largest < AP_HEAP_GUARD_LARGEST_MIN;

        if (!low)
        {
            low_since = 0;

            // 只有明显高于阈值才算健康，避免贴着阈值来回抖动时反复重置退避
            if (int_free >= AP_HEAP_GUARD_FREE_MIN + 4096 &&
                largest >= AP_HEAP_GUARD_LARGEST_MIN + 4096)
            {
                if (healthy_since == 0)
                    healthy_since = millis();
                else if (millis() - healthy_since >= AP_HEAP_GUARD_HEALTHY_MS)
                {
                    retry_interval_ms = 0;
                    next_restart_ms = 0;
                }
            }
            else
            {
                healthy_since = 0;
            }
            continue;
        }

        healthy_since = 0;

        // 没有 AP 客户端时这个缺陷不可能在起作用，重启射频只会打断 STA 页面
        const uint8_t stations = WiFi.softAPgetStationNum();
        if (stations == 0)
        {
            low_since = 0;
            continue;
        }

        if (low_since == 0)
            low_since = millis();

        // 非紧急情况要求低内存持续 3 秒，过滤页面加载等瞬时分配；
        // 已经逼近分配失败（连续块 <4KB 或空闲 <10KB）时立即处理。
        const bool emergency =
            int_free < AP_HEAP_GUARD_EMERGENCY_FREE ||
            largest < AP_HEAP_GUARD_EMERGENCY_LARGEST;
        if (!emergency && millis() - low_since < AP_HEAP_GUARD_LOW_HOLD_MS)
            continue;

        const uint32_t now = millis();
        if (next_restart_ms != 0 && (int32_t)(now - next_restart_ms) < 0)
            continue;

        // 重启后水位仍不恢复说明不是 AP 缓冲造成的：指数退避，避免无限重启
        retry_interval_ms = retry_interval_ms == 0
                                ? AP_HEAP_GUARD_RETRY_MIN_MS
                                : retry_interval_ms * 2;
        if (retry_interval_ms > AP_HEAP_GUARD_RETRY_MAX_MS)
            retry_interval_ms = AP_HEAP_GUARD_RETRY_MAX_MS;
        next_restart_ms = now + retry_interval_ms;
        low_since = now;

        DualSerial.printf("[Net] low heap (int=%uKB largest=%uKB sta=%u), "
                          "restarting WiFi RF (retry in %us)\n",
                          (unsigned)(int_free / 1024), (unsigned)(largest / 1024),
                          (unsigned)stations, (unsigned)(retry_interval_ms / 1000));
        ap_rf_restart();
    }
}

// Web/TCP 服务与 AP 射频解耦：两种接入方式下都启动
static void servers_start(void)
{
    if (s_servers_started)
        return;

    if (tcpServer == nullptr)
    {
        tcpServer = new AsyncServer(TCP_PORT);
    }

    if (tcpServer != nullptr)
    {
        tcpServer->onClient(&handleNewClient, nullptr);
        tcpServer->begin();
    }
    else
    {
        DualSerial.println("[Net] Failed to create TCP server");
    }

    ap_web_start();
    s_servers_started = true;
}

static void servers_stop(void)
{
    if (!s_servers_started)
        return;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] != nullptr && clients[i]->connected())
        {
            clients[i]->close();
        }
        clients[i] = nullptr;
    }

    if (tcpServer != nullptr)
    {
        tcpServer->end();
    }

    ap_web_stop();
    s_servers_started = false;
    DualSerial.println("[Net] TCP + Web servers stopped");
}

// ==================== 2. 启动与状态 ====================
void network_init(void)
{
    if (s_net_mutex == nullptr)
        s_net_mutex = xSemaphoreCreateMutex();

    net_lock();

    s_network_suspended = false;
    s_sta_configured = device_config_has_wifi();
    s_sta_enabled = s_sta_configured;
    s_got_ip_since_begin = false;
    s_cached_failures = 0;

    WiFi.onEvent(on_wifi_event, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(on_wifi_event, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    if (s_sta_configured)
    {
        device_config_get_wifi(s_sta_ssid, sizeof(s_sta_ssid),
                               s_sta_pass, sizeof(s_sta_pass));
        WiFi.setAutoReconnect(true);

        update_rf_state(); // -> WIFI_STA
        build_mdns_hostname();
        WiFi.setHostname(s_mdns_hostname);

        s_sta_begin_called = true;

        uint8_t cached_channel = 0;
        if (device_config_get_wifi_channel(&cached_channel))
        {
            s_cached_channel = cached_channel;
            s_has_cached_channel = true;
            s_sta_config_uses_cached_channel = true;
            DualSerial.printf("[Net] STA mode, connecting to \"%s\" (cached ch %u)\n",
                              s_sta_ssid, (unsigned)cached_channel);
            WiFi.begin(s_sta_ssid, s_sta_pass, (int32_t)cached_channel);
        }
        else
        {
            s_has_cached_channel = false;
            s_sta_config_uses_cached_channel = false;
            DualSerial.printf("[Net] STA mode, connecting to \"%s\"\n", s_sta_ssid);
            WiFi.begin(s_sta_ssid, s_sta_pass);
        }
    }
    else
    {
        s_ap_enabled = true;
        update_rf_state(); // -> WIFI_AP_STA + softAP
        if (s_ap_started)
            DualSerial.printf("[Net] No WiFi config, AP started: %s\n",
                              WiFi.softAPIP().toString().c_str());
        else
            DualSerial.println("[Net] No WiFi config, AP failed to start");
    }

    servers_start();

    if (!s_heap_guard_started)
    {
        s_heap_guard_started = true;
        xTaskCreatePinnedToCore(ap_heap_guard_task, "ap_heap_guard", 3072,
                                nullptr, 1, nullptr, 0);
    }

    net_unlock();
}

bool network_is_sta_configured(void)
{
    return s_sta_configured;
}

bool network_is_sta_enabled(void)
{
    return s_sta_configured && s_sta_enabled;
}

bool network_is_sta_connected(void)
{
    if (!s_sta_enabled || s_network_suspended)
        return false;

    const wifi_mode_t mode = WiFi.getMode();
    return (mode & WIFI_MODE_STA) != 0 && WiFi.status() == WL_CONNECTED;
}

const char *network_get_mdns_hostname(void)
{
    return s_mdns_hostname;
}

void network_handle_sta_got_ip(int32_t channel)
{
    if (s_network_suspended || !s_sta_enabled)
        return;

    net_lock();

    if (s_network_suspended || !s_sta_enabled)
    {
        net_unlock();
        return;
    }

    const int ch = channel > 0 ? (int)channel : (int)WiFi.channel();
    char ip[16] = {};
    WiFi.localIP().toString().toCharArray(ip, sizeof(ip));

    DualSerial.printf("[Net] WiFi connected: \"%s\", IP %s, ch %d, RSSI %d dBm\n",
                      s_sta_ssid, ip, ch, (int)WiFi.RSSI());

    s_got_ip_since_begin = true;
    s_cached_failures = 0;

    // 记录本次实际连接的信道，供下次开机从该信道开始扫描。
    // 只有变化时才写 NVS，避免频繁擦写。
    const uint8_t current_channel = WiFi.channel();
    if (current_channel >= 1 && current_channel <= 14 &&
        (!s_has_cached_channel || current_channel != s_cached_channel))
    {
        if (device_config_set_wifi_channel(current_channel))
        {
            s_cached_channel = current_channel;
            s_has_cached_channel = true;
        }
    }

    start_mdns();
    DualSerial.printf("[Net] Web UI: http://%s/ or http://%s.local/\n",
                      ip, s_mdns_hostname);

    network_refresh_espnow_gate();

    net_unlock();
}

void network_handle_sta_disconnected(void)
{
    if (s_network_suspended)
        return;

    // 手动关闭 STA 导致的事件：不再重连，也不必打印“retrying”
    if (!s_sta_enabled)
        return;

    // AP 开启期间不允许 STA 继续扫描重连（会拖走 SoftAP 的信道）。
    // Arduino 核心在断开事件里可能已抢先发起一次重连（首次断开必重试），
    // 这里把它按下去。
    if (s_ap_enabled)
    {
        net_lock();
        sta_retry_pause();
        net_unlock();
    }

    // 缓存信道连不上时回退到全信道扫描：
    //   - 开机后首次连接就失败：立即回退，避免 Arduino 核心再次撞同一个信道
    //   - 已经成功连接过：连续失败两次再回退，避免路由器短暂抖动就丢弃缓存
    if (s_sta_config_uses_cached_channel && !s_ap_enabled)
    {
        if (s_cached_failures < 0xFF)
            ++s_cached_failures;

        const bool fallback = !s_got_ip_since_begin || s_cached_failures >= 2;
        if (fallback)
        {
            net_lock();
            if (!s_ap_enabled)
            {
                s_sta_config_uses_cached_channel = false;
                s_cached_failures = 0;
                DualSerial.println("[Net] First connect attempt timed out, rescanning");
                WiFi.begin(s_sta_ssid, s_sta_pass);
            }
            net_unlock();
        }
    }

    static uint32_t last_log_ms = 0;
    const uint32_t now = millis();
    if (now - last_log_ms >= 5000)
    {
        last_log_ms = now;
        DualSerial.printf("[Net] WiFi disconnected (status %d), %s\n",
                          (int)WiFi.status(),
                          s_sta_retry_paused ? "auto-retry paused" : "retrying...");
    }

    // 不在这里恢复 ESP-NOW：信道可能仍是旧值，等下一次 GOT_IP 再判断
}

void network_suspend(void)
{
    if (s_network_suspended)
        return;

    net_lock();

    if (s_network_suspended)
    {
        net_unlock();
        return;
    }

    s_network_suspended = true;

    servers_stop();

    if (s_mdns_started)
    {
        MDNS.end();
        s_mdns_started = false;
    }

    if (s_espnow_ready || s_espnow_enabled)
    {
        s_espnow_ready = false;
        s_espnow_enabled = false;
        esp_now_deinit();
    }

    s_ap_enabled = false;
    s_ap_started = false;
    s_current_wifi_mode = WIFI_MODE_NULL;
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    DualSerial.println("[Net] Network suspended for OTA");

    net_unlock();
}

// ==================== 3. AP / WiFi STA 控制 ====================
void ap_toggle(void)
{
    net_lock();

    if (s_network_suspended)
    {
        DualSerial.println("[Net] AP toggle ignored (network suspended)");
        net_unlock();
        return;
    }

    if (s_ap_enabled)
    {
        s_ap_enabled = false;
        update_rf_state();
        sta_retry_resume();
        DualSerial.println("[Net] AP stopped");
    }
    else
    {
        // 先停 STA 重连，避免 APSTA 启动后 STA 扫描把 AP 信道拖走
        sta_retry_pause();
        s_ap_enabled = true;
        update_rf_state();
        if (s_ap_started)
            DualSerial.printf("[Net] AP started: %s (ch %u)\n",
                              WiFi.softAPIP().toString().c_str(),
                              (unsigned)WiFi.channel());
        else
            DualSerial.println("[Net] AP failed to start");
    }

    network_refresh_espnow_gate();
    net_unlock();
}

bool ap_is_running(void)
{
    // 返回实际状态：softAP 启动失败时网页图标不应显示“已开启”
    return s_ap_enabled && s_ap_started;
}

// 开/关 WiFi STA。关闭时只断 STA，AP 与 ESP-NOW 保持原状态；
// 开启时若 AP 也开着，只尝试连接一次（全信道扫描会拖走 SoftAP 的信道）。
void wifi_toggle(void)
{
    net_lock();

    if (s_network_suspended)
    {
        DualSerial.println("[Net] WiFi toggle ignored (network suspended)");
        net_unlock();
        return;
    }

    if (!s_sta_configured)
    {
        DualSerial.println("[Net] WiFi toggle ignored (no WiFi configured)");
        net_unlock();
        return;
    }

    if (s_sta_enabled)
    {
        s_sta_enabled = false;
        s_sta_retry_paused = true;
        WiFi.setAutoReconnect(false);
        esp_wifi_disconnect(); // 只断 STA，AP 不受影响；未连接时静默失败

        if (s_mdns_started)
        {
            MDNS.end();
            s_mdns_started = false;
        }

        // AP 开着时仍保持 APSTA（避免重启 AP）；否则关掉射频或退回 ESP-NOW 所需模式
        update_rf_state();
        DualSerial.println("[Net] WiFi STA stopped");
    }
    else
    {
        s_sta_enabled = true;

        if (s_ap_enabled)
        {
            // 只发起一次连接：失败后保持暂停自动重连，等 AP 关闭再恢复
            s_sta_retry_paused = true;
            WiFi.setAutoReconnect(false);
            update_rf_state(); // 已是 APSTA，不会重启 AP
            s_sta_config_uses_cached_channel = false;
            s_cached_failures = 0;
            WiFi.begin(s_sta_ssid, s_sta_pass);
            DualSerial.printf("[Net] WiFi STA enabled (single attempt while AP is on): \"%s\"\n",
                              s_sta_ssid);
        }
        else
        {
            s_sta_retry_paused = true; // 统一走 resume：恢复自动重连并发起连接
            update_rf_state();         // -> WIFI_STA
            sta_retry_resume();
            DualSerial.printf("[Net] WiFi STA enabled: \"%s\"\n", s_sta_ssid);
        }
    }

    network_refresh_espnow_gate();
    net_unlock();
}

void ap_print(const char *buffer, size_t length)
{
    if (!s_servers_started)
        return;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] && clients[i]->connected())
        {
            clients[i]->add(buffer, length);
            clients[i]->send();
        }
    }

    ap_web_print(buffer, length); // 同时广播到 WebSocket 客户端
}

// ==================== 4. ESP-NOW 模块实现 ====================
static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

static void start_espnow(void)
{
    if (s_network_suspended || s_espnow_enabled)
        return;

    if (!network_espnow_allowed())
    {
        if (s_sta_enabled && WiFi.status() == WL_CONNECTED)
        {
            DualSerial.printf("[Net] ESP-NOW disabled: WiFi channel %u (requires channel 1)\n",
                              (unsigned)WiFi.channel());
        }
        if (s_sta_configured)
            s_espnow_blocked = true;
        return;
    }

    s_espnow_enabled = true;
    update_rf_state();

    // 未与 AP/STA 关联时，射频可能停留在旧信道；显式回到 1
    if (!s_ap_enabled && WiFi.status() != WL_CONNECTED)
    {
        const esp_err_t err = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK)
            DualSerial.printf("[Net] Failed to set channel 1 (%d)\n", (int)err);
    }

    if (esp_now_init() != ESP_OK)
    {
        DualSerial.println("[Net] ESP-NOW Init Failed!");
        s_espnow_enabled = false; // init 失败回滚 flag
        update_rf_state();
        return;
    }

    esp_now_register_send_cb(OnDataSent);

    // 添加广播 Target
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        DualSerial.println("[Net] ESP-NOW add broadcast peer failed");
    }

    s_espnow_ready = true;
    s_espnow_blocked = false;
    DualSerial.println("[Net] ESP-NOW Started");
}

static void stop_espnow(bool blocked_by_channel)
{
    const bool was_running = s_espnow_enabled || s_espnow_ready;

    s_espnow_ready = false;
    s_espnow_enabled = false;
    s_espnow_blocked = blocked_by_channel;

    if (was_running)
    {
        esp_now_deinit();
        update_rf_state();
        DualSerial.println("[Net] ESP-NOW Stopped");
    }
    else
    {
        update_rf_state();
    }
}

void espnow_toggle(void)
{
    net_lock();

    if (s_network_suspended)
    {
        DualSerial.println("[Net] ESP-NOW toggle ignored (network suspended)");
        net_unlock();
        return;
    }

    if (s_espnow_enabled)
    {
        stop_espnow(false);
        net_unlock();
        return;
    }

    if (!network_espnow_allowed())
    {
        if (s_sta_enabled && WiFi.status() == WL_CONNECTED)
        {
            DualSerial.printf("[Net] ESP-NOW requires WiFi channel 1 (current %u)\n",
                              (unsigned)WiFi.channel());
        }
        // 记录本次启动请求，等 WiFi 回到信道 1 时由 network_refresh_espnow_gate 补启
        if (s_sta_configured)
            s_espnow_blocked = true;
        net_unlock();
        return;
    }

    start_espnow();
    net_unlock();
}

bool espnow_is_running(void)
{
    return s_espnow_enabled;
}

// 通用底层发送接口：把具体的数据内容与长度完全解耦给外部
bool espnow_send_data(const uint8_t *data, size_t len)
{
    if (!s_espnow_ready || data == nullptr || len == 0)
        return false;

    return (esp_now_send(broadcastMac, data, len) == ESP_OK);
}

// ==================== 5. mDNS 与 WiFi 事件 ====================
static void build_mdns_hostname(void)
{
    char device_name[30] = {};
    device_config_get_ble_name(device_name, sizeof(device_name));

    char base[24] = {};
    size_t out = 0;
    for (const char *p = device_name; *p != '\0' && out + 1 < sizeof(base); ++p)
    {
        const char c = (char)tolower((unsigned char)*p);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            base[out++] = c;
        }
        else if (out > 0 && base[out - 1] != '-')
        {
            base[out++] = '-';
        }
    }
    while (out > 0 && base[out - 1] == '-')
        --out;

    if (out == 0)
    {
        const char *fallback = "muggles-wand";
        for (size_t i = 0; fallback[i] != '\0' && out + 1 < sizeof(base); ++i)
            base[out++] = fallback[i];
    }
    base[out] = '\0';

    uint8_t mac[6] = {};
    WiFi.macAddress(mac);
    snprintf(s_mdns_hostname, sizeof(s_mdns_hostname), "%s-%02x%02x%02x",
             base, mac[3], mac[4], mac[5]);
}

static void start_mdns(void)
{
    if (s_mdns_started || s_mdns_hostname[0] == '\0')
        return;

    if (MDNS.begin(s_mdns_hostname))
    {
        MDNS.addService("http", "tcp", 80);
        s_mdns_started = true;
    }
    else
    {
        DualSerial.println("[Net] mDNS start failed");
    }
}

static void on_wifi_event(arduino_event_id_t event)
{
    // AP-only 模式下 WiFi.mode 切换也可能触发 STA 事件，直接忽略
    if (!s_sta_configured)
        return;

    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        EventBus::publish(EVENT_NET_STA_GOT_IP, (int32_t)WiFi.channel());
        break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        if (s_sta_begin_called)
            EventBus::publish(EVENT_NET_STA_DISCONNECTED);
        break;

    default:
        break;
    }
}
