#include "HAL.h"
#include "I2Cdev.h"
#include "HAL_MPU6050.hpp"
#include "Wire.h"

MyMPU6050 mpu;

TaskHandle_t mpu6050_task_handle = NULL;

#define INTERRUPT_PIN PIN_IMU_INT

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;        // [w, x, y, z]         quaternion container
VectorInt16 aa;      // [x, y, z]            accel sensor measurements
VectorInt16 gy;      // [x, y, z]            gyro sensor measurements
VectorInt16 aaReal;  // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld; // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity; // [x, y, z]            gravity vector
float euler[3];      // [psi, theta, phi]    Euler angle container
float ypr[3];        // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

// packet structure for InvenSense teapot demo
uint8_t teapotPacket[14] = {'$', 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, '\r', '\n'};

volatile bool mpuInterrupt = false; // indicates whether MPU interrupt pin has gone high

void IRAM_ATTR dmpDataReady()
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
    while (1)
    {
        if (!dmpReady)
        {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
        // 阻塞等中断，0 = 一直等
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // read a packet from FIFO
        if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer))
        {
            mpu.dmpGetAccel(&aa, fifoBuffer);
            mpu.dmpGetGyro(&gy, fifoBuffer);
            Serial.printf("%d,%d,%d,%d,%d,%d\n", aa.x, aa.y, aa.z, gy.x, gy.y, gy.z);
            // 统计频率（每秒打印一次）
        //     count++;
        //     uint32_t now = millis();
        //     if (now - lastTime >= 1000)
        //     {
        //         Serial.printf("[STAT] 频率: %d Hz\n", count);
        //         count = 0;
        //         lastTime = now;
        //     }
        }
    }
}

void HAL::mpu6050_delete()
{
    if (mpu6050_task_handle)
    {
        detachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN));
        mpu.setDMPEnabled(false);

        vTaskDelete(mpu6050_task_handle);
        mpu6050_task_handle = NULL;
    }
}

void HAL::mpu6050_start()
{
    Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL);
    Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties

    mpu.initialize();
    pinMode(INTERRUPT_PIN, INPUT);

    // load and configure the DMP
    devStatus = mpu.dmpInitialize();

    if (devStatus == 0)
    {
        // turn on the DMP, now that it's ready
        mpu.setDMPEnabled(true);

        // enable interrupt detection
        attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
    }
    else
    {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
    }

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
