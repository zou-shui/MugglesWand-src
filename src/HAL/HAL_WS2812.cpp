#include "HAL.h"

#include <FastLED.h>
#include <semphr.h>
#include <cmath>

// ==================== 配置宏 ====================

#define ANIMATION_INTERVAL_MS 10 // 基础刷新间隔

// ==================== 数据结构 ====================

typedef enum
{
    ANIM_NONE = 0,
    ANIM_FLOW,
    ANIM_LAST_ON,
    ANIM_BREATHE,
    ANIM_CHARGE,
} anim_type_t;

typedef struct
{
    anim_type_t type;
    uint32_t color;
    uint8_t speed_factor;
    uint8_t tail_length;
    uint16_t param1;            // 通用参数1
    int8_t param2;              // 通用参数2
    uint8_t battery_percentage; // 充电动画的电量百分比
} anim_state_t;

// ==================== 静态变量 ====================

static CRGB leds[WS2812_LED_COUNT];
static TaskHandle_t ws2812_task_handle = NULL;
static SemaphoreHandle_t anim_semaphore = NULL;
static anim_state_t current_anim = {ANIM_NONE, 0, 0, 0, 0, 0, 0};

// ==================== 内部函数 ====================

// 流水灯动画一帧渲染
static void render_flow_frame(void)
{
    // 恒定速度
    int base_step = current_anim.speed_factor;
    if (base_step < 1)
        base_step = 1;

    // 更新位置
    current_anim.param1 += base_step;

    // 计算当前LED位置
    int current_pos = current_anim.param1 / 10; // 除法获得实际LED索引
    if (current_pos >= WS2812_LED_COUNT + current_anim.tail_length)
    {
        // 动画完成
        current_anim.type = ANIM_NONE; // 退出动画
        return;
    }

    // 清屏
    fill_solid(leds, WS2812_LED_COUNT, CRGB::Black);

    // 提取RGB分量
    uint8_t r = (current_anim.color >> 16) & 0xFF;
    uint8_t g = (current_anim.color >> 8) & 0xFF;
    uint8_t b = current_anim.color & 0xFF;

    // 绘制拖尾效果
    int tail = current_anim.tail_length;
    for (int i = 0; i <= tail && i <= current_pos; i++)
    {
        int led_index = current_pos - i;
        if (led_index >= 0 && led_index < WS2812_LED_COUNT)
        {
            // 计算亮度衰减：越往后越暗
            float brightness;
            if (tail > 1)
            {
                // 使用平方衰减获得更自然的拖尾
                brightness = 1.0f - ((float)i / tail);
                brightness = brightness * brightness;
            }
            else
            {
                brightness = (i == 0) ? 1.0f : 0.3f;
            }

            // 应用亮度
            leds[led_index].r = (uint8_t)(r * brightness);
            leds[led_index].g = (uint8_t)(g * brightness);
            leds[led_index].b = (uint8_t)(b * brightness);
        }
    }

    FastLED.show();
}

// 最后一个灯常亮
static void render_last_led(void)
{
    fill_solid(leds, WS2812_LED_COUNT, CRGB::Black);

    uint8_t r = (current_anim.color >> 16) & 0xFF;
    uint8_t g = (current_anim.color >> 8) & 0xFF;
    uint8_t b = current_anim.color & 0xFF;

    leds[WS2812_LED_COUNT - 1] = CRGB(r, g, b);

    FastLED.show();
}

// 呼吸灯动画一帧渲染
static void render_breathe_frame(void)
{
    static uint32_t breathe_start_time = 0;

    if (breathe_start_time == 0)
    {
        breathe_start_time = millis();
    }

    uint32_t elapsed = millis() - breathe_start_time;
    float phase = (float)elapsed / current_anim.param1 * 2 * M_PI;
    float brightness = (sin(phase) + 1.0f) / 2.0f;

    fill_solid(leds, WS2812_LED_COUNT, CRGB::Black);

    uint8_t r = (current_anim.color >> 16) & 0xFF;
    uint8_t g = (current_anim.color >> 8) & 0xFF;
    uint8_t b = current_anim.color & 0xFF;

    leds[0].r = (uint8_t)(r * brightness);
    leds[0].g = (uint8_t)(g * brightness);
    leds[0].b = (uint8_t)(b * brightness);

    FastLED.show();
}

