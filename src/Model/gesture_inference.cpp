#include "gesture_inference.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "gesture_buffer.h"
#include "gesture_model.h" // 你的模型头文件
#include "Service/AP.h"

constexpr int kNumClasses = 8; // 输出类别数

// 归一化参数（由训练脚本输出，必须与之一致）
constexpr float kMean[2] = {0.06793611f, -0.10970584f};
constexpr float kStd[2] = {1.5366218f, 2.53526731f};

TaskHandle_t inference_task_handle = NULL;

namespace
{
    tflite::MicroErrorReporter error_reporter;
    const tflite::Model *model = nullptr;
    tflite::MicroInterpreter *interpreter = nullptr;
    TfLiteTensor *input = nullptr;
    TfLiteTensor *output = nullptr;

    constexpr int kTensorArenaSize = 50 * 1024;
    uint8_t tensor_arena[kTensorArenaSize];
}

void handleGesture(int gesture_id)
{
    // 处理手势事件，发布到事件总线
    EventBus::publish(EVENT_GESTURE_DETECTED, gesture_id);
}

void inference_task(void *pvParameters)
{
    // 初始化缓冲区
    initGestureBuffer();
    // 注册到缓冲区系统
    g_gesture_buffer.inference_task = xTaskGetCurrentTaskHandle();

    while (1)
    {
        // 等待缓冲区准备好
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        float *buffer = nullptr;

        // 获取缓冲区
        while (acquireBuffer(&buffer))
        {
            int64_t start_time = esp_timer_get_time();

            // 准备输入数据（归一化 + 量化）
            for (int i = 0; i < kWindowSize * kNumFeatures; i++)
            {
                int feature_idx = i % kNumFeatures;
                float normalized = (buffer[i] - kMean[feature_idx]) / kStd[feature_idx];

                // 量化
                int8_t quantized = static_cast<int8_t>(
                    normalized / input->params.scale + input->params.zero_point);
                input->data.int8[i] = quantized;
            }

            // 运行推理
            if (interpreter->Invoke() != kTfLiteOk)
            {
                DualSerial.println("Invoke failed!");

                continue;
            }

            // 处理输出
            int8_t max_val = -128;
            int predicted_class = 0;
            float probabilities[kNumClasses]; // 用于存储反量化后的概率值

            for (int i = 0; i < kNumClasses; i++)
            {
                int8_t val = output->data.int8[i];

                // 反量化公式: real_value = (quantized_value - zero_point) * scale
                probabilities[i] = (val - output->params.zero_point) * output->params.scale;

                if (val > max_val)
                {
                    max_val = val;
                    predicted_class = i;
                }
            }

            int64_t end_time = esp_timer_get_time();
            float inference_time = (end_time - start_time) / 1000.0f; // ms
            float max_probability = probabilities[predicted_class];

            // 输出推理结果, 包括类别、概率和推理时间
            DualSerial.printf("[CNN] C:%d P:%.2f T:%.0fms\n", predicted_class, max_probability, inference_time);

            // 如果最大概率超过阈值，则触发手势事件
            if (max_probability >= 0.6)
            {
                handleGesture(predicted_class);
            }
        }
    }
}

void inference_stop()
{
    if (inference_task_handle != NULL)
    {
        vTaskDelete(inference_task_handle);
        inference_task_handle = NULL;
    }
}

void inference_start()
{
    if (inference_task_handle != NULL)
    {
        DualSerial.println("[CNN] Task already running");
        return;
    }

    if (interpreter == nullptr || input == nullptr || output == nullptr)
    {
        DualSerial.println("[CNN] Not initialized");
        return;
    }

    xTaskCreatePinnedToCore(
        inference_task,
        "Inference",
        8192,
        NULL,
        1,
        &inference_task_handle,
        1);
}

void inference_init()
{
    if (interpreter != nullptr)
    {
        DualSerial.println("[CNN] Already initialized");
        return;
    }

    // 初始化TFLite
    model = tflite::GetModel(gesture_model_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        DualSerial.println("Model version mismatch!");
        return;
    }

    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize, &error_reporter);
    interpreter = &static_interpreter;

    // 分配张量
    if (interpreter->AllocateTensors() != kTfLiteOk)
    {
        DualSerial.println("AllocateTensors failed!");
        return;
    }

    // 获取输入输出张量
    input = interpreter->input(0);
    output = interpreter->output(0);
}
