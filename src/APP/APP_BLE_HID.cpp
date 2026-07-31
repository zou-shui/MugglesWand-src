#include "APP_BLE_HID.h"
#include "Service/BLE.h"
#include <Arduino.h>

// ====================== 鼠标参数 ============================
// 鼠标灵敏度：弧度/秒 → 鼠标像素位移的缩放系数
// 例如 1 rad/s 的旋转 → 1 * 20 = 20 像素的位移
#define MOUSE_SENSITIVITY 20.0f

// 死区阈值（弧度/秒）：低于此值的角速度视为噪声，不产生鼠标移动
#define MOUSE_DEADZONE 0.05f

// ======================== 音量旋钮参数 ========================
// EMA 滤波系数
#define VOLUME_EMA_ALPHA 0.2f

// 每触发一次音量步进所需的最小旋转角度（度）
#define VOLUME_STEP_ANGLE 1.5f

// 角度变化死区（度）：低于此值的旋转视为噪声
#define VOLUME_DEADZONE 0.0001f

// 两次 BLE 音量发送之间的最小间隔（毫秒），防止快速旋转时报文洪泛
#define VOLUME_SEND_INTERVAL_MS 100

// 旋转方向死区修正：累计器绝对值超过此阈值时，只允许同向步进
// 避免从慢速顺向转入慢速逆向时，累积器在阈值附近反复跨越
#define VOLUME_HYSTERESIS 0.0003f

bool APP_ble_keyboard_press_up(void)
{
    return ble_keyboard_tap_key(KEY_UP); // KEY_UP 定义在 BLEHIDKeys.h 中为 0x52
}

bool APP_ble_keyboard_press_down(void)
{
    return ble_keyboard_tap_key(KEY_DOWN); // KEY_DOWN 定义在 BLEHIDKeys.h 中为 0x51
}

void APP_ble_mouse_move(float gx_rad, float gz_rad)
{
    // 死区过滤
    if (fabsf(gx_rad) < MOUSE_DEADZONE)
        gx_rad = 0.0f;
    if (fabsf(gz_rad) < MOUSE_DEADZONE)
        gz_rad = 0.0f;

    // 缩放并钳位到 int8_t 范围
    int8_t dx = (int8_t)constrain((int32_t)(gx_rad * MOUSE_SENSITIVITY), -127L, 127L);
    int8_t dy = (int8_t)constrain((int32_t)(-gz_rad * MOUSE_SENSITIVITY), -127L, 127L);

    // 两轴都为 0 则跳过，减少 BLE 报文发送
    if (dx == 0 && dy == 0)
        return;

    ble_mouse_move(dx, dy);
}

void APP_ble_volume_knob(float angle_deg)
{
    // ---- 静态状态：跨调用保持 ----
    static float last_raw_angle = 0.0f; // 上一次的原始角度（用于计算原始 delta）
    static bool initialized = false;    // 首次调用标志
    static float smooth_delta = 0.0f;   // EMA 滤波后的角度变化量（度/周期）
    static float accumulator = 0.0f;    // 旋转累积器（度）
    static uint32_t last_send_ms = 0;   // 上次发送 BLE 报文的时间戳

    uint32_t now_ms = millis();

    // ---- 1. 首次调用初始化 ----
    if (!initialized)
    {
        last_raw_angle = angle_deg;
        smooth_delta = 0.0f;
        accumulator = 0.0f;
        last_send_ms = now_ms;
        initialized = true;
        return;
    }

    // ---- 2. 基于原始角度计算 delta（含环绕处理）----
    // 关键：delta 必须在原始角度上计算，不能在 EMA 滤波后的值上算。
    //       EMA 是线性运算，无法理解圆环拓扑，跨 ±180° 时会把 179°→-179°
    //       这样仅 2° 的真实旋转错算成数百度的虚假跳变。
    float raw_delta = angle_deg - last_raw_angle;

    if (raw_delta > 180.0f)
        raw_delta -= 360.0f;
    else if (raw_delta < -180.0f)
        raw_delta += 360.0f;

    last_raw_angle = angle_deg;

    // ---- 3. 安全钳位：拒绝物理上不可能的瞬时角速度 ----
    // 100Hz 采样率下，人类极限旋转速度 < 2000°/s → 单步 ≤ 20°。
    // 超过 30° 的 delta 一定是传感器故障或角度回绕异常（如 near-vertical 时
    // corrected_angle_deg 跌回 0.0），直接丢弃本帧。
    if (raw_delta > 30.0f || raw_delta < -30.0f)
    {
        return;
    }

    // ---- 4. EMA 低通滤波（作用在 delta 上，而非绝对角度）----
    smooth_delta = VOLUME_EMA_ALPHA * raw_delta + (1.0f - VOLUME_EMA_ALPHA) * smooth_delta;

    // ---- 5. 死区过滤 ----
    if (fabsf(smooth_delta) < VOLUME_DEADZONE)
    {
        smooth_delta = 0.0f;
    }

    // ---- 6. 更新累积器 ----
    accumulator += smooth_delta;

    // ---- 7. 速率限制检查 ----
    if ((now_ms - last_send_ms) < VOLUME_SEND_INTERVAL_MS)
    {
        return;
    }

    // ---- 8. 阈值步进判断 ----
    bool sent = false;

    while (accumulator >= VOLUME_STEP_ANGLE)
    {
        ble_consumer_send(MEDIA_VOLUME_UP);
        accumulator -= VOLUME_STEP_ANGLE;
        sent = true;
    }

    while (accumulator <= -VOLUME_STEP_ANGLE)
    {
        ble_consumer_send(MEDIA_VOLUME_DOWN);
        accumulator += VOLUME_STEP_ANGLE;
        sent = true;
    }

    if (sent)
    {
        last_send_ms = now_ms;

        // 防止方向反转时累积器在阈值附近抖动
        if (accumulator > -VOLUME_HYSTERESIS && accumulator < VOLUME_HYSTERESIS)
            accumulator = 0.0f;
    }
}