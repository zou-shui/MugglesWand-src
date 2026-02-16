#pragma once

constexpr int kNumClasses = 5; // 输出类别数

// 归一化参数（需要与你的训练数据一致）
constexpr float kMean[2] = {2.86185933f, 0.01604925f}; // 替换为实际的 mean 值
constexpr float kStd[2] = {2.96900795f, 0.16098918f};  // 替换为实际的 std 值

void inference_init();