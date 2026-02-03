#pragma once

// Project Information
#define PROJECT_NAME "MagicWand"
#define FIRMWARE_VER "1.0"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// OTA的热点
#define OTA_AP_SSID "MagicWand-OTA"
#define OTA_AP_PASS "meiyoumima"

// MPU6050
#define PIN_IMU_SDA 10 // IMU SDA引脚
#define PIN_IMU_SCL 11 // IMU SCL引脚
#define PIN_IMU_INT 12 // IMU中断引脚

// WS2812
#define PIN_WS2812 8        // WS2812 数据引脚
#define WS2812_LED_COUNT 160 // WS2812 灯珠数量
// #define PIN_WS2812 39       // WS2812 数据引脚
// #define WS2812_LED_COUNT 60 // WS2812 灯珠数量

// 红外
#define PIN_IR_LED 38 // 红外发射引脚
#define PIN_IR_REC 21 // 红外接收引脚

// 电池
#define PIN_BATTERY_VOLTAGE 18 // 电池电压检测引脚
#define PIN_BATTERY_CHG_DET 17 // 充电状态检测引脚
