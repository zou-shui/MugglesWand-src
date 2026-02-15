#include "gesture_buffer.h"

GestureBuffer g_gesture_buffer;

void initGestureBuffer()
{
    g_gesture_buffer.write_idx = 0;
    g_gesture_buffer.sample_count = 0;
    g_gesture_buffer.ready[0] = false;
    g_gesture_buffer.ready[1] = false;
    g_gesture_buffer.mutex = xSemaphoreCreateMutex();
    g_gesture_buffer.inference_task = nullptr;
}

void addSample(float magnitude, float delta)
{
    xSemaphoreTake(g_gesture_buffer.mutex, portMAX_DELAY);

    int widx = g_gesture_buffer.write_idx;
    int count = g_gesture_buffer.sample_count;

    // 写入当前缓冲区
    g_gesture_buffer.buffer[widx][count][0] = magnitude;
    g_gesture_buffer.buffer[widx][count][1] = delta;
    g_gesture_buffer.sample_count++;

    // 检查是否填满
    if (g_gesture_buffer.sample_count >= kWindowSize)
    {
        // 标记当前缓冲区准备好
        g_gesture_buffer.ready[widx] = true;

        // 切换到另一个缓冲区
        int next_idx = (widx + 1) % 2;

        // 滑动窗口：复制后 (kWindowSize - kSlideStep) 个点到新缓冲区
        // 实现重叠，保持连续性
        int overlap = kWindowSize - kSlideStep;
        for (int i = 0; i < overlap; i++)
        {
            g_gesture_buffer.buffer[next_idx][i][0] =
                g_gesture_buffer.buffer[widx][kSlideStep + i][0];
            g_gesture_buffer.buffer[next_idx][i][1] =
                g_gesture_buffer.buffer[widx][kSlideStep + i][1];
        }

        g_gesture_buffer.write_idx = next_idx;
        g_gesture_buffer.sample_count = overlap; // 已填充重叠部分

        // 通知推理任务
        if (g_gesture_buffer.inference_task != nullptr)
        {
            xTaskNotifyGive(g_gesture_buffer.inference_task);
        }
    }

    xSemaphoreGive(g_gesture_buffer.mutex);
}

bool acquireBuffer(float **buffer_out, int *buf_idx)
{
    xSemaphoreTake(g_gesture_buffer.mutex, portMAX_DELAY);

    // 查找准备好的缓冲区（非当前写入的）
    int read_idx = (g_gesture_buffer.write_idx + 1) % 2;

    if (!g_gesture_buffer.ready[read_idx])
    {
        xSemaphoreGive(g_gesture_buffer.mutex);
        return false;
    }

    // 标记为处理中（防止重复读取）
    g_gesture_buffer.ready[read_idx] = false;
    *buf_idx = read_idx;
    *buffer_out = &g_gesture_buffer.buffer[read_idx][0][0];

    xSemaphoreGive(g_gesture_buffer.mutex);
    return true;
}

void releaseBuffer(int buf_idx)
{
    // 实际在这里可以添加标记，如果需要的话
    // 目前简单处理，下次acquire会自动检查ready标志
}