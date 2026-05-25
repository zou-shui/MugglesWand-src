/*
    ICM42670P六轴传感器功能实现
*/
#include "HAL.h"
#include "HAL_ICM42670P.h"
#include "config.h"
#include "Model/gesture_buffer.h"
#include <math.h>
#include "Service/BLE_uart.h"

// Instantiate an ICM42670 with LSB address set to 0
ICM42670 IMU(Wire, 0, 400000);

TaskHandle_t icm42670p_task_handle = NULL;

// ======================== 有效手势状态机 ========================

// 状态定义
typedef enum
{
    STATE_INIT = 0,           // 初始状态，寻找静止起点
    STATE_STILL_DETECTED = 1, // 已检测到连续20个点静止
    STATE_MOTION_START = 2,   // 运动开始，监测峰值特征
    STATE_VALID_GESTURE = 3,  // 已检测到有效峰值特征，等待收尾静止
    STATE_SUCCESS = 4         // 成功识别手势，返回状态4后重回0
} GestureState;

// 阈值定义
#define THRESHOLD_STILL 0.5f     // 静止状态的“0附近”阈值
#define THRESHOLD_MOTION 2.0f    // 运动触发阈值
#define THRESHOLD_PEAK_POS 3.0f  // 正峰值有效阈值
#define THRESHOLD_PEAK_NEG -3.0f // 负峰值有效阈值

// 辅助结构体：用于记录峰值
typedef struct
{
    float val;
    int index;
} Peak;

