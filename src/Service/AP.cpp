/*
    WiFi射频管理模块：统一管理 ESP32-S3 的 WiFi 射频资源，避免 AP 与 ESP-NOW 之间的冲突
*/
#include "AP.h"
#include "AP_Web.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <AsyncTCP.h>
#include "Config.h"
#include "Service/Console.h"
#include "Service/DualPrint.h"
#include <esp_heap_caps.h>

// ==================== 配置与常量定义 ====================
#define TCP_PORT 8080 // TCP 服务器监听端口
#define MAX_CLIENTS 2 // TCP Server 最大连接数

static const uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ==================== 模块内部变量 ====================
static AsyncServer *tcpServer = nullptr;
static AsyncClient *clients[MAX_CLIENTS] = {nullptr};

static bool is_ap_enabled = false;
static bool is_espnow_enabled = false;

// ESP-NOW readiness：init+add_peer 完成后置 true，deinit 前先置 false
static volatile bool s_espnow_ready = false;

// 已下发到 WiFi 驱动的 mode：用于 update_rf_state() 幂等保护，避免重复 WiFi.mode()
static wifi_mode_t s_current_wifi_mode = WIFI_MODE_NULL;

// ==================== 核心射频(RF)统一调度器 ====================
// AP 开启时 WiFi 锁定 APSTA，ESP-NOW 切换不触发 WiFi.mode()，
// 避免在 NimBLE 运行期间反复拆装 STA 子接口导致共存状态机崩溃。
static void update_rf_state(void)
{
    wifi_mode_t want;
    if (is_ap_enabled)
    {
        want = WIFI_MODE_APSTA;
    }
    else if (is_espnow_enabled)
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
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID, AP_PASS);
        DualSerial.println("[AP] RF Mode -> WIFI_AP_STA");
        break;
    case WIFI_MODE_STA:
        WiFi.mode(WIFI_STA);
        DualSerial.println("[AP] RF Mode -> WIFI_STA");
        break;
    default:
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        DualSerial.println("[AP] RF Mode -> WIFI_OFF");
        break;
    }
}

// ==================== 1. TCP/IP串口模块实现 ====================
// 回调：当网络收到数据时
static void handleData(void *arg, AsyncClient *client, void *data, size_t len)
{
    if (!console_submit((const char *)data, len))
    {
        DualSerial.println("[AP] TCP command rejected (empty, too long, or busy)");
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
            DualSerial.printf("[AP] TCP Client disconnected from slot [%d]\n", i);
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
        DualSerial.println("[AP] TCP Connection limit reached, rejecting new connection.");
        client->close();
        return;
    }

    // 保存新客户端到空闲插槽
    clients[freeIndex] = client;

    // 为这个新客户端注册回调
    client->onData(&handleData, nullptr);
    client->onDisconnect(&handleDisconnect, nullptr);

    DualSerial.printf("[AP] TCP Client connected to slot [%d]\n", freeIndex);
}

// ==================== 低内存自愈 ====================
// ESP-IDF SoftAP 在客户端 802.11 省电模式下会偶发缓存单播帧却不置 TIM 位
// （IDFGH-18026），帧滞留到客户端重新关联才释放。下行流量持续时驱动缓冲
// 和 lwIP 重传会不断累积，内部堆耗尽后 WiFi 发不出包——表现为 AP 能搜到/
// 能关联，但网页打不开。
// 保底策略：堆到临界值后只重启 WiFi 射频回收驱动缓冲，HTTP/TCP 监听保留，
// 客户端会自动重连（不能调用 stop_ap_server：会丢 80 端口监听）。
#define AP_HEAP_GUARD_FREE_MIN (32 * 1024)
#define AP_HEAP_GUARD_LARGEST_MIN (16 * 1024)
#define AP_HEAP_GUARD_EMERGENCY_LARGEST (8 * 1024)

static bool s_heap_guard_started = false;

static void ap_rf_restart(void)
{
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

    is_ap_enabled = false;
    update_rf_state(); // APSTA -> STA(ESP-NOW) / OFF，驱动回收 AP 缓冲
    vTaskDelay(pdMS_TO_TICKS(500));
    is_ap_enabled = true;
    update_rf_state(); // -> APSTA，重新 softAP
}

static void ap_heap_guard_task(void *param)
{
    (void)param;
    uint32_t low_since = 0;
    uint32_t last_restart = 0;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!is_ap_enabled)
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
        DualSerial.printf("[AP] low heap (int=%u largest=%u), restarting WiFi RF\n",
                          (unsigned)int_free, (unsigned)largest);
        ap_rf_restart();
    }
}

static void start_ap_server(void)
{
    is_ap_enabled = true;
    update_rf_state(); // 统一刷新底层 RF 模式

    if (!s_heap_guard_started)
    {
        s_heap_guard_started = true;
        xTaskCreatePinnedToCore(ap_heap_guard_task, "ap_heap_guard", 3072, nullptr, 1, nullptr, 0);
    }

    if (tcpServer == nullptr)
    {
        tcpServer = new AsyncServer(TCP_PORT);
    }

    // 注册新客户端连接的回调函数
    tcpServer->onClient(&handleNewClient, nullptr);
    tcpServer->begin();

    DualSerial.println("[AP] TCP Server Started");
    DualSerial.print("[AP] IP Address: ");
    DualSerial.println(WiFi.softAPIP());
    ap_web_start(); // 同时启动 Web 控制面板
}

static void stop_ap_server(void)
{
    // 关闭所有在线的客户端
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] && clients[i]->connected())
        {
            clients[i]->close();
        }
        clients[i] = nullptr;
    }

    if (tcpServer)
    {
        tcpServer->end();
    }

    is_ap_enabled = false;
    update_rf_state(); // 如果 ESP-NOW 没开，此时会自动 WiFi.mode(WIFI_OFF)
    DualSerial.println("[AP] TCP Server Stopped");
    ap_web_stop(); // 同时停止 Web 控制面板
}

void ap_toggle(void)
{
    if (is_ap_enabled)
        stop_ap_server();
    else
        start_ap_server();
}

bool ap_is_running(void)
{
    return is_ap_enabled;
}

void ap_print(const char *buffer, size_t length)
{
    if (!is_ap_enabled)
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

// ==================== 2. ESP-NOW 模块实现 ====================
static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

static void start_espnow(void)
{
    is_espnow_enabled = true;
    update_rf_state();

    if (esp_now_init() != ESP_OK)
    {
        DualSerial.println("[AP] ESP-NOW Init Failed!");
        is_espnow_enabled = false; // init 失败回滚 flag
        update_rf_state();
        return;
    }

    esp_now_register_send_cb(OnDataSent);

    // 添加广播 Target
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastMac, 6);
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    s_espnow_ready = true;
    DualSerial.println("[AP] ESP-NOW Started");
}

static void stop_espnow(void)
{
    s_espnow_ready = false;
    is_espnow_enabled = false;

    esp_now_deinit();
    update_rf_state();

    DualSerial.println("[AP] ESP-NOW Stopped");
}

void espnow_toggle(void)
{
    if (is_espnow_enabled)
        stop_espnow();
    else
        start_espnow();
}

bool espnow_is_running(void)
{
    return is_espnow_enabled;
}

// 通用底层发送接口：把具体的数据内容与长度完全解耦给外部
bool espnow_send_data(const uint8_t *data, size_t len)
{
    if (!s_espnow_ready || data == nullptr || len == 0)
        return false;

    return (esp_now_send(broadcastMac, data, len) == ESP_OK);
}
