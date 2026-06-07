#include "HAL.h"
#include <Arduino.h>
#include "Config.h"
#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// 引入动画基类
#include "WS2812_Animation/AnimationBase.hpp"

// ==================== 静态硬件资源 ====================
static CRGB leds[WS2812_LED_COUNT];
static TaskHandle_t ws2812_task_handle = NULL;

// ==================== 图层指针 (线程安全队列间接控制) ====================
static AnimationBase *bg_layer = nullptr;      // 背景层（持续性，如充电、普通呼吸）
static AnimationBase *fx_layer = nullptr;      // 特效层（一次性，如魔法光流 Flow）
static AnimationBase *overlay_layer = nullptr; // 顶层覆盖（最高优先级，如手势指示灯）

// ==================== 多任务同步队列 ====================
typedef enum
{
    CMD_SET_BG,
    CMD_START_FX,
    CMD_SET_OVERLAY
} layer_cmd_type_t;

typedef struct
{
    layer_cmd_type_t type;
    AnimationBase *anim_ptr;
} layer_cmd_t;

static QueueHandle_t layer_queue = NULL;

// ==================== 核心渲染任务循环 ====================
static void ws2812_task(void *pvParameters)
{
    (void)pvParameters;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10); // 严格的10ms刷新间隔 (100 FPS)

    for (;;)
    {
        // 1. 检查是否有上层 APP / Service 发来的图层切换命令
        layer_cmd_t cmd;
        while (xQueueReceive(layer_queue, &cmd, 0) == pdTRUE)
        {
            switch (cmd.type)
            {
            case CMD_SET_BG:
                if (bg_layer)
                    delete bg_layer;
                bg_layer = cmd.anim_ptr;
                if (bg_layer)
                    bg_layer->init();
                break;

            case CMD_START_FX:
                if (fx_layer)
                    delete fx_layer;
                fx_layer = cmd.anim_ptr;
                if (fx_layer)
                    fx_layer->init();
                break;

            case CMD_SET_OVERLAY:
                if (overlay_layer)
                    delete overlay_layer;
                overlay_layer = cmd.anim_ptr;
                if (overlay_layer)
                    overlay_layer->init();
                break;
            }
        }

        // 2. 彻底清除硬件缓冲区画布
        fill_solid(leds, WS2812_LED_COUNT, CRGB::Black);

        // 3. 【渲染第一层】 背景层
        if (bg_layer)
        {
            bg_layer->update(leds, WS2812_LED_COUNT);
        }

        // 4. 【渲染第二层】 特效层 (覆盖或混合)
        if (fx_layer)
        {
            fx_layer->update(leds, WS2812_LED_COUNT);
            // 如果是一次性动画（如Flow跑完了），引擎自动销毁释放内存，完美暴露出底色
            if (fx_layer->finished())
            {
                delete fx_layer;
                fx_layer = nullptr;
            }
        }

        // 5. 【渲染第三层】 顶层悬浮（强制覆盖，手势状态灯绝对可见）
        if (overlay_layer)
        {
            overlay_layer->update(leds, WS2812_LED_COUNT);
            if (overlay_layer->finished())
            {
                delete overlay_layer;
                overlay_layer = nullptr;
            }
        }

        // 6. 刷向物理引脚驱动硬件
        FastLED.show();

        // 7. 绝对延时锁定帧率
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ==================== HAL 层开放接口实现 ====================

void HAL::ws2812_init(void)
{
    // 初始化 FastLED 物理外设
    FastLED.addLeds<WS2812, PIN_WS2812, GRB>(leds, WS2812_LED_COUNT);
    FastLED.setBrightness(255); // 默认全亮，交由动画层自己调节亮度
    FastLED.clear();
    FastLED.show();

    // 创建命令通信队列 (深度为8，防突发堆积)
    layer_queue = xQueueCreate(8, sizeof(layer_cmd_t));
    if (layer_queue == NULL)
        return;

    // 创建渲染核心任务
    xTaskCreatePinnedToCore(
        ws2812_task,
        "WS2812_Engine_Task",
        4096,
        NULL,
        1,
        &ws2812_task_handle,
        1);
}

// 停止引擎并清理内存
void HAL::ws2812_stop(void)
{
    if (ws2812_task_handle != NULL)
    {
        vTaskDelete(ws2812_task_handle);
        ws2812_task_handle = NULL;
    }

    // 清理三大图层残余内存
    if (bg_layer)
    {
        delete bg_layer;
        bg_layer = nullptr;
    }
    if (fx_layer)
    {
        delete fx_layer;
        fx_layer = nullptr;
    }
    if (overlay_layer)
    {
        delete overlay_layer;
        overlay_layer = nullptr;
    }

    if (layer_queue)
    {
        vQueueDelete(layer_queue);
        layer_queue = NULL;
    }

    FastLED.clear();
    FastLED.show();
}

// ----------------- 图层异步推送 API -----------------

// 设置环境底色背景层
void HAL::ws2812_set_background(AnimationBase *anim)
{
    if (layer_queue == NULL)
    {
        delete anim;
        return;
    }
    layer_cmd_t cmd = {CMD_SET_BG, anim};
    if (xQueueSend(layer_queue, &cmd, 0) != pdTRUE)
    {
        delete anim; // 队列满安全自毁
    }
}

// 突发插播单次魔法特效应调用此接口
void HAL::ws2812_start_fx(AnimationBase *anim)
{
    if (layer_queue == NULL)
    {
        delete anim;
        return;
    }
    layer_cmd_t cmd = {CMD_START_FX, anim};
    if (xQueueSend(layer_queue, &cmd, 0) != pdTRUE)
    {
        delete anim;
    }
}

// 设置不可被覆盖的手势状态顶层
void HAL::ws2812_set_overlay(AnimationBase *anim)
{
    if (layer_queue == NULL)
    {
        delete anim;
        return;
    }
    layer_cmd_t cmd = {CMD_SET_OVERLAY, anim};
    if (xQueueSend(layer_queue, &cmd, 0) != pdTRUE)
    {
        delete anim;
    }
}