// 充电动画一帧渲染
static void render_charge_frame(void)
{
    // 计算当前电量位置
    int current_pos = (current_anim.battery_percentage * WS2812_LED_COUNT) / 100;
    if (current_pos > WS2812_LED_COUNT - 1)
        current_pos = WS2812_LED_COUNT - 1;

    // 计算颜色渐变：从红色到绿色
    uint8_t r = (uint8_t)(255 * (100 - current_anim.battery_percentage) / 100.0f);
    uint8_t g = (uint8_t)(255 * current_anim.battery_percentage / 100.0f);
    uint8_t b = 0;

    // 恒定速度
    int base_step = current_anim.speed_factor;
    if (base_step < 1)
        base_step = 1;

    // 更新流动位置
    current_anim.param1 += base_step;

    // 计算流动LED位置
    int flow_pos = current_anim.param1 / 10;

    // 拖尾长度使用 current_pos（确保拖尾完全从 current_pos 消失后才重置）
    int tail = current_pos;
    if (flow_pos >= current_pos + tail)
    {
        // 拖尾完全消失，重置流动位置
        current_anim.param1 = 0;
        flow_pos = 0;
    }

    // 清屏
    fill_solid(leds, WS2812_LED_COUNT, CRGB::Black);

    // 绘制流动拖尾效果（只在0到current_pos范围内）
    for (int i = 0; i <= tail; i++)
    {
        int led_index = flow_pos - i;
        if (led_index >= 0 && led_index <= current_pos)
        {
            // 计算亮度衰减
            float brightness;
            if (tail > 1)
            {
                brightness = 1.0f - ((float)i / tail);
                brightness = brightness * brightness;
            }
            else
            {
                brightness = (i == 0) ? 1.0f : 0.3f;
            }

            // 应用亮度
            leds[led_index].r = (uint8_t)(r * brightness);
            leds[led_index].g = (uint8_t)(g * brightness);
            leds[led_index].b = (uint8_t)(b * brightness);
        }
    }
    leds[current_pos] = CRGB(r, g, b); // 确保当前电量位置的灯珠常亮

    FastLED.show();
}

// 主任务循环
static void ws2812_task(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        // 等待动画触发信号，无限等待
        if (xSemaphoreTake(anim_semaphore, portMAX_DELAY) == pdTRUE)
        {
            // 执行动画直到完成
            while (current_anim.type != ANIM_NONE)
            {
                uint32_t last_update = millis();

                switch (current_anim.type)
                {
                case ANIM_FLOW:
                    render_flow_frame();
                    break;

                case ANIM_LAST_ON:
                    render_last_led();
                    // 常亮模式不自动退出
                    vTaskDelay(pdMS_TO_TICKS(50)); // 降低CPU占用
                    break;

                case ANIM_BREATHE:
                    render_breathe_frame();
                    // 呼吸灯模式不自动退出
                    break;

                case ANIM_CHARGE:
                    render_charge_frame();
                    // 充电动画不自动退出
                    break;

                default:
                    current_anim.type = ANIM_NONE;
                    break;
                }

                // 控制帧率
                uint32_t elapsed = millis() - last_update;
                if (elapsed < ANIMATION_INTERVAL_MS)
                {
                    vTaskDelay(pdMS_TO_TICKS(ANIMATION_INTERVAL_MS - elapsed));
                }
            }

            if (current_anim.type != ANIM_LAST_ON)
            {
                fill_solid(leds, WS2812_LED_COUNT, CRGB::Black);
                FastLED.show();
            }
        }
    }
}

