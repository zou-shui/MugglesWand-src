/*
    `debug crash` 的实现：打印上一次 panic 的 core dump 报告。

    背景：
      ESP-IDF 在 panic 时会把完整 core dump 写进 flash 的 coredump 分区
      （0x3F0000，64KB），重启后数据仍在。本板没有引出 GPIO43/44，UART0 的
      panic 文本看不到，所以用这个命令把 core dump 读回来。

    DEBUGCAUSE：
      core dump 本身不保存 Xtensa 的 DEBUGCAUSE，但 "Unhandled debug exception"
      的具体原因（stack canary / 断点 / 单步 / 调试中断）就编码在里面。
      因此用 -Wl,--wrap=panicHandler 在 panic 入口把 DEBUGCAUSE 和异常帧
      关键字段写进 RTC_NOINIT 内存，重启后一并打印。
      panic 时 cache 可能已关闭，__wrap_panicHandler 必须在 IRAM 中执行，
      且只能读寄存器和写 RTC 内存，不能调用 flash 中的函数。
*/
#include <Arduino.h>

#include "CrashReport.h"
#include "DualPrint.h"

#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/task_snapshot.h>

#include <string.h>

/************ panic 现场补充记录（RTC_NOINIT） ************/

#define PANIC_RECORD_MAGIC 0x504E4352u // "PNCR"

struct PanicRecord
{
    uint32_t magic;
    uint32_t debug_cause;     // Xtensa DEBUGCAUSE；仅 exception_cause == 1 时有意义
    uint32_t exception_cause; // 异常帧里的 EXCCAUSE（pseudo reason，1~7）
    uint32_t pc;
    uint32_t excvaddr;
    uint32_t sp;
    uint32_t core;
    uint32_t checksum;
};

// RTC_NOINIT 变量不能带初始值；冷启动内容是随机的，用 magic + checksum 校验
RTC_NOINIT_ATTR static PanicRecord s_panic_record;

static bool panic_record_get(PanicRecord *out)
{
    if (out == nullptr || s_panic_record.magic != PANIC_RECORD_MAGIC)
        return false;

    uint32_t checksum = s_panic_record.magic ^ s_panic_record.debug_cause ^
                        s_panic_record.exception_cause ^ s_panic_record.pc ^
                        s_panic_record.excvaddr ^ s_panic_record.sp ^
                        s_panic_record.core;
    if (checksum != s_panic_record.checksum)
        return false;

    *out = s_panic_record;
    return true;
}

// 真正的 panicHandler 在 IDF 的 libesp_system 里，永不返回
extern "C" void __real_panicHandler(void *frame) __attribute__((noreturn));

extern "C" void IRAM_ATTR __attribute__((noreturn)) __wrap_panicHandler(void *frame)
{
    // XtExcFrame 固定布局：pc=1, a1=4, exccause=20, excvaddr=21
    volatile uint32_t *exc_frame = (volatile uint32_t *)frame;

    uint32_t debug_cause = 0;
    uint32_t core = 0;
    __asm__ __volatile__("rsr.debugcause %0" : "=r"(debug_cause));
    __asm__ __volatile__("rsr.prid %0" : "=r"(core));

    uint32_t cause = exc_frame[20];
    uint32_t pc = exc_frame[1];
    uint32_t excvaddr = exc_frame[21];
    uint32_t sp = exc_frame[4];
    core = (core >> 13) & 1;

    s_panic_record.magic = PANIC_RECORD_MAGIC;
    s_panic_record.debug_cause = debug_cause;
    s_panic_record.exception_cause = cause;
    s_panic_record.pc = pc;
    s_panic_record.excvaddr = excvaddr;
    s_panic_record.sp = sp;
    s_panic_record.core = core;
    s_panic_record.checksum =
        PANIC_RECORD_MAGIC ^ debug_cause ^ cause ^ pc ^ excvaddr ^ sp ^ core;

    __real_panicHandler(frame);
}

/************ core dump 解码 ************/

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF

// Xtensa 普通 EXCCAUSE（0x00~0x27），与 IDF panic_arch.c 的 reason 表一致
static const char *const k_exc_cause_names[] = {
    "IllegalInstruction", "Syscall", "InstructionFetchError", "LoadStoreError",
    "Level1Interrupt", "Alloca", "IntegerDivideByZero", "PCValue",
    "Privileged", "LoadStoreAlignment", "res", "res",
    "InstrPDAddrError", "LoadStorePIFDataError", "InstrPIFAddrError", "LoadStorePIFAddrError",
    "InstTLBMiss", "InstTLBMultiHit", "InstFetchPrivilege", "res",
    "InstrFetchProhibited", "res", "res", "res",
    "LoadStoreTLBMiss", "LoadStoreTLBMultihit", "LoadStorePrivilege", "res",
    "LoadProhibited", "StoreProhibited", "res", "res",
    "Cp0Dis", "Cp1Dis", "Cp2Dis", "Cp3Dis",
    "Cp4Dis", "Cp5Dis", "Cp6Dis", "Cp7Dis"};

