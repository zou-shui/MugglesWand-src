# Muggles' Wand - src

此仓库是[Muggles' Wand](https://github.com/zou-shui/MugglesWand)项目的**固件源码**子仓库。硬件平台：ESP32-S3（Arduino 框架）。构建系统：PlatformIO。如要自行修改源码，请先在VSCode安装插件PlatformIO。PlatformIO会自动识别工程并下载依赖库，随后即可编译工程。

## 工程简介

源码按 `src/` 目录分为四层：

- **HAL** — 硬件抽象层：WS2812 灯带、ICM42670P 六轴 IMU、MAX17048 电池电量计、电源与按键管理
- **Model** — TensorFlow Lite 手势识别：缓存IMU数据，在手势有效时对数据进行推理并发布推理结果。
- **Service** — 系统服务：EventBus 事件总线、Console 串口指令、BLE（NimBLE）、AP 热点 + Web 管理页、OTA 升级、低功耗睡眠
- **APP** — 用户应用层：将手势映射为具体功能，如 Lumos、BLE HID（键盘/鼠标/音量）、ESP-NOW 信号联动、POV 光绘、Home Assistant MQTT。可自行编写更多APP模块实现更多创意功能。

各层通过 EventBus 解耦：HAL 上报 IMU/按键事件，Model 发布手势推理结果，APP 监听并执行相应动作。

> 另外，在Config.h中可以修改AP、BLE名称。

## WiFi 与 Home Assistant

魔杖支持两种网络接入方式，由 DeviceConfig 决定：

| 配置 | 开机行为 |
|---|---|
| 未设置 WiFi SSID | 打开 AP 热点，Web UI 在 `http://192.168.4.1/` |
| 设置了 WiFi SSID | 连接局域网 STA，不再默认开 AP；Web UI 在 `http://<设备IP>/` 或 `http://<hostname>.local/` |

### 配置

在串口、TCP 控制台或 Web 终端中执行（重启生效）：

```
config wifi <ssid>
config wifi-pass <password>
config mqtt <broker-host>
config mqtt-port 1883
config mqtt-user <user>
config mqtt-pass <password>
config list
config clear wifi|mqtt
config reset
ha status
```

`ha status` 会打印 WiFi IP/信道/RSSI、MQTT 连接状态、主题前缀和 HA 绑定数量，便于排查。

> MQTT 需要一个 broker（HA 的 Mosquitto 加载项即可）。魔杖和 HA 都连接到同一个 broker，不直接互连。broker 地址建议填写 IP 或局域网 DNS 名称，`.local` 域名不保证能被解析。

### 按键恢复

STA 模式开机后不开 AP。如果 WiFi 配置错误导致无法连接，**连续短按两次按键**会切换 AP 开/关：

- 打开 AP 后连接魔杖热点，访问 `http://192.168.4.1/` 修改配置；
- 再次双击关闭 AP，STA 连接不受影响。

### 手势绑定

`map bind` 的事件名必填，只允许小写字母、数字、下划线，1~31 个字符。和现有 map 一样，一个手势只能绑定一个 APP，绑到 `homeassistant` 会替换它原来的动作：

```
map bind 3 homeassistant swipe_up
map bind 4 homeassistant circle
map list
```

手势 3 触发时，魔杖发布：

- `<base>/event`：`{"event_type":"swipe_up"}`（HA Event 实体）
- `<base>/gesture`：`swipe_up`（HA 设备触发器）

同一个事件名不要绑定到多个手势，否则 HA 侧无法区分。调试时可以用 `gesture 3` 模拟触发手势 3。

### HA 自动发现

配置 MQTT 并连上 broker 后，HA 会自动创建：

- 设备 `Zoushui's Wand`（名称来自 BLE 名称）
- `Gesture` Event 实体（列出当前绑定的手势事件）
- 每个手势一个设备触发器，自动化里可直接选择
- `Battery` 电量传感器和 `Charging` 充电状态
- 设备卡片的 "Visit" 链接直达魔杖 Web UI

发现主题前缀固定为 `homeassistant`，设备主题为 `muggleswand/<MAC后6位>`。手势事件不保留，发现配置和电量保留。Event 实体需要 HA 2023.8 及以上；更老的 HA 仍可使用设备触发器。

在 HA 自动化中：添加触发器 → 设备 → 选择魔杖 → 选择手势事件（如 `swipe_up`），再选择要控制的设备或动作即可。

### ESP-NOW 信道限制

ESP-NOW 依赖 WiFi 信道 1：

- STA 连接的路由器在信道 1：ESP-NOW 与 WiFi 共存；
- 路由器在其它信道：ESP-NOW 自动关闭，`espnow` 命令会拒绝启动；
- 需要 ESP-NOW 时，把路由器 2.4GHz 信道固定为 1，或使用 AP 模式。

### 注意

- WiFi/MQTT 配置修改后需要 `reboot` 生效。
- ESP32-S3 只支持 2.4GHz WiFi，请连接路由器的 2.4GHz 频段。
- WiFi + MQTT 常连会增加功耗；固件保持 modem sleep，并按 30 秒 keepalive 与 broker 通信。
- 局域网 Web 控制台没有鉴权，能执行 `reboot`、`shutdown`、`config`，请只在可信网络使用。
- MQTT 凭据与 AP 密码一样明文保存在 NVS。
- 当前 MQTT 为明文 1883，不支持 TLS。

## OTA

固件支持以OTA方式更新，在Console中发送ota指令使其进入OTA模式，连接魔杖热点并访问[192.168.4.1/update](http://192.168.4.1/update)即可上传固件进行更新。STA 模式下执行 `ota` 会先停止 Web/MQTT/STA，再切回 AP 模式。
编译好的固件在`.pio/build/esp32-s3-fh4r2/firmware.bin`
