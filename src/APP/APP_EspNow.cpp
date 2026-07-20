#include "APP_EspNow.h"
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

#define CHANNEL 1
static const uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum MsgType
{
    MSG_HEARTBEAT = 0x01,
    MSG_SIGNAL = 0x02
};

static void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

// 高频心跳任务：每 20ms 发射一次，方便接收端瞬间捕捉
static void heartbeatTask(void *pvParameters)
{
    uint8_t heartbeatData = MSG_HEARTBEAT;
    while (true)
    {
        esp_now_send(broadcastMac, &heartbeatData, sizeof(heartbeatData));
        vTaskDelay(pdMS_TO_TICKS(20)); // 高频广播：20ms
    }
}

void APP_espnow_init()
{
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK)
    {
        ESP.restart();
    }

    esp_now_register_send_cb(OnDataSent);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastMac, 6);
    peerInfo.channel = CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    xTaskCreate(heartbeatTask, "hb_task", 2048, NULL, 1, NULL);
}

void APP_espnow_tx_signal()
{
    uint8_t signalData = MSG_SIGNAL;
    esp_now_send(broadcastMac, &signalData, sizeof(signalData));
}