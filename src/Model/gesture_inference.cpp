#include "gesture_inference.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "gesture_buffer.h"
#include "gesture_model.h" // 你的模型头文件

constexpr int kNumClasses = 5; // 输出类别数

// 归一化参数（需要与你的训练数据一致）
constexpr float kMean[2] = {2.86185933f, 0.01604925f}; // 替换为实际的 mean 值
constexpr float kStd[2] = {2.96900795f, 0.16098918f};  // 替换为实际的 std 值

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

    volatile bool g_inference_running = false; // 标志位，表示推理任务是否正在运行
}

void handleGesture(int gesture_id)
{
    // 你的动作处理代码
    switch (gesture_id)
    {
    case 0: /* 动作0 */
        break;
    case 1: /* 动作1 */
        break;
    case 2: /* 动作2 */
        break;
    case 3: /* 动作3 */
        break;
    }
}

void inference_task(void *pvParameters)
{
    g_inference_running = true;
    // 注册到缓冲区系统
    g_gesture_buffer.inference_task = xTaskGetCurrentTaskHandle();

    while (g_inference_running)
    {
        // 等待缓冲区准备好
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        float *buffer = nullptr;
        int buf_idx = 0;

        // 获取缓冲区
        while (acquireBuffer(&buffer, &buf_idx))
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
                Serial.println("Invoke failed!");
                releaseBuffer(buf_idx);
                continue;
            }

            // 处理输出
            int8_t max_val = -128;
            int predicted_class = 0;

            for (int i = 0; i < kNumClasses; i++)
            {
                int8_t val = output->data.int8[i];
                if (val > max_val)
                {
                    max_val = val;
                    predicted_class = i;
                }
            }

            int64_t end_time = esp_timer_get_time();
            float inference_time = (end_time - start_time) / 1000.0f; // ms

            // 输出结果
            Serial.printf("%d,%.2f\n",
                          predicted_class, inference_time);

            // TODO: 在这里执行手势对应的动作
            handleGesture(predicted_class);

            releaseBuffer(buf_idx);
        }
    }
    vTaskDelete(NULL);
}

void inference_deinit()
{
    if (inference_task_handle != NULL)
    {
        g_inference_running = false;

        // 唤醒任务（防止卡在 ulTaskNotifyTake）
        xTaskNotifyGive(inference_task_handle);

        // 等待任务自己删除
        vTaskDelay(pdMS_TO_TICKS(50));

        inference_task_handle = NULL;
    }

    // 清空指针（可选但推荐）
    interpreter = nullptr;
    model = nullptr;
    input = nullptr;
    output = nullptr;
}

void inference_init()
{
    // 初始化TFLite
    model = tflite::GetModel(gesture_model_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        Serial.println("Model version mismatch!");
        vTaskDelete(NULL);
        return;
    }

    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize, &error_reporter);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk)
    {
        Serial.println("AllocateTensors failed!");
        vTaskDelete(NULL);
        return;
    }

    input = interpreter->input(0);
    output = interpreter->output(0);

    // 创建推理任务
    xTaskCreatePinnedToCore(
        inference_task,
        "Inference",
        8192,
        NULL,
        2,
        &inference_task_handle,
        1);
}
