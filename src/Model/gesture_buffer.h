#pragma once
#include <Arduino.h>

// 缓冲区配置
constexpr int kWindowSize = 100; // 模型输入长度
constexpr int kNumFeatures = 2;  // 特征数

// 修改后的缓冲区结构
struct GestureBuffer
{
    float history_buffer[kWindowSize][kNumFeatures]; // 环形/滚动缓冲区，永远保存最新的100个点
    int write_idx;                                   // 下一个写入的位置
    int total_samples;                               // 总采样数，用于判断是否填满过100个点
    bool data_ready;                                 // 推理数据是否准备就绪

    // 专供推理任务读取的线性缓冲区（100个点连续排列）
    float inference_data[kWindowSize][kNumFeatures];

    SemaphoreHandle_t mutex;     // 保护缓冲区访问
    TaskHandle_t inference_task; // 推理任务句柄，用于通知
};

extern GestureBuffer g_gesture_buffer;

// 初始化缓冲区
void initGestureBuffer();

// 添加一个采样点，由 en 决定是否触发推理
void addSample(float arg1, float arg2, bool en);

// 获取准备好的缓冲区（推理任务调用）
// 返回 true 表示有数据，buffer_out 指向连续的 100 个点数据
bool acquireBuffer(float **buffer_out);