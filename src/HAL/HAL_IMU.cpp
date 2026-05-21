/*
    ICM42670P六轴传感器功能实现
*/
#include "HAL.h"
#include "HAL_ICM42670P.h"
#include "config.h"
#include "Model/gesture_buffer.h"

// Instantiate an ICM42670 with LSB address set to 0
ICM42670 IMU(Wire, 0, 400000);

TaskHandle_t icm42670p_task_handle = NULL;

void event_cb(inv_imu_sensor_event_t *evt)
{
    static uint32_t count = 0;
    static uint32_t lastTime = 0;

    static float prevTheta = 0.0f;
    static bool firstSample = true;
    static bool reDelta = false;

    // Format data for Serial Plotter
    if (IMU.isAccelDataValid(evt) && IMU.isGyroDataValid(evt))
    {
        float magnitude = 0.0f;
        float theta = 0.0f;
        float delta = 0.0f;

        // ==============================
        // 1️⃣ 旋转强度
        // ==============================
        magnitude = sqrtf(evt->gyro[0] * evt->gyro[0] + evt->gyro[2] * evt->gyro[2]);

        // ==============================
        // 2️⃣ 方向
        // ==============================
        const float threshold = 0.5f; // 旋转强度小于此值时认为没有明确方向

        if (magnitude > threshold)
        {
            theta = -atan2f(evt->gyro[2], evt->gyro[0]);

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

        // Serial.printf("%f,%f\n", magnitude, delta);
        // 添加到缓冲区（自动触发推理）
        addSample(magnitude, delta);
    }
}

void IRAM_ATTR imuDataReady(void)
{
    if (!icm42670p_task_handle)
        return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(icm42670p_task_handle, 0, eNoAction, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

static void icm42670p_task(void *pvParameters)
{
    // 初始化缓冲区
    initGestureBuffer();
    while (1)
    {
        // 阻塞等中断
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        IMU.getDataFromFifo(event_cb);
    }
}

void HAL::ICM42670_WakeOnMotion()
{
    IMU.startWakeOnMotion(100);
}

void HAL::ICM42670P_stop()
{
    if (!icm42670p_task_handle)
        return;
    IMU.enterSleepMode();
    vTaskDelete(icm42670p_task_handle);
    icm42670p_task_handle = NULL;
}

void HAL::ICM42670P_start()
{
    if (icm42670p_task_handle != NULL)
    {
        Serial.println("[ICM42670P] Task already running");
        return;
    }

    IMU.enableDataFromFifoInterrupt(PIN_IMU_INT, imuDataReady);

    xTaskCreatePinnedToCore(
        icm42670p_task,
        "ICM42670P_Task",
        4096,
        NULL,
        4,
        &icm42670p_task_handle,
        0);
}

bool HAL::ICM42670P_init()
{
    int ret;

    Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL);

    // Initializing the ICM42670
    ret = IMU.begin();
    if (ret != 0)
    {
        Serial.print("[ERROR] ICM42670P initialization failed: ");
        Serial.println(ret);
        return false;
    }
    // 此时传感器处于sleep模式

    return true;
}
