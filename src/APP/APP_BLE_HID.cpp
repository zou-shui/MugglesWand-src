#include "APP_BLE_HID.h"

#include "APP_Lumos.h"
#include "BLEKeySequence.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimFlow.hpp"
#include "Service/BLE.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"

#include <Arduino.h>
#include <FastLED.h>
#include <string.h>

// ============================================================================
// 键盘
// ============================================================================

namespace
{

    constexpr uint8_t kKeySequenceQueueLength = 4;

    // The request owns a complete sequence copy. The parser runs in the dedicated
    // task, so the APP event task never waits for key holds or inter-key delays.
    struct KeyboardSequenceRequest
    {
        char sequence[BLE_KEY_SEQUENCE_MAX];
    };

    QueueHandle_t keyboard_sequence_queue = nullptr;

    void copy_sequence(char *destination, size_t capacity, const char *source)
    {
        size_t length = 0;
        while (source[length] != '\0' && length + 1 < capacity)
            ++length;
        memcpy(destination, source, length);
        destination[length] = '\0';
    }

    // Executes one parsed action. Returning false aborts the rest of the sequence,
    // which prevents a disconnected BLE keyboard from replaying stale key events.
    bool execute_key_action(const BLEKeyAction &action, void *context)
    {
        (void)context;
        if (!ble_keyboard_tap_combo(action.modifiers, action.keycode,
                                    action.hold_ms))
        {
            return false;
        }

        if (action.delay_ms > 0)
            vTaskDelay(pdMS_TO_TICKS(action.delay_ms));
        return true;
    }

    void keyboard_sequence_task(void *parameter)
    {
        (void)parameter;
        KeyboardSequenceRequest request;

        while (true)
        {
            if (!xQueueReceive(keyboard_sequence_queue, &request, portMAX_DELAY))
                continue;

            // Clear a possible key left held by an interrupted previous sequence.
            ble_keyboard_release_all();
            bool completed = BLEKeySequence_parse(
                request.sequence, execute_key_action, nullptr);
            ble_keyboard_release_all();

            if (!completed)
                DualSerial.printf("[Keyboard] Sequence aborted: %s\n",
                                  request.sequence);
        }
    }

} // namespace

void APP_ble_keyboard_init(void)
{
    if (keyboard_sequence_queue != nullptr)
        return;

    keyboard_sequence_queue = xQueueCreate(
        kKeySequenceQueueLength, sizeof(KeyboardSequenceRequest));
    if (keyboard_sequence_queue == nullptr)
    {
        DualSerial.println("[Keyboard] Failed to create sequence queue");
        return;
    }

    xTaskCreatePinnedToCore(
        keyboard_sequence_task,
        "ble_kbd_task",
        4096,
        nullptr,
        1,
        nullptr,
        0);
}

bool APP_ble_keyboard_validate(const char *parameter)
{
    return BLEKeySequence_parse(parameter, nullptr, nullptr);
}

bool APP_ble_keyboard_execute(const char *parameter)
{
    if (parameter == nullptr || parameter[0] == '\0')
        return false;
    if (!APP_ble_keyboard_validate(parameter))
        return false;
    if (!ble_keyboard_is_ready())
    {
        DualSerial.println("[Keyboard] BLE keyboard is not connected");
        return false;
    }
    if (keyboard_sequence_queue == nullptr)
    {
        DualSerial.println("[Keyboard] Sequence task is not initialized");
        return false;
    }

    size_t length = strlen(parameter);
    if (length >= BLE_KEY_SEQUENCE_MAX)
    {
        DualSerial.println("[Keyboard] Sequence is too long");
        return false;
    }

    KeyboardSequenceRequest request = {};
    copy_sequence(request.sequence, sizeof(request.sequence), parameter);
    if (xQueueSend(keyboard_sequence_queue, &request, 0) != pdPASS)
    {
        DualSerial.println("[Keyboard] Sequence queue is full");
        return false;
    }

    HAL::ws2812_start_fx(new AnimFlow(CRGB::White));
    return true;
}

// ============================================================================
// 鼠标
// ============================================================================

namespace
{

    // Maps angular velocity in rad/s to mouse pixels.
    constexpr float kMouseSensitivity = 20.0f;
    constexpr float kMouseDeadzone = 0.05f;

    // Mouse mode receives angular velocity through IMU mux 3.
    bool mouse_mode = false;

} // namespace

void APP_ble_mouse_mode_enter(void)
{
    if (mouse_mode)
        return;

    mouse_mode = true;
    APP_Lumos_on(CRGB::Blue); // Blue indicates mouse mode.
    DualSerial.println("[APP] Mouse mode enabled");
    EventBus::publish(EVENT_IMU_SET_MUX, 3);
}

