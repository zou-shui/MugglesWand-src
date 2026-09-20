#ifndef DUAL_SERIAL_H
#define DUAL_SERIAL_H

#include <Arduino.h>

// 自定义双路输出类
class DualPrint : public Print
{
public:
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
};

// 声明全局对象（extern），供所有包含此头文件的模块使用
extern DualPrint DualSerial;

#endif