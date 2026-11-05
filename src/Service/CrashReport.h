#pragma once

/*
    调试功能：`debug crash`。

    读取 flash 中 ESP-IDF 保存的上一次 panic core dump，打印一份适合直接
    复制给 AI / 人工分析的崩溃报告。用法故意不写进 console help。
*/
void crash_report_print();
