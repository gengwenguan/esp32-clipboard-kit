# SoftAP 配网与共享剪贴板（ESP32-S3）

## 项目简介

这是一个基于 ESP-IDF 的 ESP32-S3 工程，实现 SoftAP 配网（Captive Portal）、LCD 状态界面、按键 UI、USB HID 键盘自动输入字符串，以及通过 WebSocket 同步的共享剪贴板页面。

## 功能特性

- SoftAP 配网与 DNS 劫持（Captive Portal）
- Web 配网页与配置管理（保存 USB 键盘字符串）
- 共享剪贴板页面与 WebSocket 实时同步
- LCD 显示与三键 UI
- USB HID 键盘模拟输入

## 硬件与环境

- 芯片：ESP32-S3
- LCD：GC9107（SPI）
- ESP-IDF：5.5.2

## 目录结构

```
.
├── CMakeLists.txt
├── main
│   ├── include
│   ├── main.c
│   ├── wifi_prov.c
│   ├── web_server.c
│   ├── dns_server.c
│   ├── ws_server.c
│   ├── clipboard_service.c
│   ├── lcd_display.c
│   ├── ui_manager.c
│   ├── usb_hid.c
│   └── button.c
├── managed_components
├── dependencies.lock
└── sdkconfig
```

## 快速开始

1. 安装并导出 ESP-IDF 环境变量，确保 `idf.py` 可用
2. 进入工程根目录
3. 编译与烧录

```
. $HOME/esp/esp-idf/export.sh

idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## 启动与运行流程

1. 初始化 LCD、UI、按键
2. 初始化 NVS 与网络栈，启动 DNS、HTTP、WebSocket 等服务
3. 若已有保存的 STA 配置则尝试连接路由器，否则启动 SoftAP 配网

## SoftAP 配网

- SSID：ESP32-S3-Prov
- 密码：无（Open）
- 网关：192.168.4.1

连接 SoftAP 后打开任意网页或访问 `http://192.168.4.1/`，将被重定向到配置页。提交 Wi-Fi SSID/密码后设备会尝试连接路由器，成功后关闭 SoftAP，仅保留 STA 模式。

## Web 页面与接口

- `GET /`：配网页
- `POST /connect`：提交 `ssid/password` 并连接 Wi-Fi
- `POST /save_usb`：保存 USB 键盘字符串
- `GET /clipboard`：共享剪贴板页面
- `GET /ws`：WebSocket 同步剪贴板内容

## 共享剪贴板协议

WebSocket 消息采用 JSON：

- `{"type":"get_state"}`：请求当前剪贴板
- `{"type":"update","content":"<base64>"}`：更新剪贴板并广播

## LCD 与按键

三页 UI：

1. Wi-Fi 状态
2. 时钟
3. USB 键盘状态与发送

按键定义：

- KEY1（GPIO11）：确认/执行（USB 页启用或发送）
- KEY2（GPIO0）：上一页
- KEY3（GPIO39）：下一页
- KEY2+KEY3 同按：切换 LCD 反色

## USB HID 键盘

- Web 页保存字符串至 NVS
- LCD USB 页面开启 USB 后可触发发送字符串
- 使用 TinyUSB HID 键盘报告模拟输入

## 关键源码入口

- [main.c](file:///Users/bytedance/esp/softap_prov/main/main.c)
- [wifi_prov.c](file:///Users/bytedance/esp/softap_prov/main/wifi_prov.c)
- [web_server.c](file:///Users/bytedance/esp/softap_prov/main/web_server.c)
- [dns_server.c](file:///Users/bytedance/esp/softap_prov/main/dns_server.c)
- [ws_server.c](file:///Users/bytedance/esp/softap_prov/main/ws_server.c)
- [clipboard_service.c](file:///Users/bytedance/esp/softap_prov/main/clipboard_service.c)
- [lcd_display.c](file:///Users/bytedance/esp/softap_prov/main/lcd_display.c)
- [ui_manager.c](file:///Users/bytedance/esp/softap_prov/main/ui_manager.c)
- [usb_hid.c](file:///Users/bytedance/esp/softap_prov/main/usb_hid.c)
- [button.c](file:///Users/bytedance/esp/softap_prov/main/button.c)