int8_t valid_gesture(float gx, float gy)
{
    // 状态机内部状态及计数器
    static int8_t state = STATE_INIT;

    // 各状态所需的计数器
    static int still_cnt = 0;    // 状态0和状态3用的静止计数器
    static int p2_point_cnt = 0; // 状态2观察的点数计数器
    static int p3_point_cnt = 0; // 状态3观察的点数计数器

    // 状态2用于峰值检测的辅助变量
    static float last_gx = 0.0f, last_gy = 0.0f;
    static float trend_gx = 0, trend_gy = 0; // 1表示上升，-1表示下降，0表示未知

    // 峰值记录（这里简化逻辑：合并考虑或单独考虑。手势识别通常两轴都会出现波峰，这里以两轴各自检测为例）
    // 为了满足“相邻正负峰值”的检测，我们需要记录历史峰值
    static Peak peaks_x[20];
    static int peak_cnt_x = 0;
    static Peak peaks_y[20];
    static int peak_cnt_y = 0;

    switch (state)
    {

    case STATE_INIT:
    { // ------ 状态 0：寻找起始静止状态 ------
        if (fabsf(gx) < THRESHOLD_STILL && fabsf(gy) < THRESHOLD_STILL)
        {
            still_cnt++;
            if (still_cnt >= 20)
            {
                state = STATE_STILL_DETECTED; // 进入状态 1
                // 准备进入状态2的初始化
                last_gx = gx;
                last_gy = gy;
                trend_gx = 0;
                trend_gy = 0;
            }
        }
        else
        {
            still_cnt = 0; // 必须是连续20个点
        }
        return state;
    }

    case STATE_STILL_DETECTED:
    { // ------ 状态 1：静止完成，等待触发 ------
        if (fabsf(gx) > THRESHOLD_MOTION || fabsf(gy) > THRESHOLD_MOTION)
        {
            state = STATE_MOTION_START; // 进入状态 2
            p2_point_cnt = 0;
            peak_cnt_x = 0;
            peak_cnt_y = 0;
            last_gx = gx;
            last_gy = gy;
        }
        return state;
    }

    case STATE_MOTION_START:
    { // ------ 状态 2：波形峰值特征检测 ------
        p2_point_cnt++;
        if (p2_point_cnt > 90)
        {
            state = STATE_INIT; // 超时未匹配，回0
            still_cnt = 0;
            return STATE_INIT;
        }

        // --- 简单的实时峰值提取算法（以X轴为例，Y轴同理） ---
        // X轴峰值检测
        if (gx > last_gx)
        {
            if (trend_gx < 0 && last_gx < 0)
            {
                // 从下降转为上升 -> 发现了负峰值（波谷）
                if (peak_cnt_x < 20)
                    peaks_x[peak_cnt_x++] = (Peak){last_gx, p2_point_cnt};
            }
            trend_gx = 1; // 上升沿
        }
        else if (gx < last_gx)
        {
            if (trend_gx > 0 && last_gx > 0)
            {
                // 从上升转为下降 -> 发现了正峰值（波峰）
                if (peak_cnt_x < 20)
                    peaks_x[peak_cnt_x++] = (Peak){last_gx, p2_point_cnt};
            }
            trend_gx = -1; // 下降沿
        }
        last_gx = gx;

        // Y轴峰值检测
        if (gy > last_gy)
        {
            if (trend_gy < 0 && last_gy < 0)
            {
                if (peak_cnt_y < 20)
                    peaks_y[peak_cnt_y++] = (Peak){last_gy, p2_point_cnt};
            }
            trend_gy = 1;
        }
        else if (gy < last_gy)
        {
            if (trend_gy > 0 && last_gy > 0)
            {
                if (peak_cnt_y < 20)
                    peaks_y[peak_cnt_y++] = (Peak){last_gy, p2_point_cnt};
            }
            trend_gy = -1;
        }
        last_gy = gy;

        // --- 检查是否满足“至少两组相邻且超过阈值的正负峰值” ---
        // 这里判断标准：在已捕获的峰值序列中，是否存在相邻的(正, 负)或(负, 正)都满足阈值
        int valid_pairs = 0;

        // 检查X轴
        for (int i = 0; i < peak_cnt_x - 1; i++)
        {
            float v1 = peaks_x[i].val;
            float v2 = peaks_x[i + 1].val;
            if ((v1 >= THRESHOLD_PEAK_POS && v2 <= THRESHOLD_PEAK_NEG) ||
                (v1 <= THRESHOLD_PEAK_NEG && v2 >= THRESHOLD_PEAK_POS))
            {
                valid_pairs++;
            }
        }
        // 检查Y轴
        for (int i = 0; i < peak_cnt_y - 1; i++)
        {
            float v1 = peaks_y[i].val;
            float v2 = peaks_y[i + 1].val;
            if ((v1 >= THRESHOLD_PEAK_POS && v2 <= THRESHOLD_PEAK_NEG) ||
                (v1 <= THRESHOLD_PEAK_NEG && v2 >= THRESHOLD_PEAK_POS))
            {
                valid_pairs++;
            }
        }

        // 如果总共监测到至少两组有效相邻峰值
        if (valid_pairs >= 2)
        {
            state = STATE_VALID_GESTURE; // 进入状态 3
            p3_point_cnt = 0;
            still_cnt = 0; // 重置状态3要用的静止计数器
        }

        return state;
    }

    case STATE_VALID_GESTURE:
    { // ------ 状态 3：收尾静止检测 ------
        p3_point_cnt++;

        if (fabsf(gx) < THRESHOLD_STILL && fabsf(gy) < THRESHOLD_STILL)
        {
            still_cnt++;
            if (still_cnt >= 10)
            {
                // 成功识别手势！跳转到状态4，但根据需求，状态4执行完后要立刻回到状态0
                // 为了让调用者能看到状态4，我们先返回4，下一次调用时或者直接在此处重置。
                // 严格按照“返回状态4，然后回到状态0”：
                state = STATE_INIT;
                still_cnt = 0;
                return STATE_SUCCESS; // 返回 4
            }
        }
        else
        {
            still_cnt = 0; // 必须是连续的10个点
        }

        // 如果到了第50个点还没有凑齐连续10个静止点
        if (p3_point_cnt >= 50)
        {
            state = STATE_INIT;
            still_cnt = 0;
            return STATE_INIT; // 返回 0
        }

        return state;
    }

    default:
        state = STATE_INIT;
        return STATE_INIT;
    }
}

// ======================== 姿态解算，角速度映射 ========================

// 互补滤波与对齐算法参数定义
#define DT 0.01f // 100Hz采样率 -> 10ms
#define HALF_DT 0.005f
#define KP 2.0f   // 加速度计反馈增益
#define KI 0.005f // 陀螺仪积分误差增益

// 姿态四元数
static float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
static float exInt = 0.0f, eyInt = 0.0f, ezInt = 0.0f;

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
        int8_t sta = valid_gesture(valid_gx, valid_gz);
        addSample(valid_gx, valid_gz, sta >= 4 ? 1 : 0);

        // 串口输出：[对齐后的GX], [对齐后的GZ], [实时修正自旋角(度)]
        // char buf[64];
        // memset(buf, 0, sizeof(buf));
        // // sprintf(buf, "%f,%f,%f,%d\n", valid_gx, valid_gz, corrected_angle_deg, sta);
        // sprintf(buf, "%f,%f,%d\n", valid_gx, valid_gz, sta);
        // ble_send(buf, strlen(buf)); // 通过BLE发送数据
        // Serial.print(buf);
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
