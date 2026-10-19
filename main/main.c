/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"

static void create_main_ui(void)
{
    lv_obj_t *scr = lv_scr_act(); // 获取当前活动屏幕
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0); // 将屏幕背景设为黑色
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);        // 屏幕背景设为不透明

    lv_obj_t *label = lv_label_create(scr); // 创建标签
    lv_label_set_text(label, "Hello LVGL"); // 设置标签文本
    lv_obj_set_style_text_color(label, lv_color_white(), 0); // 设置标签文本颜色
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0); // 设置标签字体
    lv_obj_center(label); // 将标签居中显示

    lv_obj_t *sub_label = lv_label_create(scr); // 创建副标签
    lv_label_set_text(sub_label, "ESP32-S3 LVGL Port"); // 设置副标签文本
    lv_obj_set_style_text_color(sub_label, lv_color_white(), 0); // 设置副标签文本颜色
    lv_obj_set_style_text_font(sub_label, &lv_font_montserrat_16, 0); // 设置副标签字体
    lv_obj_align_to(sub_label, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8); // 将副标签对齐到主标签下方
}

void app_main()
{
    waveshare_esp32_s3_rgb_lcd_init(); // 初始化 Waveshare ESP32-S3 RGB LCD
    // wavesahre_rgb_lcd_bl_on();  // 打开屏幕背光
    // wavesahre_rgb_lcd_bl_off(); // 关闭屏幕背光

    ESP_LOGI(TAG, "显示 LVGL 界面");
    // 由于 LVGL API 非线程安全，需先加锁
    if (lvgl_port_lock(-1)) {
        create_main_ui(); // 创建主界面
        // 释放互斥量
        lvgl_port_unlock();
    }
}