// IDF panic_soc_fill_info() 的 pseudo_reason 表（panicHandler 路径使用）
static const char *const k_pseudo_cause_names[] = {
    "Unknown reason",
    "Unhandled debug exception",
    "Double exception",
    "Unhandled kernel exception",
    "Coprocessor exception",
    "Interrupt wdt timeout on CPU0",
    "Interrupt wdt timeout on CPU1",
    "Cache disabled but cached memory region accessed"};

// IDF core dump 对 pseudo cause 加了 XCHAL_EXCCAUSE_NUM(0x40)，
// 所以 core dump 里的 0x41 实际是 0x40 + 1 = PANIC_RSN_DEBUGEXCEPTION。
static const char *exception_cause_name(uint32_t cause, bool *pseudo, uint32_t *raw_cause)
{
    if (cause >= 0x40)
    {
        *pseudo = true;
        *raw_cause = cause - 0x40;
        if (*raw_cause < sizeof(k_pseudo_cause_names) / sizeof(k_pseudo_cause_names[0]))
            return k_pseudo_cause_names[*raw_cause];
        return "Unknown pseudo cause";
    }

    *pseudo = false;
    *raw_cause = cause;
    if (cause < sizeof(k_exc_cause_names) / sizeof(k_exc_cause_names[0]))
        return k_exc_cause_names[cause];
    return "Unknown";
}

static void print_debug_cause(uint32_t debug_cause)
{
    DualSerial.printf("[Crash] DEBUGCAUSE   : 0x%08x", debug_cause);

    bool has_reason = false;
    if (debug_cause & 0x01)
    {
        DualSerial.print(" SingleStep");
        has_reason = true;
    }
    if (debug_cause & 0x02)
    {
        DualSerial.print(" HwBreakpoint");
        has_reason = true;
    }
    if (debug_cause & 0x04) // DBREAK；bit8 区分 watchpoint 1（stack canary）
    {
        DualSerial.print((debug_cause & (1u << 8))
                             ? " StackCanaryWatchpoint(watchpoint1)"
                             : " DataWatchpoint(watchpoint0)");
        has_reason = true;
    }
    if (debug_cause & 0x08)
    {
        DualSerial.print(" BreakInstruction");
        has_reason = true;
    }
    if (debug_cause & 0x10)
    {
        DualSerial.print(" BreakNInstruction");
        has_reason = true;
    }
    if (debug_cause & 0x20)
    {
        DualSerial.print(" DebugInterrupt");
        has_reason = true;
    }

    if (!has_reason)
        DualSerial.print(" (no debug cause bit set)");
    DualSerial.println();
}

