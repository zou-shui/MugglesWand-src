#pragma once

// ============================================================
// 灯带加热助手 OPT_WandHeater（OPT = Optional，可选模块）
// ============================================================
// 用途：让灯带全灯珠最高亮度常亮，靠 LED 自身发热软化魔杖以方便矫正魔杖的弯曲形变。
// 前提：已完成魔杖制作，但魔杖外形有轻微弯曲，想要通过加热软化魔杖以矫正弯曲。
// 触发方式：在控制台执行 debug 命令后解锁，之后每次"短按按键一次"切换灯带全亮 / 熄灭。
// ！注意！该功能作为一项实验性功能，可能会导致灯带、外壳等过热损坏，请谨慎使用。

// 该功能默认关闭（不参与编译），若要启用请将 WAND_HEATER_ENABLE 置为 1。
// 在完成魔杖的形变矫正后，应当将 WAND_HEATER_ENABLE 置为 0，以避免误操作导致魔杖过热损坏。
#define WAND_HEATER_ENABLE 0 // 编译开关；0=关闭，1=启用

#if WAND_HEATER_ENABLE
void OPT_WandHeater_init(void);
#else
// 开关关闭时退化为空实现：APP_init 中的调用点无需条件编译
static inline void OPT_WandHeater_init(void) {}
#endif
