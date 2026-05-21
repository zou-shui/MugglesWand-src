#pragma once

// Project Information
#define PROJECT_NAME "MagicWand"
#define FIRMWARE_VER "4.0" // 硬件版本.软件版本
#define BUILD_TIME __DATE__ "  " __TIME__

// OTA的热点
#define OTA_AP_SSID "MagicWand-OTA"
#define OTA_AP_PASS "meiyoumima"

// ICM42670P
#define PIN_IMU_SDA 10 // IMU SDA引脚
#define PIN_IMU_SCL 11 // IMU SCL引脚
#define PIN_IMU_INT 12 // IMU中断引脚

// WS2812
#define PIN_WS2812 39       // WS2812 数据引脚
#define WS2812_LED_COUNT 41 // WS2812 灯珠数量

// 电池计量
#define PIN_BATT_GAUGE_SCL 17 // 电池电量检测SCL引脚
#define PIN_BATT_GAUGE_SDA 18 // 电池电量检测SDA
#define PIN_BATT_GAUGE_INT 14 // 电池电量检测中断引脚

// 电源使能
#define PIN_PWR_EN 21

// 按键
#define PIN_KEY 13
