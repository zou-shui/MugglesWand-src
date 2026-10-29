/*
    OTA服务，用于通过WiFi进行固件更新
*/
#include <Arduino.h>
#include "OTA.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimProgress.hpp"
#include "HAL/WS2812_Animation/AnimBreathe.hpp"
#include "Service/AP.h"
#include "Service/DeviceConfig.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>

/************ Web服务器 ************/
WebServer server(80);

/************ OTA状态 ************/
unsigned long ota_progress_millis = 0;
uint8_t ota_progress_pct = 0; // OTA 进度百分比（0~100），供 AnimProgress 读取

/************ OTA IP地址 ************/
static IPAddress ota_ip;

/************ OTA回调 ************/
static void onOTAStart()
{
    DualSerial.println("[OTA] update started!");
    ota_progress_pct = 0;

    // 启动进度条动画
    HAL::ws2812_set_overlay(new AnimProgress(ota_progress_pct));
}

static void onOTAProgress(size_t current, size_t final)
{
    if (millis() - ota_progress_millis > 500)
    {
        ota_progress_millis = millis();
        DualSerial.printf("[OTA] Progress: %u bytes\n", current);
        ota_progress_pct = final == 0 ? 0 : (uint8_t)((current * 100) / final);
    }
}

static void onOTAEnd(bool success)
{
    if (success)
    {
        DualSerial.println("[OTA] update finished successfully!");
        ota_progress_pct = 100; // 确保进度条显示完成状态
    }
    else
    {
        DualSerial.println("[OTA] update failed!");
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
    char ap_ssid[33];
    char ap_password[64];
    device_config_get_ap(ap_ssid, sizeof(ap_ssid), ap_password, sizeof(ap_password));

    static bool ota_running = false;
    if (ota_running)
    {
        DualSerial.println("[OTA] Already running");
        DualSerial.print("[OTA] SSID: ");
        DualSerial.println(ap_ssid);
        DualSerial.print("[OTA] Password: ");
        DualSerial.println(ap_password);
        DualSerial.println("[OTA] Open browser: http://192.168.4.1/update");
        return;
    }
    ota_running = true;

    // OTA 启动时完全接管 WiFi：停止 Web/TCP/mDNS/AP/STA/ESP-NOW，
    // 避免与下面 WebServer(80) 的端口冲突
    network_suspend();

    DualSerial.println("[OTA] Starting AP mode...");

    // 设置AP模式
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);

    ota_ip = WiFi.softAPIP();

    DualSerial.println("[OTA] AP Started");
    DualSerial.print("[OTA] SSID: ");
    DualSerial.println(ap_ssid);
    DualSerial.print("[OTA] Password: ");
    DualSerial.println(ap_password);
    DualSerial.print("[OTA] IP: ");
    DualSerial.println(ota_ip);

    // 根目录测试
    server.on("/", []()
              { server.send(200, "text/plain", "ESP32 OTA Ready\nVisit /update"); });

    // 启动ElegantOTA
    ElegantOTA.begin(&server);

    ElegantOTA.onStart(onOTAStart);
    ElegantOTA.onProgress(onOTAProgress);
    ElegantOTA.onEnd(onOTAEnd);

    server.begin();

    DualSerial.println("[OTA] HTTP Server Started");
    DualSerial.println("[OTA] Open browser: http://192.168.4.1/update");

    HAL::ws2812_set_overlay(new AnimBreathe()); // OTA启动时显示呼吸动画，表示等待上传固件

    xTaskCreatePinnedToCore(
        OTA_loop,
        "OTA_loop",
        4096,
        NULL,
        4,
        NULL,
        0);
}