#pragma once

#include <Arduino.h>

#define TCP_PORT 8080 // TCP 服务器监听端口

// ================= 外部接口 =================

/**
 * @brief 初始化网络串口模块
 * @note 内部会配置串口、WiFi模式，并默认启动 AP 和 TCP 服务器
 */
void ap_init(void);

/**
 * @brief 切换 AP 的开关状态（用于节省电量）
 * @note 如果当前是开启状态，调用后将关闭热点和服务器；反之开启。
 */
void ap_toggle(void);

/**
 * @brief 向当前连接的网络客户端发送数据
 * @param buffer 数据缓冲区指针
 * @param length 数据长度
 */
void ap_print(const char *buffer, size_t length);
