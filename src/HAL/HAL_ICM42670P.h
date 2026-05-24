#ifndef ICM42670_H
#define ICM42670_H

#include "Arduino.h"
#include "SPI.h"
#include "Wire.h"

extern "C"
{
#include "imu/inv_imu_driver.h"
#undef ICM42670
}

// This defines the handler called when retrieving a sample from the FIFO
typedef void (*ICM42670_sensor_event_cb)(inv_imu_sensor_event_t *event);
// This defines the handler called when receiving an irq
typedef void (*ICM42670_irq_handler)(void);

class ICM42670
{
public:
    typedef struct
    {
        float Ax, Ay, Az; // 加速度，单位: g

        float Gx, Gy, Gz; // 角速度，单位: rad/s (弧度/秒)
    } IMU_PhysicalData_t;

    ICM42670(TwoWire &i2c, bool address_lsb);
    ICM42670(TwoWire &i2c, bool lsb, uint32_t freq);

    int begin();
    int enableDataFromFifoInterrupt(uint8_t intpin, ICM42670_irq_handler handler);
    int enterSleepMode();
    int startWakeOnMotion(uint8_t wom_threshold);
    int startAccel(uint16_t odr, uint16_t fsr);
    int startGyro(uint16_t odr, uint16_t fsr);
    void convertToPhysical(const inv_imu_sensor_event_t *evt, IMU_PhysicalData_t &output);
    int getDataFromRegisters(inv_imu_sensor_event_t &evt);
    int enableFifoInterrupt(uint8_t intpin, ICM42670_irq_handler handler, uint8_t fifo_watermark);
    int getDataFromFifo(ICM42670_sensor_event_cb event_cb);
    bool isAccelDataValid(inv_imu_sensor_event_t *evt);
    bool isGyroDataValid(inv_imu_sensor_event_t *evt);

    uint8_t i2c_address;
    TwoWire *i2c;
    uint32_t clk_freq;

protected:
    struct inv_imu_device icm_driver;
    inv_imu_interrupt_parameter_t int1_config;
    float accel_sensitivity; // 1 g 对应的 LSB 计数值
    float gyro_sensitivity;  // 1 dps 对应的 LSB 计数值

    ACCEL_CONFIG0_ODR_t accel_freq_to_param(uint16_t accel_freq_hz);
    GYRO_CONFIG0_ODR_t gyro_freq_to_param(uint16_t gyro_freq_hz);
    ACCEL_CONFIG0_FS_SEL_t accel_fsr_g_to_param(uint16_t accel_fsr_g);
    GYRO_CONFIG0_FS_SEL_t gyro_fsr_dps_to_param(uint16_t gyro_fsr_dps);
};

#endif // ICM42670_H
