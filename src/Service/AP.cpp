#include "AP.h"
#include <WiFi.h>
#include <AsyncTCP.h> // 引入异步 TCP 库
#include "Config.h"
#include "Service/Console.h"
#include "Service/DualPrint.h"

#define MAX_CLIENTS 2 // 最大连接数限制为 2

static AsyncServer *tcpServer = nullptr;
static AsyncClient *clients[MAX_CLIENTS] = {nullptr}; // 使用数组保存最多 2 个客户端句柄
static bool ap_is_running = false;

// 前向声明回调函数
static void handleNewClient(void *arg, AsyncClient *client);
static void handleData(void *arg, AsyncClient *client, void *data, size_t len);
static void handleDisconnect(void *arg, AsyncClient *client);

static void start_ap_server(void)
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    if (tcpServer == nullptr)
    {
        tcpServer = new AsyncServer(TCP_PORT);
    }

    // 注册新客户端连接的回调函数
    tcpServer->onClient(&handleNewClient, nullptr);
    tcpServer->begin();

    ap_is_running = true;
    DualSerial.println("\n[AP] TCP Server Started. Waiting for clients to connect...");
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
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    ap_is_running = false;
    DualSerial.println("\n[AP] TCP Server Stopped.");
}

void ap_toggle(void)
{
    if (ap_is_running)
        stop_ap_server();
    else
        start_ap_server();
}

// 串口数据转网络：广播给所有在线客户端
void ap_print(const char *buffer, size_t length)
{
    if (!ap_is_running)
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

// ==================== 以下全为回调函数 ====================

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