void APP_ble_mouse_mode_exit(void)
{
    if (!mouse_mode)
        return;

    mouse_mode = false;
    APP_Lumos_off();
    DualSerial.println("[APP] Mouse mode disabled");
    EventBus::publish(EVENT_IMU_SET_MUX, 2);
}

bool APP_ble_mouse_in_mode(void)
{
    return mouse_mode;
}

bool APP_ble_mouse_execute(const char *parameter)
{
    (void)parameter; // Mouse mode has no gesture-mapping parameter.
    APP_ble_mouse_mode_enter();
    return true;
}

void APP_ble_mouse_move(float gx_rad, float gz_rad)
{
    if (fabsf(gx_rad) < kMouseDeadzone)
        gx_rad = 0.0f;
    if (fabsf(gz_rad) < kMouseDeadzone)
        gz_rad = 0.0f;

    int8_t dx = (int8_t)constrain(
        (int32_t)(gx_rad * kMouseSensitivity), -127L, 127L);
    int8_t dy = (int8_t)constrain(
        (int32_t)(-gz_rad * kMouseSensitivity), -127L, 127L);

    // Avoid sending empty mouse reports while the wand is nearly stationary.
    if (dx == 0 && dy == 0)
        return;

    ble_mouse_move(dx, dy);
}

// ============================================================================
// 音量旋钮
// ============================================================================

namespace
{

    constexpr float kVolumeEmaAlpha = 0.2f;
    constexpr float kVolumeStepAngle = 1.5f;
    constexpr float kVolumeDeadzone = 0.0001f;
    constexpr uint32_t kVolumeSendIntervalMs = 100;

    // Prevents oscillation around a step threshold when rotation changes direction.
    constexpr float kVolumeHysteresis = 0.0003f;

    // Volume mode receives corrected absolute angle through IMU mux 4.
    bool volume_mode = false;

} // namespace

void APP_ble_volume_mode_enter(void)
{
    if (volume_mode)
        return;

    volume_mode = true;
    APP_Lumos_on(CRGB::Red); // Red indicates volume mode.
    DualSerial.println("[APP] Volume mode enabled");
    EventBus::publish(EVENT_IMU_SET_MUX, 4);
}

void APP_ble_volume_mode_exit(void)
{
    if (!volume_mode)
        return;

    volume_mode = false;
    APP_Lumos_off();
    DualSerial.println("[APP] Volume mode disabled");
    EventBus::publish(EVENT_IMU_SET_MUX, 2);
}

bool APP_ble_volume_in_mode(void)
{
    return volume_mode;
}

bool APP_ble_knob_execute(const char *parameter)
{
    (void)parameter; // Volume knob mode has no gesture-mapping parameter.
    APP_ble_volume_mode_enter();
    return true;
}

void APP_ble_volume_knob(float angle_deg)
{
    // State is shared across 100 Hz calls to convert incremental rotation into
    // discrete media-volume reports.
    static float last_raw_angle = 0.0f;
    static bool initialized = false;
    static float smooth_delta = 0.0f;
    static float accumulator = 0.0f;
    static uint32_t last_send_ms = 0;

    uint32_t now_ms = millis();
    if (!initialized)
    {
        last_raw_angle = angle_deg;
        smooth_delta = 0.0f;
        accumulator = 0.0f;
        last_send_ms = now_ms;
        initialized = true;
        return;
    }

    // Unwrap the circular angle before filtering. Calculating this delta from
    // the absolute angle avoids a false 358-degree jump across +/-180 degrees.
    float raw_delta = angle_deg - last_raw_angle;
    if (raw_delta > 180.0f)
        raw_delta -= 360.0f;
    else if (raw_delta < -180.0f)
        raw_delta += 360.0f;
    last_raw_angle = angle_deg;

    // Reject physically impossible jumps caused by transient sensor failures.
    if (raw_delta > 30.0f || raw_delta < -30.0f)
        return;

    smooth_delta = kVolumeEmaAlpha * raw_delta +
                   (1.0f - kVolumeEmaAlpha) * smooth_delta;

    if (fabsf(smooth_delta) < kVolumeDeadzone)
        smooth_delta = 0.0f;

    accumulator += smooth_delta;
    if ((now_ms - last_send_ms) < kVolumeSendIntervalMs)
        return;

    bool sent = false;
    while (accumulator >= kVolumeStepAngle)
    {
        ble_consumer_send(MEDIA_VOLUME_UP);
        accumulator -= kVolumeStepAngle;
        sent = true;
    }

    while (accumulator <= -kVolumeStepAngle)
    {
        ble_consumer_send(MEDIA_VOLUME_DOWN);
        accumulator += kVolumeStepAngle;
        sent = true;
    }

    if (sent)
    {
        last_send_ms = now_ms;
        if (accumulator > -kVolumeHysteresis &&
            accumulator < kVolumeHysteresis)
        {
            accumulator = 0.0f;
        }
    }
}
