# Muggles' Wand - src

此仓库是[Muggles' Wand](https://github.com/zou-shui/MugglesWand)项目的**固件源码**子仓库。硬件平台：ESP32-S3（Arduino 框架）。构建系统：PlatformIO。如要自行修改源码，请先在VSCode安装插件PlatformIO。

由于PlatformIO官方尚未提供 ESP32-S3FH4R2 的板卡定义文件，请手动将 `esp32-s3-fh4r2.json` 放入 PlatformIO 的 boards 目录：

**Windows:**

> C:/Users/<用户名>/.platformio/platforms/espressif32/boards/

**Linux / macOS:**

> ~/.platformio/platforms/espressif32/boards/

之后即可正常打开工程。

## 工程简介

源码按 `src/` 目录分为四层：

- **HAL** — 硬件抽象层：WS2812 灯带、ICM42670P 六轴 IMU、MAX17048 电池电量计、电源管理与按键
- **Model** — TensorFlow Lite 手势识别：缓存IMU数据，在手势有效时对数据进行推理并发布推理结果。
- **Service** — 系统服务：EventBus 事件总线、Console 串口指令、BLE（NimBLE）、AP 热点 + Web 管理页、OTA 升级、低功耗睡眠
- **APP** — 应用层：将手势映射为具体功能，如 Lumos、BLE HID（键盘/鼠标/音量）、ESP-NOW 信号联动、POV 光绘

各层通过 EventBus 解耦：HAL 上报 IMU/按键事件，Model 发布手势推理结果，APP 监听并执行相应动作。

> 另外，在Config.h中可以修改AP、BLE名称。

## OTA

固件支持以OTA方式更新，在Console中发送ota指令使其进入OTA模式，连接魔杖热点并访问[192.168.4.1/update](http://192.168.4.1/update)即可上传固件进行更新。
编译好的固件在`.pio/build/esp32-s3-fh4r2/firmware.bin`


