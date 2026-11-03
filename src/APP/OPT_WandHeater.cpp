#include "OPT_WandHeater.h"

#if WAND_HEATER_ENABLE // 未开启宏时本文件编译为空，不占用 Flash / RAM

#include "Config.h"
#include "HAL/HAL.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"
#include "HAL/WS2812_Animation/AnimBreathe.hpp"

static QueueHandle_t heat_queue = NULL;
static bool heat_unlocked = false; // 是否已进入 DEBUG 模式（解锁加热功能）
static bool heat_on = false;       // 灯带当前是否处于加热（全亮）状态

// 切换灯带加热：开启时在 background 层挂载"最高亮度常亮"动画，关闭时置空该图层
static void OPT_WandHeater_toggle(void)
{
    if (!heat_on)
    {
        heat_on = true;

        // brightMin == brightMax == 255 把呼吸曲线压平成常量，正弦相位不再影响输出；
        // periodMs 只参与相位计算，必须非 0（为 0 时 AnimBreathe::update 直接返回，一帧都不渲染）
        HAL::ws2812_set_background(new AnimBreathe(CRGB::White, 1000, 255, 255));
        DualSerial.println("[Heater] ON");
    }
    else
    {
        heat_on = false;

        // 置空 background 层：引擎会 delete 掉旧的 AnimBreathe，
        // 若此时再无其它图层在工作，还会自动切断灯带电源
        HAL::ws2812_set_background(nullptr);
        DualSerial.println("[Heater] OFF");
    }
}

static void OPT_WandHeater_task(void *param)
{
    (void)param;
    SystemEvent event;

    while (1)
    {
        if (xQueueReceive(heat_queue, &event, portMAX_DELAY) == pdTRUE)
        {
            switch (event.id)
            {
            case EVENT_SYS_DEBUG:
                // 只要事件总线出现过一次 debug 事件即永久解锁（与系统内其它 DEBUG 模式语义一致）
                heat_unlocked = true;
                DualSerial.println("[Heater] debug mode unlocked (single press toggles the LED heater)");
                break;

            case EVENT_BTN_SHORT_PRESS:
                if (1 == event.param1.i32 && heat_unlocked)
                {
                    OPT_WandHeater_toggle();
                }
                break;

            default:
                break;
            }
        }
    }
}

void OPT_WandHeater_init(void)
{
    heat_queue = xQueueCreate(8, sizeof(SystemEvent));
    if (heat_queue == NULL)
        return;

    EventBus::subscribe(EVENT_SYS_DEBUG, heat_queue);
    EventBus::subscribe(EVENT_BTN_SHORT_PRESS, heat_queue);

    xTaskCreatePinnedToCore(
        OPT_WandHeater_task,
        "wand_heater",
        2048,
        NULL,
        1,
        NULL,
        0);
}

#endif // WAND_HEATER_ENABLE
