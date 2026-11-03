#pragma once

#include <stddef.h>

void console_init();

// 将外部命令排入 Console 任务执行，避免多个网络回调并发解析命令
bool console_submit(const char *command, size_t length);
