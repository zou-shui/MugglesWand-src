#pragma once

void inference_init();
void inference_start();
void inference_stop();

// 返回模型实际输出的手势类别数。
int gesture_inference_num_classes();