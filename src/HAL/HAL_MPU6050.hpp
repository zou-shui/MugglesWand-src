#pragma once
#include "MPU6050.h"

class MyMPU6050 : public MPU6050
{
public:
    // 数据容器结构体
    typedef struct
    {
        // 原始数据（RAW）
        int16_t Accel_X_RAW;
        int16_t Accel_Y_RAW;
        int16_t Accel_Z_RAW;

        int16_t Gyro_X_RAW;
        int16_t Gyro_Y_RAW;
        int16_t Gyro_Z_RAW;

        // 转换后的物理量
        double Ax; // 加速度 (g)
        double Ay;
        double Az;

        double Gx; // 角速度 (°/s 或 rad/s)
        double Gy;
        double Gz;

    } Data_t;

    // 构造函数
    MyMPU6050(uint8_t address = MPU6050_DEFAULT_ADDRESS, void *wireObj = 0)
        : MPU6050(address, wireObj) {}

    // 自定义初始化
    void initialize()
    {
        setSleepEnabled(false);
        setClockSource(MPU6050_CLOCK_PLL_XGYRO);
        setFullScaleGyroRange(MPU6050_GYRO_FS_2000);
        setFullScaleAccelRange(MPU6050_ACCEL_FS_16);
        setDLPFMode(MPU6050_DLPF_BW_20); // 低通滤波器，启用后 Gyroscope Output Rate 将降低为 1kHz，不启用则为 8kHz
        setRate(9);                      // 采样率 = Gyroscope Output Rate / (1 + rate)
        setIntEnabled(1);
        // 更新分辨率系数
        updateResolutions();
    }

    // 一键读取所有数据到结构体
    void getAllData(Data_t &data)
    {
        // 读取原始数据
        getMotion6(&data.Accel_X_RAW, &data.Accel_Y_RAW, &data.Accel_Z_RAW,
                   &data.Gyro_X_RAW, &data.Gyro_Y_RAW, &data.Gyro_Z_RAW);

        // 转换为物理单位
        convertRawToPhysical(data);
    }

    // 设置/获取转换单位
    void setAccelUnitG(bool useG)
    {
        useG_ = useG;
        updateResolutions();
    }
    void setGyroUnitDPS(bool useDPS)
    {
        useDPS_ = useDPS;
        updateResolutions();
    }

private:
    Data_t cache_;          // 内部缓存
    double accelRes_ = 0.0; // 加速度分辨率 (g/LSB 或 m/s²/LSB)
    double gyroRes_ = 0.0;  // 陀螺仪分辨率 (°/s/LSB 或 rad/s/LSB)
    bool useG_ = true;      // true=g, false=m/s²
    bool useDPS_ = true;    // true=°/s, false=rad/s

    void updateResolutions()
    {
        // 根据当前量程计算分辨率
        // 加速度：±2g = 16384 LSB/g, ±4g = 8192, ±8g = 4096, ±16g = 2048
        uint8_t accelRange = getFullScaleAccelRange();
        double accelLSB = 16384.0 / (1 << accelRange); // 2^(accelRange)
        accelRes_ = useG_ ? (1.0 / accelLSB) : (9.80665 / accelLSB);

        // 陀螺仪：±250°/s = 131 LSB/(°/s), ±500=65.5, ±1000=32.8, ±2000=16.4
        uint8_t gyroRange = getFullScaleGyroRange();
        double gyroLSB = 131.0 / (1 << gyroRange);
        gyroRes_ = useDPS_ ? (1.0 / gyroLSB) : (0.0174533 / gyroLSB); // 0.0174533 = π/180
    }

    void convertRawToPhysical(Data_t &data)
    {
        // 加速度
        data.Ax = data.Accel_X_RAW * accelRes_;
        data.Ay = data.Accel_Y_RAW * accelRes_;
        data.Az = data.Accel_Z_RAW * accelRes_;

        // 陀螺仪
        data.Gx = data.Gyro_X_RAW * gyroRes_;
        data.Gy = data.Gyro_Y_RAW * gyroRes_;
        data.Gz = data.Gyro_Z_RAW * gyroRes_;
    }
};