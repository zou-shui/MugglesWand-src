#include "gesture_buffer.h"
#include "gesture_model.h" // 你的模型头文件
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

constexpr int kNumClasses = 5; // 输出类别数

// 归一化参数（需要与你的训练数据一致）
constexpr float kMean[2] = {2.89347857f, 0.01675318f}; // 替换为实际的 mean 值
constexpr float kStd[2] = {2.98847213f, 0.16172716f};  // 替换为实际的 std 值

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
    // 注册到缓冲区系统
    g_gesture_buffer.inference_task = xTaskGetCurrentTaskHandle();

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

    Serial.println("Inference task ready");

    // 主循环
    while (1)
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
            Serial.printf("[Gesture] Class: %d, Time: %.2f ms\n",
                          predicted_class, inference_time);

            // TODO: 在这里执行手势对应的动作
            handleGesture(predicted_class);

            releaseBuffer(buf_idx);
        }
    }
}
