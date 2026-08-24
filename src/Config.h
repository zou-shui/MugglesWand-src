#pragma once

// Project Information
#define PROJECT_NAME "Muggles' Wand" // 项目名称
#define FIRMWARE_VER "5.0"           // 硬件版本.软件版本
#define BUILD_TIME __DATE__ "  " __TIME__

// AP
#define AP_SSID "Zoushui's Wand" // AP热点名称
#define AP_PASS "12345678"       // AP热点密码（至少8位）

// BLE
#define BLE_DEVICE_NAME "Zoushui's Wand" // BLE设备名称

// ICM42670P
#define PIN_IMU_SDA 10 // IMU SDA引脚(外部上拉)
#define PIN_IMU_SCL 11 // IMU SCL引脚(外部上拉)
#define PIN_IMU_INT 12 // IMU中断引脚(IMU自身内部上拉)

// WS2812
#define PIN_WS2812 38       // WS2812 数据引脚
#define PIN_WS2812_EN 39    // WS2812 使能引脚
#define WS2812_LED_COUNT 41 // WS2812 灯珠数量

// 电池计量
#define PIN_BATT_GAUGE_SCL 17 // 电池电量检测SCL引脚(外部上拉)
#define PIN_BATT_GAUGE_SDA 18 // 电池电量检测SDA(外部上拉)
#define PIN_BATT_GAUGE_INT 14 // 电池电量检测中断引脚(外部上拉)

// 电源使能(外部上拉)
#define PIN_PWR_EN 21

// 按键
#define PIN_KEY 13

// LED控制兼充电检测
#define PIN_CHG_DET 9
#define CHG_DET_THRESHOLD_MV 1500 // 充电检测电压阈值（mV），高于此值认为在充电