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
static bool s_ap_enabled = false;
static bool s_espnow_enabled = false;

// ESP-NOW readiness：init+add_peer 完成后置 true，deinit 前先置 false
static volatile bool s_espnow_ready = false;

// ESP-NOW 因信道不为 1 被被动停止；之后回到信道 1 时自动恢复
static bool s_espnow_blocked = false;

// 是否配置了 WiFi STA（开机后不再改变，重启生效）
static bool s_sta_configured = false;

// WiFi.begin() 是否已经调用：用于过滤模式切换产生的早期 disconnect 事件
static bool s_sta_begin_called = false;

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
    if (!s_sta_configured || !s_sta_retry_paused)
        return;

    s_sta_retry_paused = false;
    WiFi.setAutoReconnect(true);

    if (WiFi.status() != WL_CONNECTED)
    {
        DualSerial.printf("[Net] Resuming STA connection to \"%s\"\n", s_sta_ssid);
        WiFi.begin();
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
static void update_rf_state(void)
{
    wifi_mode_t want;
    if (s_ap_enabled)
    {
        want = WIFI_MODE_APSTA;
    }
    else if (s_sta_configured || s_espnow_enabled)
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
        return;
    }
    s_current_wifi_mode = want;

    switch (want)
    {
    case WIFI_MODE_APSTA:
    {
        WiFi.mode(WIFI_AP_STA);

        char ssid[33];
        char pass[64];
        device_config_get_ap(ssid, sizeof(ssid), pass, sizeof(pass));
        if (!WiFi.softAP(ssid, pass, 1))
        {
            DualSerial.println("[Net] Failed to start softAP");
        }

        // STA 未连接时把信道钉回 1，避免残留的扫描过程让 AP 在信道间漂移；
        // ESP-NOW 同样依赖信道 1。STA 已连接时不能改，否则会断开 STA。
        if (WiFi.status() != WL_CONNECTED &&
            esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE) != ESP_OK)
        {
            DualSerial.println("[Net] Failed to pin AP channel to 1");
        }

        DualSerial.println("[Net] RF Mode -> WIFI_AP_STA");
        break;
    }
    case WIFI_MODE_STA:
        WiFi.mode(WIFI_STA);
        DualSerial.println("[Net] RF Mode -> WIFI_STA");
        break;
    default:
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        DualSerial.println("[Net] RF Mode -> WIFI_OFF");
        break;
    }
}

// ESP-NOW 依赖信道 1。AP/STA 处于其它信道时不允许开启。
bool network_espnow_allowed(void)
{
    if (s_network_suspended)
        return false;

    if (s_ap_enabled)
        return WiFi.channel() == 1;

    if (WiFi.status() == WL_CONNECTED)
        return WiFi.channel() == 1;

    // STA 未连接：若之前因信道被阻塞，保持阻塞直到下一次 GOT_IP 确认信道 1
    if (s_sta_configured && s_espnow_blocked)
        return false;

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
        DualSerial.println("[Net] WiFi back on channel 1, restarting ESP-NOW");
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
#define AP_HEAP_GUARD_FREE_MIN (32 * 1024)
#define AP_HEAP_GUARD_LARGEST_MIN (16 * 1024)
#define AP_HEAP_GUARD_EMERGENCY_LARGEST (8 * 1024)

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
    uint32_t last_restart = 0;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_ap_enabled)
        {
            low_since = 0;
            continue;
        }

        uint32_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (int_free >= AP_HEAP_GUARD_FREE_MIN && largest >= AP_HEAP_GUARD_LARGEST_MIN)
        {
            low_since = 0;
            continue;
        }

        if (low_since == 0)
        {
            low_since = millis();
        }

        // 连续 2 秒低内存才动手，避免页面加载等瞬时分配误触发；
        // 已经逼近 WiFi 发不出去包（最大块 <8KB）时立即处理。
        if (largest >= AP_HEAP_GUARD_EMERGENCY_LARGEST && millis() - low_since < 2000)
            continue;
        if (millis() - last_restart < 10000)
            continue;

        last_restart = millis();
        low_since = millis();
        DualSerial.printf("[Net] low heap (int=%u largest=%u), restarting WiFi RF\n",
                          (unsigned)int_free, (unsigned)largest);
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
        DualSerial.printf("[Net] TCP console listening on port %u\n",
                          (unsigned)TCP_PORT);
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

        DualSerial.printf("[Net] STA mode, connecting to \"%s\"\n", s_sta_ssid);
        DualSerial.println("[Net] Double-click the button to toggle AP if WiFi fails");
        s_sta_begin_called = true;
        WiFi.begin(s_sta_ssid, s_sta_pass);
    }
    else
    {
        s_ap_enabled = true;
        update_rf_state(); // -> WIFI_AP_STA + softAP
        DualSerial.printf("[Net] No WiFi config, AP started: %s\n",
                          WiFi.softAPIP().toString().c_str());
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

bool network_is_sta_connected(void)
{
    if (!s_sta_configured || s_network_suspended)
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
    if (s_network_suspended)
        return;

    net_lock();

    if (s_network_suspended)
    {
        net_unlock();
        return;
    }

    const int ch = channel > 0 ? (int)channel : (int)WiFi.channel();
    char ip[16] = {};
    WiFi.localIP().toString().toCharArray(ip, sizeof(ip));

    DualSerial.printf("[Net] WiFi connected: \"%s\", IP %s, ch %d, RSSI %d dBm\n",
                      s_sta_ssid, ip, ch, (int)WiFi.RSSI());

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

    // AP 开启期间不允许 STA 继续扫描重连（会拖走 SoftAP 的信道）。
    // Arduino 核心在断开事件里可能已抢先发起一次重连（首次断开必重试），
    // 这里把它按下去。
    if (s_ap_enabled)
    {
        net_lock();
        sta_retry_pause();
        net_unlock();
    }

    static uint32_t last_log_ms = 0;
    const uint32_t now = millis();
    if (now - last_log_ms >= 5000)
    {
        last_log_ms = now;
        DualSerial.printf("[Net] WiFi disconnected (status %d), retrying...\n",
                          (int)WiFi.status());
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
    s_current_wifi_mode = WIFI_MODE_NULL;
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    DualSerial.println("[Net] Network suspended for OTA");

    net_unlock();
}

// ==================== 3. AP 控制 ====================
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
        DualSerial.printf("[Net] AP started: %s (ch %u)\n",
                          WiFi.softAPIP().toString().c_str(),
                          (unsigned)WiFi.channel());
    }

    network_refresh_espnow_gate();
    net_unlock();
}

bool ap_is_running(void)
{
    return s_ap_enabled;
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
        DualSerial.printf("[Net] ESP-NOW disabled: WiFi channel %u (requires channel 1)\n",
                          (unsigned)WiFi.channel());
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
        DualSerial.printf("[Net] ESP-NOW requires WiFi channel 1 (current %u)\n",
                          (unsigned)WiFi.channel());
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
        DualSerial.printf("[Net] mDNS: http://%s.local/\n", s_mdns_hostname);
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
