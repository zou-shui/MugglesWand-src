/*
    WiFi射频管理模块：统一管理 ESP32-S3 的 WiFi 射频资源，避免 AP 与 ESP-NOW 之间的冲突
*/
#include "AP.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <AsyncTCP.h>
#include "Config.h"
#include "Service/Console.h"
#include "Service/DualPrint.h"

// ==================== 配置与常量定义 ====================
#define TCP_PORT 8080  // TCP 服务器监听端口
#define WIFI_CHANNEL 1 // ESP-NOW 与 AP 统一使用的信道
#define MAX_CLIENTS 2  // TCP Server 最大连接数

static const uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ==================== 模块内部变量 ====================
static AsyncServer *tcpServer = nullptr;
static AsyncClient *clients[MAX_CLIENTS] = {nullptr};

static bool is_ap_enabled = false;
static bool is_espnow_enabled = false;

// ==================== 核心射频(RF)统一调度器 ====================
static void update_rf_state(void)
{
    if (is_ap_enabled && is_espnow_enabled)
    {
        // 两者都需要：开启 AP+STA 混合模式
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID, AP_PASS, WIFI_CHANNEL);
        esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
        DualSerial.println("[AP] RF Mode -> WIFI_AP_STA");
    }
    else if (is_ap_enabled && !is_espnow_enabled)
    {
        // 只有 AP 需要：开启纯 AP 模式
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS, WIFI_CHANNEL);
        esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
        DualSerial.println("[AP] RF Mode -> WIFI_AP");
    }
    else if (!is_ap_enabled && is_espnow_enabled)
    {
        // 只有 ESP-NOW 需要：开启纯 STA 模式
        WiFi.mode(WIFI_STA);
        esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
        DualSerial.println("[AP] RF Mode -> WIFI_STA");
    }
    else
    {
        // 两者都被关闭：安全彻底切断射频，实现极致省电
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        DualSerial.println("[AP] RF Mode -> WIFI_OFF");
    }
}

// ==================== 1. TCP/IP串口模块实现 ====================
// 回调：当网络收到数据时
static void handleData(void *arg, AsyncClient *client, void *data, size_t len)
{
    char *str = (char *)data;

    //  利用 len 找到当前数据的真正末尾，强制加上 C 字符串结束符
    str[len] = '\0';
    console_parse(str); // 将接收到的数据传递给串口命令解析器
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
            DualSerial.printf("[AP] Client disconnected from slot [%d]\n", i);
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
        DualSerial.println("[AP] Connection limit reached, rejecting new connection.");
        client->close();
        return;
    }

    // 保存新客户端到空闲插槽
    clients[freeIndex] = client;

    // 为这个新客户端注册回调
    client->onData(&handleData, nullptr);
    client->onDisconnect(&handleDisconnect, nullptr);

    DualSerial.printf("[AP] Client connected to slot [%d]\n", freeIndex);
}

static void start_ap_server(void)
{
    is_ap_enabled = true;
    update_rf_state(); // 统一刷新底层 RF 模式

    if (tcpServer == nullptr)
    {
        tcpServer = new AsyncServer(TCP_PORT);
    }

    // 注册新客户端连接的回调函数
    tcpServer->onClient(&handleNewClient, nullptr);
    tcpServer->begin();

    DualSerial.println("[AP] TCP Server Started.");
    DualSerial.print("[AP] IP Address: ");
    DualSerial.println(WiFi.softAPIP());
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
    DualSerial.println("[AP] TCP Server Stopped.");
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
}

// ==================== 2. ESP-NOW 模块实现 ====================
static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

static void start_espnow(void)
{
    is_espnow_enabled = true;
    update_rf_state(); // 统一刷新底层 RF 模式

    if (esp_now_init() != ESP_OK)
    {
        DualSerial.println("[AP] ESP-NOW Init Failed!");
        return;
    }

    esp_now_register_send_cb(OnDataSent);

    // 添加广播 Target
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastMac, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    DualSerial.println("[AP] ESP-NOW Started.");
}

static void stop_espnow(void)
{
    esp_now_deinit();

    is_espnow_enabled = false;
    update_rf_state(); // 如果 AP 没开，此时会自动 WiFi.mode(WIFI_OFF)
    DualSerial.println("[AP] ESP-NOW Stopped.");
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
    if (!is_espnow_enabled || data == nullptr || len == 0)
        return false;

    return (esp_now_send(broadcastMac, data, len) == ESP_OK);
}
