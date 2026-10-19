/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * LVGL 相关参数，可由用户调整
 *
 */
#define LVGL_PORT_H_RES             (800)
#define LVGL_PORT_V_RES             (480)
#define LVGL_PORT_TICK_PERIOD_MS    (CONFIG_EXAMPLE_LVGL_PORT_TICK)

/**
 * LVGL 定时器处理任务相关参数，可由用户调整
 *
 */
#define LVGL_PORT_TASK_MAX_DELAY_MS (CONFIG_EXAMPLE_LVGL_PORT_TASK_MAX_DELAY_MS)    // LVGL 定时器任务的最大延时，单位毫秒
#define LVGL_PORT_TASK_MIN_DELAY_MS (CONFIG_EXAMPLE_LVGL_PORT_TASK_MIN_DELAY_MS)    // LVGL 定时器任务的最小延时，单位毫秒
#define LVGL_PORT_TASK_STACK_SIZE   (CONFIG_EXAMPLE_LVGL_PORT_TASK_STACK_SIZE_KB * 1024) // LVGL 定时器任务的栈大小，单位字节
#define LVGL_PORT_TASK_PRIORITY     (CONFIG_EXAMPLE_LVGL_PORT_TASK_PRIORITY)        // LVGL 定时器任务的优先级
#define LVGL_PORT_TASK_CORE         (CONFIG_EXAMPLE_LVGL_PORT_TASK_CORE)            // LVGL 定时器任务所在的核，
// `-1` 表示不指定核
/**
 *
 * LVGL 缓冲相关参数，可由用户调整：
 *  （使能防撕裂功能时，这些参数将不再生效）
 *
 *  - 缓冲分配的内存类型：
 *      - MALLOC_CAP_SPIRAM: 在 PSRAM 中分配 LVGL 缓冲
 *      - MALLOC_CAP_INTERNAL: 在 SRAM 中分配 LVGL 缓冲
 *      （SRAM 比 PSRAM 更快，但 PSRAM 容量更大）
 *
 */
#if CONFIG_EXAMPLE_LVGL_PORT_BUF_PSRAM
#define LVGL_PORT_BUFFER_MALLOC_CAPS    (MALLOC_CAP_SPIRAM)
#elif CONFIG_EXAMPLE_LVGL_PORT_BUF_INTERNAL
#define LVGL_PORT_BUFFER_MALLOC_CAPS    (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#endif
#define LVGL_PORT_BUFFER_HEIGHT         (CONFIG_EXAMPLE_LVGL_PORT_BUF_HEIGHT)

/**
 * 防撕裂相关配置，可由用户调整。
 *
 */
#define LVGL_PORT_AVOID_TEAR_ENABLE     (CONFIG_EXAMPLE_LVGL_PORT_AVOID_TEAR_ENABLE) // 设为 1 以启用
#if LVGL_PORT_AVOID_TEAR_ENABLE
/**
 * 设置防撕裂模式：
 *      - 0：关闭防撕裂功能
 *      - 1：LCD 双缓冲 & LVGL 全屏刷新
 *      - 2：LCD 三缓冲 & LVGL 全屏刷新
 *      - 3：LCD 双缓冲 & LVGL 直接模式（推荐）
 *
 */
#define LVGL_PORT_AVOID_TEAR_MODE       (CONFIG_EXAMPLE_LVGL_PORT_AVOID_TEAR_MODE)

/**
 * 使能防撕裂功能时设置 LCD 面板的旋转角度：
 *      - 0：0 度
 *      - 90：90 度
 *      - 180：180 度
 *      - 270：270 度
 *
 */
#define EXAMPLE_LVGL_PORT_ROTATION_DEGREE  (CONFIG_EXAMPLE_LVGL_PORT_ROTATION_DEGREE)

/**
 * 以下配置根据上述配置自动设置，用户无需修改。
 *
 */
#if LVGL_PORT_AVOID_TEAR_MODE == 1
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_FULL_REFRESH          (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 2
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (3)
#define LVGL_PORT_FULL_REFRESH          (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 3
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_DIRECT_MODE           (1)
#endif /* LVGL_PORT_AVOID_TEAR_MODE */

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0
#define EXAMPLE_LVGL_PORT_ROTATION_0    (1)
#else
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 90
#define EXAMPLE_LVGL_PORT_ROTATION_90   (1)
#elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 180
#define EXAMPLE_LVGL_PORT_ROTATION_180  (1)
#elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 270
#define EXAMPLE_LVGL_PORT_ROTATION_270  (1)
#endif
#ifdef LVGL_PORT_LCD_RGB_BUFFER_NUMS
#undef LVGL_PORT_LCD_RGB_BUFFER_NUMS
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (3)
#endif
#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */
#else
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (1)
#define LVGL_PORT_FULL_REFRESH          (0)
#define LVGL_PORT_DIRECT_MODE           (0)
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

/**
 * @brief 初始化 LVGL 移植层
 *
 * @param[in] lcd_handle: LCD 面板句柄
 * @param[in] tp_handle: 触摸面板句柄
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_ERR_INVALID_ARG: 无效参数
 *      - 其它: 失败
 */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle);

/**
 * @brief 获取 LVGL 互斥量
 *
 * @param[in] timeout_ms: 超时时间 [ms]。0 表示无限阻塞。
 *
 * @return
 *      - true:  已获取到互斥量
 *      - false: 未获取到互斥量
 */
bool lvgl_port_lock(int timeout_ms);

/**
 * @brief 释放 LVGL 互斥量
 *
 */
void lvgl_port_unlock(void);

/**
 * @brief 当 RGB 帧缓冲传输完成时通知 LVGL 任务。
 *
 * @return
 *      - true:  任务需要被重新调度
 *      - false: 任务不需要被重新调度
 */
bool lvgl_port_notify_rgb_vsync(void);

#ifdef __cplusplus
}
#endif
