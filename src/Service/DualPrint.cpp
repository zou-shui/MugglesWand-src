// dual_serial.cpp
#include "DualPrint.h"
#include "Service/AP.h"

size_t DualPrint::write(uint8_t c)
{
    Serial.write(c);
    char buf[2] = {(char)c, '\0'};
    ap_print(buf, 1);
    return 1;
}

size_t DualPrint::write(const uint8_t *buffer, size_t size)
{
    Serial.write(buffer, size);
    ap_print((const char *)buffer, size);
    return size;
}

// 定义全局对象（实例化）
DualPrint DualSerial;