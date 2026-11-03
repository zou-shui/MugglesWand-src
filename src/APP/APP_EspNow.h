#pragma once

#include <Arduino.h>
#include <stdbool.h>

enum MsgType
{
    MSG_HEARTBEAT = 0x01,
    MSG_SIGNAL_1 = 0x02,
    MSG_SIGNAL_2 = 0x03
};

// 初始化 ESP-NOW 发送模块：仅当有手势绑定到 espnow 时才启动服务与心跳
void APP_espnow_init();

// 手势映射变化后重新评估：现在需要 ESP-NOW 则补启（不会自动关闭）
void APP_espnow_sync(void);

// 确保心跳任务已创建（手动开启 ESP-NOW 时也调用；可重复调用）
void APP_espnow_ensure_heartbeat(void);

// 发送简短信号量的接口
void APP_espnow_tx_signal(uint8_t signalType);

// 手势映射入口：parameter 为 0~255 的十进制或十六进制字符串
bool APP_espnow_validate(const char *parameter);
bool APP_espnow_execute(const char *parameter);
