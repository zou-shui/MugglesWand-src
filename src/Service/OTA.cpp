/*
    OTA服务，用于通过WiFi进行固件更新
*/
#include "OTA.h"
#include "Config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>

/************ AP配置 ************/
const char *ap_ssid = OTA_AP_SSID;     // WiFi AP名称
const char *ap_password = OTA_AP_PASS; // WiFi AP密码

/************ Web服务器 ************/
WebServer server(80);

/************ OTA状态 ************/
unsigned long ota_progress_millis = 0;

/************ OTA IP地址 ************/
static IPAddress ota_ip;

/************ OTA回调 ************/
static void onOTAStart()
{
    Serial.println("[OTA] update started!");
}

static void onOTAProgress(size_t current, size_t final)
{
    if (millis() - ota_progress_millis > 1000)
    {
        ota_progress_millis = millis();
        Serial.printf("[OTA] Progress: %u / %u bytes\n", current, final);
    }
}

static void onOTAEnd(bool success)
{
    if (success)
    {
        Serial.println("[OTA] update finished successfully!");
    }
    else
    {
        Serial.println("[OTA] update failed!");
    }
}

/************ OTA循环 ************/
static void OTA_loop(void *param)
{
    while (1)
    {
        server.handleClient();
        ElegantOTA.loop();
        vTaskDelay(10);
    }
}

IPAddress OTA_get_ip()
{
    return ota_ip;
}

/************ OTA启动 ************/
void OTA_begin()
{
    static bool ota_running = false;
    if (ota_running)
    {
        Serial.println("[OTA] Already running");
        Serial.print("[OTA] SSID: ");
        Serial.println(ap_ssid);
        Serial.print("[OTA] Password: ");
        Serial.println(ap_password);
        Serial.println("[OTA] Open browser: http://192.168.4.1/update");
        return;
    }
    ota_running = true;

    Serial.println("[OTA] Starting AP mode...");

    // 设置AP模式
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);

    ota_ip = WiFi.softAPIP();

    Serial.println("[OTA] AP Started");
    Serial.print("[OTA] SSID: ");
    Serial.println(ap_ssid);
    Serial.print("[OTA] Password: ");
    Serial.println(ap_password);
    Serial.print("[OTA] IP: ");
    Serial.println(ota_ip);

    // 根目录测试
    server.on("/", []()
              { server.send(200, "text/plain", "ESP32 OTA Ready\nVisit /update"); });

    // 启动ElegantOTA
    ElegantOTA.begin(&server);

    ElegantOTA.onStart(onOTAStart);
    ElegantOTA.onProgress(onOTAProgress);
    ElegantOTA.onEnd(onOTAEnd);

    server.begin();

    Serial.println("[OTA] HTTP Server Started");
    Serial.println("[OTA] Open browser: http://192.168.4.1/update");

    xTaskCreatePinnedToCore(
        OTA_loop,
        "OTA_loop",
        4096,
        NULL,
        4,
        NULL,
        0);
}