void crash_report_print()
{
    esp_core_dump_summary_t summary;
    memset(&summary, 0, sizeof(summary));

    if (esp_core_dump_image_check() != ESP_OK)
    {
        DualSerial.println("[Crash] No valid crash record found in flash");
        return;
    }

    if (esp_core_dump_get_summary(&summary) != ESP_OK)
    {
        DualSerial.println("[Crash] Crash record exists but summary cannot be parsed");
        return;
    }

    // exc_task 是定长数组，补结束符后再打印
    char task_name[sizeof(summary.exc_task) + 1];
    memcpy(task_name, summary.exc_task, sizeof(summary.exc_task));
    task_name[sizeof(summary.exc_task)] = '\0';

    bool pseudo = false;
    uint32_t raw_cause = 0;
    const char *cause_name =
        exception_cause_name(summary.ex_info.exc_cause, &pseudo, &raw_cause);

    char current_sha[APP_ELF_SHA256_SZ] = {0};
    esp_ota_get_app_elf_sha256(current_sha, sizeof(current_sha));
    const char *dump_sha = (const char *)summary.app_elf_sha256;

    DualSerial.println("---------------- Crash Report ----------------");
    DualSerial.printf("[Crash] Faulting task : %s\n", task_name);
    if (pseudo)
        DualSerial.printf("[Crash] Exception     : %s (raw EXCCAUSE 0x%02x, core dump value 0x%02x)\n",
                          cause_name, (unsigned)raw_cause, (unsigned)summary.ex_info.exc_cause);
    else
        DualSerial.printf("[Crash] Exception     : %s (EXCCAUSE 0x%02x)\n",
                          cause_name, (unsigned)summary.ex_info.exc_cause);
    DualSerial.printf("[Crash] Fault address : 0x%08x\n", (unsigned)summary.ex_info.exc_vaddr);
    DualSerial.printf("[Crash] PC            : 0x%08x\n", (unsigned)summary.exc_pc);
    DualSerial.printf("[Crash] Dump SHA256   : %s\n", dump_sha);
    DualSerial.printf("[Crash] Current SHA256: %s\n", current_sha);
    DualSerial.printf("[Crash] SHA match     : %s\n",
                      strncmp(current_sha, dump_sha, APP_ELF_SHA256_SZ - 1) == 0 ? "yes" : "NO (symbolization needs the matching ELF)");

    // RTC 中的补充记录：用异常原因判断是否属于同一次 panic
    PanicRecord record;
    bool has_record = panic_record_get(&record);
    bool record_matches = has_record &&
                          record.exception_cause == (summary.ex_info.exc_cause & 0x3F);

    if (record_matches)
    {
        DualSerial.printf("[Crash] Faulting core : %u\n", (unsigned)record.core);
        if (record.exception_cause == 1)
            print_debug_cause(record.debug_cause);

        if (record.pc != summary.exc_pc)
            DualSerial.printf("[Crash] Note: panic frame PC 0x%08x, task frame PC 0x%08x, SP 0x%08x\n",
                              (unsigned)record.pc, (unsigned)summary.exc_pc,
                              (unsigned)record.sp);
    }
    else if (has_record)
    {
        DualSerial.printf("[Crash] RTC record from the most recent panic "
                          "(core=%u pc=0x%08x sp=0x%08x cause=0x%02x):\n",
                          (unsigned)record.core, (unsigned)record.pc,
                          (unsigned)record.sp, (unsigned)record.exception_cause);
        if (record.exception_cause == 1)
            print_debug_cause(record.debug_cause);
        DualSerial.println("[Crash] (it may not correspond to the core dump above)");
    }

    const uint32_t epc_count = sizeof(summary.ex_info.epcx) / sizeof(summary.ex_info.epcx[0]);
    for (uint32_t i = 0; i < epc_count; i++)
    {
        if (summary.ex_info.epcx_reg_bits & (1u << i))
            DualSerial.printf("[Crash] EPC%-2u         : 0x%08x\n",
                              (unsigned)(i + 1), (unsigned)summary.ex_info.epcx[i]);
    }

    DualSerial.print("[Crash] Registers     :");
    for (int i = 0; i < 16; i++)
        DualSerial.printf(" a%d=0x%08x", i, (unsigned)summary.ex_info.exc_a[i]);
    DualSerial.println();

    uint32_t depth = summary.exc_bt_info.depth;
    const uint32_t bt_max = sizeof(summary.exc_bt_info.bt) / sizeof(summary.exc_bt_info.bt[0]);
    if (depth > bt_max)
        depth = bt_max;

    DualSerial.printf("[Crash] Backtrace (%u frames%s):\n",
                      (unsigned)depth, summary.exc_bt_info.corrupted ? ", corrupted" : "");
    for (uint32_t i = 0; i < depth; i++)
        DualSerial.printf("[Crash]   #%-2u 0x%08x\n", (unsigned)i,
                          (unsigned)summary.exc_bt_info.bt[i]);

    // 常见原因提示
    if (pseudo && raw_cause == 1)
    {
        DualSerial.println("[Crash] EXCCAUSE 0x40+1 = IDF pseudo cause 'Unhandled debug exception'.");
        DualSerial.println("[Crash] CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK is enabled in this SDK;");
        DualSerial.println("[Crash] the most common cause is a task stack overflow (stack canary watchpoint).");
    }

    if (strcmp(task_name, "ipc0") == 0 || strcmp(task_name, "ipc1") == 0)
    {
        // 当前构建实际的 IPC 栈大小，用来确认 IPCTaskStackFix 是否生效
        TaskHandle_t ipc_handle = xTaskGetHandle(task_name);
        if (ipc_handle != nullptr)
        {
            TaskSnapshot_t snapshot;
            uint8_t *base = pxTaskGetStackStart(ipc_handle);
            if (vTaskGetSnapshot(ipc_handle, &snapshot) == pdTRUE && snapshot.pxEndOfStack >= base)
            {
                uint32_t total = (uint32_t)((uintptr_t)snapshot.pxEndOfStack - (uintptr_t)base + 1);
                DualSerial.printf("[Crash] Current %s stack: %u B%s\n",
                                  task_name, (unsigned)total,
                                  total > 1024 ? " (IPCTaskStackFix enabled)"
                                               : " (IPCTaskStackFix NOT enabled)");
            }
        }
    }

    DualSerial.println("[Crash] Raw core dump: flash offset 0x3F0000, 64KB");
    DualSerial.println("[Crash] Symbolize: xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf <addr>");
    DualSerial.println("---------------------------------------------");
}

#else // core dump 未启用

void crash_report_print()
{
    DualSerial.println("[Crash] Core dump is disabled in this build");
}

#endif
