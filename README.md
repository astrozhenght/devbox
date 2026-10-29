# DevBox

DevBox 是一个基于 STM32H743 的嵌入式桌面终端项目，使用 FreeRTOS 管理任务，使用 LVGL 构建 800×480 图形界面，并集成触摸、Wi-Fi、串口终端、RTC、外部 Flash、SDRAM 和 SD 卡等功能。

项目重点不只是实现功能，也关注嵌入式工程的分层、模块边界和可维护性。当前代码按照 `App / BSP / Platform / Common / ThirdParty` 组织，尽量避免 UI、业务逻辑与 MCU 外设实现直接耦合。

## 主要功能

- LVGL 图形主界面和应用启动器
- GT911 电容触摸输入
- UART 串口调试终端
- ESP8266 Wi-Fi 联网
- 网络天气获取与显示
- SNTP 网络时间同步和 RTC 时间维护
- 日历界面
- LTDC + DMA2D LCD 显示加速
- SDRAM 显存
- W25Q 系列 QSPI Flash
- SDMMC + FatFS 文件系统接口

## 硬件与软件

### 硬件

- MCU：STM32H743，Arm Cortex-M7
- LCD：800×480，LTDC 驱动
- 触摸：GT911
- Wi-Fi：ESP8266，UART6 + DMA
- 外部存储：SDRAM、W25Q QSPI Flash、SD 卡

### 软件

- Keil MDK-ARM
- Arm Compiler 6.19
- STM32 HAL / CMSIS
- FreeRTOS V10.3.1
- LVGL V8.3.11
- FatFS R0.14（Revision 5380）
- cJSON

## 软件架构

```text
App
 ├─ LVGL UI
 └─ Service
        ↓
BSP
 ├─ LCD / Touch
 ├─ Wi-Fi / RTC
 └─ Flash / SD
        ↓
Platform
 ├─ MCU 外设适配
 └─ FreeRTOS / LVGL / FatFS 移植
        ↓
ThirdParty
 ├─ STM32 HAL / CMSIS
 ├─ FreeRTOS / LVGL
 └─ FatFS / cJSON
```

- **App**：界面、业务服务和应用流程，不直接操作 MCU 寄存器。
- **BSP**：面向板载器件提供接口，例如 LCD、GT911、ESP8266 和 W25Q Flash。
- **Platform**：封装 MCU 外设初始化以及第三方组件的目标平台适配。
- **Common**：与硬件和业务无关的通用工具，例如环形缓冲区和十六进制转换。
- **ThirdParty**：未经业务化修改或尽量保持原始结构的第三方源码。

更完整的分层说明见 [工程架构 V2.1](Docs/PROJECT_ARCHITECTURE_V2.1.md)。

## 目录结构

```text
devbox/
├─ App/                     # UI、业务服务和本地配置模板
│  ├─ Config/
│  ├─ LVGL/
│  └─ Service/
├─ BSP/                     # 板载器件驱动
├─ Common/                  # 通用组件
├─ Platform/
│  ├─ MCU/                  # STM32 Core 与外设初始化
│  └─ Port/                 # FreeRTOS、LVGL、FatFS 移植层
├─ Project/MDK-ARM/         # Keil 工程、启动文件和链接脚本
├─ ThirdParty/              # 第三方库源码
├─ Docs/                    # 项目文档
└─ Build/                   # 本地编译输出，不提交到 Git
```

## 获取并配置工程

### 1. 克隆仓库

```bash
git clone https://github.com/astrozhenght/devbox.git
cd devbox
```

### 2. 创建本地凭据文件

复制配置模板：

```powershell
Copy-Item App/Config/wifi_credentials.example.h App/Config/wifi_credentials.h
```

然后编辑 `App/Config/wifi_credentials.h`：

```c
#define APP_WIFI_SSID          "your-wifi-ssid"
#define APP_WIFI_PASSWORD      "your-wifi-password"
#define APP_WEATHER_API_KEY    "your-seniverse-api-key"
```

`wifi_credentials.h` 已加入 `.gitignore`，不会提交到 Git。请勿把真实密码或 API Key 写入其他源码。

### 3. 打开并编译

1. 使用 Keil MDK 打开 `Project/MDK-ARM/project.uvprojx`。
2. 确认已安装 STM32H7 Device Family Pack。
3. 选择 `project` Target。
4. 执行 Build。
5. 连接调试器，将程序下载到目标板。

编译产物生成在 `Build/MDK-ARM/`，该目录不会提交到仓库。

## 关键模块

| 模块 | 位置 | 作用 |
| --- | --- | --- |
| 主界面 | `App/LVGL/ui_home.c` | 状态栏、时间天气和应用启动器 |
| 串口终端 | `App/LVGL/ui_uart_terminal.c` | 串口收发与数据显示界面 |
| Wi-Fi 服务 | `App/Service/wifi_service_stm32.c` | Wi-Fi命令队列、天气与时间服务适配 |
| LCD BSP | `BSP/Src/bsp_lcd.c` | LCD 面板与帧缓冲操作 |
| 触摸 BSP | `BSP/Src/bsp_touch.c` | GT911 初始化和触摸数据读取 |
| Wi-Fi BSP | `BSP/Src/bsp_wifi.c` | ESP8266 AT 通信与 UART DMA 接收 |
| MCU 平台 | `Platform/MCU/User/` | DMA、LTDC、UART、RTC、QSPI 等外设初始化 |
| LVGL 移植 | `Platform/Port/LVGL/` | 显示、输入设备和文件系统接口 |

## 开发约定

- 公共函数和变量使用小写蛇形命名，例如 `bsp_touch_init()`。
- BSP 接口统一使用 `bsp_` 前缀。
- MCU 平台接口统一使用 `platform_` 前缀。
- UI 文件只处理界面创建、事件和显示更新。
- 业务流程放在 `App/Service`，避免在 UI 中直接调用 HAL。
- 第三方源码集中放在 `ThirdParty`，非必要不直接修改。
- 编译产物、IDE 用户文件和真实凭据不提交到仓库。

## 注意事项

- ESP8266 通常只支持 2.4 GHz Wi-Fi，请确认热点频段和加密方式兼容。
- FreeRTOS 环境下，中断优先级必须符合 `FreeRTOSConfig.h` 的约束。
- LCD 帧缓冲位于外部 SDRAM，修改显示配置时需同步检查地址和像素格式。
- 更换 LCD、触摸芯片或板载器件时，应优先修改 BSP 和 Platform 层，避免把硬件差异扩散到 App 层。

## 第三方组件

本项目包含 STM32 HAL/CMSIS、FreeRTOS、LVGL、FatFS 和 cJSON。各组件遵循其原始许可证，相关许可证文件保留在对应的 `ThirdParty` 子目录中。
