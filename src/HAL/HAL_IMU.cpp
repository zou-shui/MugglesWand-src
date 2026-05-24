/*
    ICM42670P六轴传感器功能实现
*/
#include "HAL.h"
#include "HAL_ICM42670P.h"
#include "config.h"
#include "Model/gesture_buffer.h"
#include <math.h>

// Instantiate an ICM42670 with LSB address set to 0
ICM42670 IMU(Wire, 0, 400000);

TaskHandle_t icm42670p_task_handle = NULL;

// 互补滤波与对齐算法参数定义
#define DT 0.01f // 100Hz采样率 -> 10ms
#define HALF_DT 0.005f
#define KP 2.0f   // 加速度计反馈增益
#define KI 0.005f // 陀螺仪积分误差增益

// 姿态四元数
static float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
static float exInt = 0.0f, eyInt = 0.0f, ezInt = 0.0f;

// =========================================================================
void event_cb(inv_imu_sensor_event_t *evt)
{
    ICM42670::IMU_PhysicalData_t Data;
    IMU.convertToPhysical(evt, Data);

    if (IMU.isAccelDataValid(evt) && IMU.isGyroDataValid(evt))
    {
        float ax = Data.Ax;
        float ay = Data.Ay;
        float az = Data.Az;

        float gx_raw = Data.Gx;
        float gy_raw = Data.Gy;
        float gz_raw = Data.Gz;

        // 用于四元数更新的临时变量
        float gx = gx_raw;
        float gy = gy_raw;
        float gz = gz_raw;

        // 1.Mahony 互补滤波实时更新姿态四元数 (保持对重力方向的追踪)
        float norm = 1.0 / sqrtf(ax * ax + ay * ay + az * az);
        ax *= norm;
        ay *= norm;
        az *= norm;

        // 当前估计的重力向量在 IMU 坐标系下的分量 [vx, vy, vz]
        float vx = 2.0f * (q1 * q3 - q0 * q2);
        float vy = 2.0f * (q0 * q1 + q2 * q3);
        float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

        // 互补滤波误差校正
        float ex = (ay * vz - az * vy);
        float ey = (az * vx - ax * vz);
        float ez = (ax * vy - ay * vx);

        exInt += ex * KI;
        eyInt += ey * KI;
        ezInt += ez * KI;

        gx += KP * ex + exInt;
        gy += KP * ey + eyInt;
        gz += KP * ez + ezInt;

        // 四元数更新
        float q0L = q0, q1L = q1, q2L = q2, q3L = q3;
        q0 += (-q1L * gx - q2L * gy - q3L * gz) * HALF_DT;
        q1 += (q0L * gx + q2L * gz - q3L * gy) * HALF_DT;
        q2 += (q0L * gy - q1L * gz + q3L * gx) * HALF_DT;
        q3 += (q0L * gz + q1L * gy - q2L * gx) * HALF_DT;

        norm = 1.0 / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
        q0 *= norm;
        q1 *= norm;
        q2 *= norm;
        q3 *= norm;

        // 2.时刻刷新的动态重力投影
        float valid_gx = gx_raw;
        float valid_gz = gz_raw;
        float corrected_angle_deg = 0.0f; // 用于观测的修正角度

        // vx, vz 是重力在 X-Z 平面的投影
        float mag = sqrtf(vx * vx + vz * vz);

        if (mag > 0.001f)
        {
            // 使用 atan2f 明确计算出当前芯片 X 轴与重力投影方向的绝对夹角（弧度）
            float current_theta = atan2f(vz, vx);

            // 实时计算修正角度（使用 ESP32 默认自带的 RAD_TO_DEG 宏转为角度制）
            corrected_angle_deg = current_theta * RAD_TO_DEG;

            // 计算旋转矩阵的三角函数
            float cos_t = cosf(current_theta);
            float sin_t = sinf(current_theta);

            // 2D 旋转矩阵对齐：将未受滤波污染的干净角速度对齐到动态重力基准线
            valid_gx = gx_raw * cos_t + gz_raw * sin_t;
            valid_gz = -gx_raw * sin_t + gz_raw * cos_t;
        }

        // 压入神经网络训练缓冲区
        // recordGestureData(valid_gx, valid_gz);

        // 串口输出：[对齐后的GX], [对齐后的GZ], [实时修正自旋角(度)]
        Serial.printf("%f,%f,%f\n", valid_gx, valid_gz, corrected_angle_deg);
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

    // 初始化算法变量
    q0 = 1.0f;
    q1 = 0.0f;
    q2 = 0.0f;
    q3 = 0.0f;
    exInt = 0.0f;
    eyInt = 0.0f;
    ezInt = 0.0f;

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
