| 支持的目标芯片 | ESP32-S3 |
| -------------- | -------- |

| 支持的 LCD 控制器 | ST7701 |
| ----------------- | ------ |

| 支持的触摸控制器 | GT911 |
| ---------------- | ----- |

# RGB 防撕裂示例

[esp_lcd](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/lcd.html) 开箱即提供了多种面板驱动，例如 ST7789、SSD1306、NT35510。然而市场上还有大量其他面板，把它们全部包含进来已超出 `esp_lcd` 组件的职责范围。

`esp_lcd` 允许用户在工程范围内添加自己的面板驱动（即面板驱动可以位于 esp-idf 之外），因此只要用户实现的面板驱动遵循 `esp_lcd` 组件定义的接口，上层代码（例如 LVGL 移植代码）就可以不加修改地复用。

本示例演示在 esp-idf 工程中把 LVGL 与 RGB 接口屏幕配合使用时如何防撕裂。示例会在开机后使用 LVGL 库绘制一个居中的 Hello LVGL 标签，并附上 ESP32-S3 LVGL Port 副标题的极简界面，用于肉眼确认屏幕与刷新链路工作正常。

本示例使用 [esp_timer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html) 产生 LVGL 所需的时钟节拍，并使用一个专用任务运行 `lv_timer_handler()`。由于 LVGL 的 API 不是线程安全的，本示例使用一个互斥量，在调用 `lv_timer_handler()` 之前获取、调用之后释放。在其他任务和线程中，围绕每一处 LVGL（lv_...）相关的函数调用与代码，都需要使用同一个互斥量。更多移植指南请参考 [LVGL porting doc](https://docs.lvgl.io/master/porting/index.html)。

## 如何使用本示例

## ESP-IDF 版本要求

### 所需硬件

* 一块 ESP32-S3R8 开发板
* 一块 ST7701 LCD 面板，带 RGB 接口
* 一根 USB 数据线，用于供电与烧写程序

### 硬件连接

ESP 开发板与 LCD 之间的连接如下：

```
       ESP Board                           RGB  Panel
+-----------------------+              +-------------------+
|                   GND +--------------+GND                |
|                       |              |                   |
|                   3V3 +--------------+VCC                |
|                       |              |                   |
|                   PCLK+--------------+PCLK               |
|                       |              |                   |
|             DATA[15:0]+--------------+DATA[15:0]         |
|                       |              |                   |
|                  HSYNC+--------------+HSYNC              |
|                       |              |                   |
|                  VSYNC+--------------+VSYNC              |
|                       |              |                   |
|                     DE+--------------+DE                 |
|                       |              |                   |
|               BK_LIGHT+--------------+BLK                |
+-----------------------+              |                   |
                               3V3-----+DISP_EN            |
                                       |                   |
                                       +-------------------+
```

* 本示例使用的 LCD 参数与 GPIO 编号可以在 [waveshare_rgb_lcd_port.c](main/waveshare_rgb_lcd_port.c) 中修改。请特别注意**厂商特定的初始化序列**，它在不同厂商之间可能不同，应向 LCD 供应商索取初始化序列代码。
* LVGL 参数不仅可以通过 `menuconfig` 修改，也可以直接修改 `main/lvgl_port.h` 中的 LVGL 参数宏。

### 配置工程

运行 `idf.py menuconfig`，进入 `示例配置` 菜单。

### 编译与烧写

运行 `idf.py set-target esp32s3` 选择目标芯片。

运行 `idf.py -p PORT build flash monitor` 完成编译、烧写并监视日志。LCD 上会如期显示出居中的 Hello LVGL 标签与 ESP32-S3 LVGL Port 副标题。

第一次为本示例运行 `idf.py` 时会额外耗时，因为构建系统需要解析组件依赖，并把缺失的组件从组件仓库下载到 `managed_components` 文件夹。

（要退出串口监视器，请按 ``Ctrl-]``。）

完整的工程配置步骤以及使用 ESP-IDF 构建工程的方法请参见 [入门指南](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html)。

## 故障排查

如有任何技术问题，请在 GitHub 上提交 [issue](https://github.com/espressif/esp-iot-solution/issues)，我们会尽快回复。

---

## 快速开始（克隆与编译）

**前置条件**

* [ESP-IDF **v5.4.1**](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32s3/get-started/)（本工程验证所用的版本）
* 搭载 7 英寸 RGB LCD 面板（ST7701）与 GT911 电容触摸的 ESP32-S3 开发板

**本工程假设的硬件：** ESP32-S3R8、ST7701 RGB LCD、经 I2C 连接的 GT911 触摸面板。

```bash
git clone https://github.com/eeee4444/lvgl_porting.git
cd lvgl_porting
. $IDF_PATH/export.sh          # Linux / macOS
# . C:\Espressif\frameworks\esp-idf-v5.4.1\export.ps1   # Windows PowerShell

idf.py set-target esp32s3
idf.py -p PORT build flash monitor
```

**说明**

* 所有第三方组件（LVGL 8.4.0、esp_lcd_touch、esp_lcd_touch_gt911）都以源码形式内置（vendored）在 `components/` 下，因此无需下载组件，构建可以离线完成。
* `dependencies.lock` 是**有意不纳入版本跟踪**的：它由 IDF 组件管理器生成，否则会带有与本机相关的绝对路径，导致在其他机器上配置失败。首次构建时它会自动重新生成。
* 如果你改动了某个组件或 `main/idf_component.yml`，请删除 `dependencies.lock`，让它重新生成。
