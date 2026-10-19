/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"

// VSYNC 事件回调函数
IRAM_ATTR static bool rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    return lvgl_port_notify_rgb_vsync();
}

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
/**
 * @brief I2C 主机初始化
 */
static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    // 配置 I2C 参数
    i2c_param_config(i2c_master_port, &i2c_conf);

    // 安装 I2C 驱动
    return i2c_driver_install(i2c_master_port, i2c_conf.mode, 0, 0, 0);
}

// GPIO 初始化
void gpio_init(void)
{
    // 将配置结构体清零
    gpio_config_t io_conf = {};
    // 禁用中断
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // 引脚位掩码，此处使用 GPIO4
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    // 设置为输入模式
    io_conf.mode = GPIO_MODE_OUTPUT;

    gpio_config(&io_conf);
}

// 复位触摸屏
void waveshare_esp32_s3_touch_reset()
{
    uint8_t write_buf = 0x01;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x24, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    // 复位触摸屏。建议在使用触摸屏之前先复位。
    write_buf = 0x2C;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    esp_rom_delay_us(100 * 1000);
    gpio_set_level(GPIO_INPUT_IO_4, 0);
    esp_rom_delay_us(100 * 1000);
    write_buf = 0x2E;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    esp_rom_delay_us(200 * 1000);
}

#endif

// 初始化 RGB LCD
esp_err_t waveshare_esp32_s3_rgb_lcd_init()
{
    ESP_LOGI(TAG, "安装 RGB LCD 面板驱动"); // 记录 RGB LCD 面板驱动安装的开始
    esp_lcd_panel_handle_t panel_handle = NULL; // 声明 LCD 面板句柄
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT, // 设置面板的时钟源
        .timings =  {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ, // 像素时钟频率
            .h_res = EXAMPLE_LCD_H_RES, // 水平分辨率
            .v_res = EXAMPLE_LCD_V_RES, // 垂直分辨率
            .hsync_pulse_width = 4, // 水平同步脉冲宽度
            .hsync_back_porch = 8, // 水平后沿
            .hsync_front_porch = 8, // 水平前沿
            .vsync_pulse_width = 4, // 垂直同步脉冲宽度
            .vsync_back_porch = 8, // 垂直后沿
            .vsync_front_porch = 8, // 垂直前沿
            .flags = {
                .pclk_active_neg = 1, // 像素时钟低电平有效
            },
        },
        .data_width = EXAMPLE_RGB_DATA_WIDTH, // RGB 数据宽度
        .bits_per_pixel = EXAMPLE_RGB_BIT_PER_PIXEL, // 每像素位数
        .num_fbs = LVGL_PORT_LCD_RGB_BUFFER_NUMS, // 帧缓冲数量
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE, // 弹跳缓冲大小（单位：像素）
        .sram_trans_align = 4, // SRAM 传输对齐
        .psram_trans_align = 64, // PSRAM 传输对齐
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC, // 水平同步 GPIO 引脚号
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC, // 垂直同步 GPIO 引脚号
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE, // 数据使能 GPIO 引脚号
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK, // 像素时钟 GPIO 引脚号
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP, // 显示 GPIO 引脚号
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA0,
            EXAMPLE_LCD_IO_RGB_DATA1,
            EXAMPLE_LCD_IO_RGB_DATA2,
            EXAMPLE_LCD_IO_RGB_DATA3,
            EXAMPLE_LCD_IO_RGB_DATA4,
            EXAMPLE_LCD_IO_RGB_DATA5,
            EXAMPLE_LCD_IO_RGB_DATA6,
            EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8,
            EXAMPLE_LCD_IO_RGB_DATA9,
            EXAMPLE_LCD_IO_RGB_DATA10,
            EXAMPLE_LCD_IO_RGB_DATA11,
            EXAMPLE_LCD_IO_RGB_DATA12,
            EXAMPLE_LCD_IO_RGB_DATA13,
            EXAMPLE_LCD_IO_RGB_DATA14,
            EXAMPLE_LCD_IO_RGB_DATA15,
        },
        .flags = {
            .fb_in_psram = 1, // 使用 PSRAM 作为帧缓冲
        },
    };

    // 使用指定配置创建新的 RGB 面板
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

    ESP_LOGI(TAG, "初始化 RGB LCD 面板"); // 记录 RGB LCD 面板的初始化
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle)); // 初始化 LCD 面板

    esp_lcd_touch_handle_t tp_handle = NULL; // 声明触摸面板句柄
#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
    ESP_LOGI(TAG, "初始化 I2C 总线"); // 记录 I2C 总线的初始化
    i2c_master_init(); // 初始化 I2C 主机
    ESP_LOGI(TAG, "初始化 GPIO"); // 记录 GPIO 初始化
    gpio_init(); // 初始化 GPIO 引脚
    ESP_LOGI(TAG, "初始化触摸 LCD"); // 记录触摸 LCD 初始化
    waveshare_esp32_s3_touch_reset(); // 复位触摸面板

    esp_lcd_panel_io_handle_t tp_io_handle = NULL; // 声明触摸面板 I/O 句柄
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG(); // 配置 GT911 触摸控制器的 I2C

    ESP_LOGI(TAG, "初始化 I2C 面板 IO"); // 记录 I2C 面板 I/O 初始化
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_MASTER_NUM, &tp_io_config, &tp_io_handle)); // 创建新的 I2C 面板 I/O

    ESP_LOGI(TAG, "初始化触摸控制器 GT911"); // 记录触摸控制器初始化
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES, // 设置最大 X 坐标
        .y_max = EXAMPLE_LCD_V_RES, // 设置最大 Y 坐标
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST, // 复位 GPIO 引脚号
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT, // 中断 GPIO 引脚号
        .levels = {
            .reset = 0, // 复位电平
            .interrupt = 0, // 中断电平
        },
        .flags = {
            .swap_xy = 0, // 不交换 X 和 Y
            .mirror_x = 0, // 不镜像 X
            .mirror_y = 0, // 不镜像 Y
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle)); // 创建新的 I2C GT911 触摸控制器
#endif // CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911

    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle)); // 使用面板和触摸句柄初始化 LVGL

    // 注册 RGB 面板事件回调
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
#if EXAMPLE_RGB_BOUNCE_BUFFER_SIZE > 0
        .on_bounce_frame_finish = rgb_lcd_on_vsync_event, // 弹跳帧完成回调
#else
        .on_vsync = rgb_lcd_on_vsync_event, // 垂直同步回调
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL)); // 注册事件回调

    return ESP_OK; // 返回成功
}

/******************************* 打开屏幕背光 **************************************/
esp_err_t wavesahre_rgb_lcd_bl_on()
{
    // 配置 CH422G 为输出模式
    uint8_t write_buf = 0x01;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x24, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    // 拉高背光引脚以点亮屏幕背光
    write_buf = 0x1E;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    return ESP_OK;
}

/******************************* 关闭屏幕背光 **************************************/
esp_err_t wavesahre_rgb_lcd_bl_off()
{
    // 配置 CH422G 为输出模式
    uint8_t write_buf = 0x01;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x24, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    // 拉低背光引脚以关闭屏幕背光
    write_buf = 0x1A;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    return ESP_OK;
}
