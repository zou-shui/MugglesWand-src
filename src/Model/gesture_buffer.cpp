#include "gesture_buffer.h"

GestureBuffer g_gesture_buffer;

void initGestureBuffer()
{
    g_gesture_buffer.write_idx = 0;
    g_gesture_buffer.total_samples = 0;
    g_gesture_buffer.data_ready = false;
    g_gesture_buffer.mutex = xSemaphoreCreateMutex();
    g_gesture_buffer.inference_task = nullptr;
}

void addSample(float arg1, float arg2, bool en)
{
    xSemaphoreTake(g_gesture_buffer.mutex, portMAX_DELAY);

    // 1. 无论 en 为什么，先将数据存入循环历史缓冲区
    int widx = g_gesture_buffer.write_idx;
    g_gesture_buffer.history_buffer[widx][0] = arg1;
    g_gesture_buffer.history_buffer[widx][1] = arg2;

    // 更新索引和计数
    g_gesture_buffer.write_idx = (widx + 1) % kWindowSize;
    if (g_gesture_buffer.total_samples < kWindowSize)
    {
        g_gesture_buffer.total_samples++;
    }

    // 2. 当 en 为 true 时，触发推理准备
    if (en)
    {
        // 健壮性检查：如果历史数据还不够 100 个点，可以选择不推理或有多少用多少
        // 这里假设只有填满 100 个点才允许推理
        if (g_gesture_buffer.total_samples >= kWindowSize)
        {
            // 将环形缓冲区的数据按“从旧到新”的顺序复制到连续的推理缓冲区中
            // 当前的 write_idx 恰好是最旧的那个点（即将被覆盖的点）
            int read_ptr = g_gesture_buffer.write_idx;

            for (int i = 0; i < kWindowSize; i++)
            {
                g_gesture_buffer.inference_data[i][0] = g_gesture_buffer.history_buffer[read_ptr][0];
                g_gesture_buffer.inference_data[i][1] = g_gesture_buffer.history_buffer[read_ptr][1];
                read_ptr = (read_ptr + 1) % kWindowSize;
            }

            g_gesture_buffer.data_ready = true;

            // 通知推理任务
            if (g_gesture_buffer.inference_task != nullptr)
            {
                xTaskNotifyGive(g_gesture_buffer.inference_task);
            }
        }
    }

    xSemaphoreGive(g_gesture_buffer.mutex);
}

bool acquireBuffer(float **buffer_out)
{
    xSemaphoreTake(g_gesture_buffer.mutex, portMAX_DELAY);

    if (!g_gesture_buffer.data_ready)
    {
        xSemaphoreGive(g_gesture_buffer.mutex);
        return false;
    }

    // 标记为已读取
    g_gesture_buffer.data_ready = false;

    // 返回线性整理好的推理缓冲区首地址
    *buffer_out = &g_gesture_buffer.inference_data[0][0];

    xSemaphoreGive(g_gesture_buffer.mutex);
    return true;
}