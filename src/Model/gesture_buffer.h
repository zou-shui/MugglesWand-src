#pragma once
#include <Arduino.h>

// 缓冲区配置
constexpr int kWindowSize = 100; // 模型输入长度
constexpr int kSlideStep = 5;    // 滑动步长（每5点推理一次）
constexpr int kNumFeatures = 2;  // 特征数 magnitude + delta

// 双缓冲结构
struct GestureBuffer
{
    float buffer[2][kWindowSize][kNumFeatures]; // 双缓冲
    int write_idx;                              // 当前写入的缓冲索引 (0或1)
    int sample_count;                           // 当前缓冲区已采样数
    bool ready[2];                              // 缓冲区是否准备好推理

    SemaphoreHandle_t mutex;     // 保护缓冲区访问
    TaskHandle_t inference_task; // 推理任务句柄，用于通知
};

extern GestureBuffer g_gesture_buffer;

// 初始化缓冲区
void initGestureBuffer();

// 添加一个采样点
void addSample(float magnitude, float delta);

// 获取准备好的缓冲区（推理任务调用）
// 返回true表示有数据，buffer_out指向数据，需要调用releaseBuffer释放
bool acquireBuffer(float **buffer_out, int *buf_idx);

// 释放缓冲区（推理完成后调用）
void releaseBuffer(int buf_idx);
