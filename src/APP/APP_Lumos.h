#pragma once
#include <stdint.h>

void APP_Lumos_trigger(uint32_t color);
void APP_Lumos_on(uint32_t color);
void APP_Lumos_off(void);

// @brief 手势映射入口：无参数，触发白色 Lumos 照明
bool APP_Lumos_execute(const char *parameter);
