#pragma once
#include "MPU6050_6Axis_MotionApps612.h"

// 继承 MotionApps612，用于定制化初始化
class MyMPU6050 : public MPU6050_6Axis_MotionApps612
{
public:
    // 构造函数直接调用基类构造
    MyMPU6050(uint8_t address = MPU6050_DEFAULT_ADDRESS, void *wireObj = 0)
        : MPU6050_6Axis_MotionApps612(address, wireObj)
    {
    }

    // 重写 dmpInitialize
    uint8_t dmpInitialize()
    {
        // 先调用基类的 dmpInitialize, 这里DMP已经可以用了
        uint8_t result = MPU6050_6Axis_MotionApps612::dmpInitialize();
        if (result == 0)
        {
            // 再进行定制化设置，覆盖基类的默认设置
            setFullScaleGyroRange(MPU6050_GYRO_FS_2000);    // 陀螺仪量程 ±2000°/s
            setFullScaleAccelRange(MPU6050_ACCEL_FS_16);    // 加速度计量程 ±16g
        }

        return 0;
    }
};