// ==================== 接口实现 ====================
void HAL::ws2812_trigger_breathe(uint32_t color, uint16_t period_ms)
{
    if (anim_semaphore == NULL)
        return;
    if (color == 0)
        color = 0xFFFFFF; // 默认白色
    if (period_ms == 0)
        period_ms = 3000; // 默认3秒

    // 填充动画参数
    current_anim.type = ANIM_BREATHE;
    current_anim.color = color;
    current_anim.param1 = period_ms; // 呼吸周期

    FastLED.setBrightness(50);
    // 发送信号通知任务执行动画
    xSemaphoreGive(anim_semaphore);
}

void HAL::ws2812_trigger_flow(uint32_t color, uint8_t speed_factor, uint8_t tail_length)
{
    if (anim_semaphore == NULL)
        return;
    if (speed_factor == 0)
        speed_factor = 17;
    if (speed_factor > 50)
        speed_factor = 50;
    if (tail_length == 0)
        tail_length = 20;
    if (tail_length > 50)
        tail_length = 50;

    // 填充动画参数
    current_anim.type = ANIM_FLOW;
    current_anim.color = color;
    current_anim.speed_factor = speed_factor;
    current_anim.tail_length = tail_length;
    current_anim.param1 = 0; // 位置归零

    FastLED.setBrightness(255);
    // 发送信号通知任务执行动画
    xSemaphoreGive(anim_semaphore);
}

void HAL::ws2812_toggle_last_led(uint32_t color)
{
    if (anim_semaphore == NULL)
        return;
    if (color == 0)
        color = 0xFFFFFF; // 默认白色

    // 如果已经处于最后灯常亮 → 关闭
    if (current_anim.type == ANIM_LAST_ON)
    {
        current_anim.type = ANIM_NONE;

        fill_solid(leds, WS2812_LED_COUNT, CRGB::Black);
        FastLED.show();
        return;
    }

    // 否则开启最后一个灯常亮
    current_anim.type = ANIM_LAST_ON;
    current_anim.color = color;

    FastLED.setBrightness(255);
    // 唤醒任务
    xSemaphoreGive(anim_semaphore);
}

void HAL::ws2812_trigger_charge(uint8_t battery_percentage)
{
    if (anim_semaphore == NULL)
        return;
    if (battery_percentage > 100)
        battery_percentage = 100;

    // 填充动画参数
    current_anim.type = ANIM_CHARGE;
    current_anim.battery_percentage = battery_percentage;
    current_anim.speed_factor = 4; // 默认速度
    current_anim.param1 = 0;       // 流动位置归零

    FastLED.setBrightness(50);

    // 发送信号通知任务执行动画
    xSemaphoreGive(anim_semaphore);
}

void HAL::ws2812_stop()
{
    FastLED.clear();
    FastLED.show();
    if (ws2812_task_handle != NULL)
    {
        vTaskDelete(ws2812_task_handle);
        ws2812_task_handle = NULL;
    }
}

void HAL::ws2812_init(void)
{
    // 初始化 FastLED
    FastLED.addLeds<WS2812, PIN_WS2812, GRB>(leds, WS2812_LED_COUNT);
    FastLED.setBrightness(50);
    FastLED.clear();
    FastLED.show();

    // 创建二值信号量（用于任务同步）
    anim_semaphore = xSemaphoreCreateBinary();
    if (anim_semaphore == NULL)
    {
        // 创建失败处理
        return;
    }

    // 创建 FreeRTOS 任务
    xTaskCreatePinnedToCore(
        ws2812_task,
        "WS2812_Task",
        4096,
        NULL,
        2,
        &ws2812_task_handle,
        1);

    leds[0] = CRGB::Green;
    FastLED.show(); // 由于其后紧接电源保持，故该灯亮起约等于已开机，提示用户可以松开按键了
}