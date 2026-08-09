#pragma once
#include <stdint.h>
#include <stdbool.h>

// POV (视觉暂留) 光绘模块：手势进入模式，IMU 线性加速度触发光绘动画
// 触发: 模式内正交于 Y 轴的挥动（剔除重力后的 X-Z 线性加速度超阈值）→ 光绘动画

// @brief 设置下次光绘触发的参数（由 EVENT_POV_SET 驱动，仅存参不触发）
// @param patternIndex 图案索引 (0 ~ POV_PATTERN_COUNT-1)
// @param reverse      是否从最后一行倒序播放（适配反向挥舞）
void APP_POV_set_params(uint8_t patternIndex, bool reverse);

// @brief 进入 POV 模式（手势 5 触发）：点亮紫色模式指示灯
void APP_POV_mode_enter(void);

// @brief 退出 POV 模式（单击按键）：熄灭指示灯并恢复手势状态灯
void APP_POV_mode_exit(void);

// @brief 当前是否处于 POV 模式
bool APP_POV_in_mode(void);

// @brief POV 模式下的 IMU 数据回调（mux 5：剔除重力后的 X-Z 平面线性加速度，单位 g）
//        挥动幅度超阈值立即触发光绘；静止/微加速度时保持待命等待
void APP_POV_on_imu_data(float linearX, float linearZ);

// @brief 打印内置图案清单
void APP_POV_list(void);
