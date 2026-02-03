#include "HAL.h"

#include <FastLED.h>
#include <semphr.h>

// ==================== 配置宏 ====================

#define ANIMATION_INTERVAL_MS 1 // 基础刷新间隔

// ==================== 数据结构 ====================

typedef enum
{
    ANIM_NONE = 0,
    ANIM_FLOW,
} anim_type_t;

typedef struct
{
    anim_type_t type;
    uint32_t color;
    uint8_t speed_factor;
    uint8_t tail_length;
    uint16_t param1; // 通用参数1（用于流水灯的位置）
    int8_t param2;   // 通用参数2（用于流水灯的速度）
} anim_state_t;

// ==================== 静态变量 ====================

static CRGB leds[WS2812_LED_COUNT];
static TaskHandle_t ws2812_task_handle = NULL;
static SemaphoreHandle_t anim_semaphore = NULL;
static anim_state_t current_anim = {ANIM_NONE, 0, 0, 0, 0, 0};

// ==================== 内部函数 ====================

// 流水灯动画一帧渲染
static void render_flowing_frame(void)
{
    // 先慢后快的非线性速度曲线
    // 使用指数加速：speed = base_speed + (position * acceleration)
    float progress = (float)current_anim.param1 / (WS2812_LED_COUNT * 3);
    if (progress > 1.0f)
        progress = 1.0f;

    // 非线性速度计算：起始慢，后面指数增长
    float speed_multiplier = 0.3f + (progress * progress * 0.7f);
    int base_step = (current_anim.speed_factor * speed_multiplier);
    if (base_step < 1)
        base_step = 1;

    // 更新位置
    current_anim.param1 += base_step;

    // 计算当前LED位置
    int current_pos = current_anim.param1 / 10; // 除法获得实际LED索引
    if (current_pos >= WS2812_LED_COUNT + current_anim.tail_length)
    {
        // 动画完成
        current_anim.type = ANIM_NONE;
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

    // 偶尔在前面添加一点微光（营造预告感）
    if (current_pos < WS2812_LED_COUNT - 1)
    {
        int preview_pos = current_pos + 1;
        if (preview_pos < WS2812_LED_COUNT)
        {
            leds[preview_pos] = CRGB(
                (uint8_t)(r * 0.05f),
                (uint8_t)(g * 0.05f),
                (uint8_t)(b * 0.05f));
        }
    }

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
                    render_flowing_frame();
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

            // 动画完成后可选：保持最后一帧或自动熄灭
            // 这里选择自动熄灭
            fill_solid(leds, WS2812_LED_COUNT, CRGB::Black);
            FastLED.show();
        }
    }
}

// ==================== 接口实现 ====================

void HAL::ws2812_trigger_flowing(uint32_t color, uint8_t speed_factor, uint8_t tail_length)
{
    if (anim_semaphore == NULL)
        return;
    if (speed_factor == 0)
        speed_factor = 1;
    if (speed_factor > 10)
        speed_factor = 10;
    if (tail_length == 0)
        tail_length = 3;
    if (tail_length > 10)
        tail_length = 10;

    // 填充动画参数
    current_anim.type = ANIM_FLOW;
    current_anim.color = color;
    current_anim.speed_factor = speed_factor;
    current_anim.tail_length = tail_length;
    current_anim.param1 = 0; // 位置归零
    current_anim.param2 = 0; // 速度归零

    // 发送信号通知任务执行动画
    xSemaphoreGive(anim_semaphore);
}

void HAL::ws2812_stop(void)
{
    current_anim.type = ANIM_NONE;

    fill_solid(leds, WS2812_LED_COUNT, CRGB::Black);
    FastLED.show();
}

void HAL::ws2812_set_solid(uint32_t color)
{
    // 停止任何动画
    current_anim.type = ANIM_NONE;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    fill_solid(leds, WS2812_LED_COUNT, CRGB(r, g, b));
    FastLED.show();
}

void HAL::ws2812_init(void)
{
    // 初始化 FastLED
    FastLED.addLeds<WS2812, PIN_WS2812, GRB>(leds, WS2812_LED_COUNT);
    FastLED.setBrightness(255);
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
}