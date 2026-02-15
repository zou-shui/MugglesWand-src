#include "HAL.h"
#include "HAL_MPU6050.hpp"
#include <cmath>

MyMPU6050 mpu;
MyMPU6050::Data_t data;

TaskHandle_t mpu6050_task_handle = NULL;

#define INTERRUPT_PIN PIN_IMU_INT

void IRAM_ATTR mpuDataReady()
{
    if (!mpu6050_task_handle)
        return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(
        mpu6050_task_handle,
        0,
        eNoAction,
        &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

static void mpu6050_task(void *pvParameters)
{
    uint32_t count = 0;
    uint32_t lastTime = 0;

    float prevTheta = 0.0f;
    bool firstSample = true;
    bool reDelta = false;

    while (1)
    {
        // 阻塞等中断
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 统计频率（每秒打印一次）
        // count++;
        // uint32_t now = millis();
        // if (now - lastTime >= 1000)
        // {
        //     Serial.printf("[STAT] 频率: %d Hz\n", count);
        //     count = 0;
        //     lastTime = now;
        // }

        mpu.getAllData(data);
        // Serial.printf("%f,%f,%f,%f,%f,%f\n", data.Ax, data.Ay, data.Az, data.Gx, data.Gy, data.Gz);
        // Serial.printf("%d,%d,%d,%d,%d,%d\n", data.Accel_X_RAW, data.Accel_Y_RAW, data.Accel_Z_RAW,
        //               data.Gyro_X_RAW, data.Gyro_Y_RAW, data.Gyro_Z_RAW);

        float magnitude = 0.0f;
        float theta = 0.0f;
        float delta = 0.0f;

        // ==============================
        // 1️⃣ 旋转强度
        // ==============================
        magnitude = sqrtf(data.Gx * data.Gx + data.Gz * data.Gz);

        // ==============================
        // 2️⃣ 方向
        // ==============================
        const float threshold = 0.5f; // 旋转强度小于此值时认为没有明确方向

        if (magnitude > threshold)
        {
            theta = -atan2f(data.Gz, data.Gx);

            // ==============================
            // 3️⃣ 方向变化率
            // ==============================

            if (!firstSample && !reDelta)
            {
                delta = theta - prevTheta;

                // 角度归一化到 (-π, π]
                if (delta > M_PI)
                    delta -= 2.0f * M_PI;
                else if (delta < -M_PI)
                    delta += 2.0f * M_PI;
            }
            reDelta = false;
        }
        else
        {
            // 如果接近零向量，方向保持不变
            theta = prevTheta;
            // 且下一次如果有明确方向时，不进行 delta 计算（避免抖动）
            reDelta = true;
        }
        

        prevTheta = theta;
        firstSample = false;

        Serial.printf("%f,%f,%f\n", magnitude, theta, delta);
    }
}

void HAL::mpu6050_delete()
{
    if (mpu6050_task_handle)
    {
        detachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN));

        vTaskDelete(mpu6050_task_handle);
        mpu6050_task_handle = NULL;
    }
}

void HAL::mpu6050_start()
{
    if (mpu6050_task_handle != NULL)
    {
        Serial.println("[MPU6050] Task already running");
        return;
    }
    Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL);
    Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties

    mpu.initialize();
    mpu.setGyroUnitDPS(false); // 使用 rad/s

    pinMode(INTERRUPT_PIN, INPUT);

    // enable interrupt detection
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), mpuDataReady, RISING);

    // 创建 FreeRTOS 任务
    xTaskCreatePinnedToCore(
        mpu6050_task,
        "MPU6050_Task",
        4096,
        NULL,
        3,
        &mpu6050_task_handle,
        1);
}
