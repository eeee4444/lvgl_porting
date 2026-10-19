/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lvgl_port.h"

static const char *TAG = "lv_port";                      // 日志标签
static SemaphoreHandle_t lvgl_mux;                       // 用于同步的 LVGL 互斥量
static TaskHandle_t lvgl_task_handle = NULL;             // LVGL 任务的句柄

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0
// 用于双缓冲的下一帧缓冲获取函数
static void *get_next_frame_buffer(esp_lcd_panel_handle_t panel_handle)
{
    static void *next_fb = NULL;                          // 指向下一帧缓冲的指针
    static void *fb[2] = { NULL };                        // 存放两个帧缓冲的数组
    if (next_fb == NULL) {
        ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb[0], &fb[1])); // 获取帧缓冲
        next_fb = fb[1];                                  // 初始化为第二个缓冲
    } else {
        // 在两个帧缓冲之间切换
        next_fb = (next_fb == fb[0]) ? fb[1] : fb[0];
    }
    return next_fb;                                       // 返回下一个帧缓冲
}

// 将像素从一个缓冲旋转拷贝到另一个缓冲的函数
IRAM_ATTR static void rotate_copy_pixel(const uint16_t *from, uint16_t *to, uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t w, uint16_t h, uint16_t rotation)
{
    int from_index = 0;                                   // 源缓冲区的索引
    int to_index = 0;                                     // 目标缓冲区的索引
    int to_index_const = 0;                               // 目标缓冲区的常量索引

    switch (rotation) {
    case 90:
        to_index_const = (w - x_start - 1) * h;          // 计算 90 度旋转的常量索引
        for (int from_y = y_start; from_y < y_end + 1; from_y++) {
            from_index = from_y * w + x_start;           // 计算源缓冲区中的索引
            to_index = to_index_const + from_y;          // 计算目标缓冲区中的索引
            for (int from_x = x_start; from_x < x_end + 1; from_x++) {
                *(to + to_index) = *(from + from_index);  // 复制像素
                from_index += 1;                          // 移动到源缓冲区的下一个像素
                to_index -= h;                            // 移动到目标缓冲区的下一个像素
            }
        }
        break;
    case 180:
        to_index_const = h * w - x_start - 1;            // 计算 180 度旋转的常量索引
        for (int from_y = y_start; from_y < y_end + 1; from_y++) {
            from_index = from_y * w + x_start;           // 计算源缓冲区中的索引
            to_index = to_index_const - from_y * w;      // 计算目标缓冲区中的索引
            for (int from_x = x_start; from_x < x_end + 1; from_x++) {
                *(to + to_index) = *(from + from_index);  // 复制像素
                from_index += 1;                          // 移动到源缓冲区的下一个像素
                to_index -= 1;                            // 移动到目标缓冲区的下一个像素
            }
        }
        break;
    case 270:
        to_index_const = (x_start + 1) * h - 1;          // 计算 270 度旋转的常量索引
        for (int from_y = y_start; from_y < y_end + 1; from_y++) {
            from_index = from_y * w + x_start;           // 计算源缓冲区中的索引
            to_index = to_index_const - from_y;          // 计算目标缓冲区中的索引
            for (int from_x = x_start; from_x < x_end + 1; from_x++) {
                *(to + to_index) = *(from + from_index);  // 复制像素
                from_index += 1;                          // 移动到源缓冲区的下一个像素
                to_index += h;                            // 移动到目标缓冲区的下一个像素
            }
        }
        break;
    default:
        break;                                             // 不支持的旋转角度不做处理
    }
}
#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */

#if LVGL_PORT_AVOID_TEAR_ENABLE
#if LVGL_PORT_DIRECT_MODE
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0

// 用于保存待刷新脏区域信息的结构体
typedef struct {
    uint16_t inv_p;                                   // 无效区域的数量
    uint8_t inv_area_joined[LV_INV_BUF_SIZE];       // 记录已合并无效区域的数组
    lv_area_t inv_areas[LV_INV_BUF_SIZE];            // 无效区域数组
} lv_port_dirty_area_t;

// 刷新状态枚举
typedef enum {
    FLUSH_STATUS_PART,                                // 部分刷新
    FLUSH_STATUS_FULL                                 // 全屏刷新
} lv_port_flush_status_t;

// 刷新探测结果枚举
typedef enum {
    FLUSH_PROBE_PART_COPY,                           // 部分拷贝的探测结果
    FLUSH_PROBE_SKIP_COPY,                           // 跳过拷贝的探测结果
    FLUSH_PROBE_FULL_COPY,                           // 全量拷贝的探测结果
} lv_port_flush_probe_t;

static lv_port_dirty_area_t dirty_area;             // 脏区域结构体的实例

// 保存当前脏区域信息的函数
static void flush_dirty_save(lv_port_dirty_area_t *dirty_area)
{
    lv_disp_t *disp = _lv_refr_get_disp_refreshing(); // 获取当前正在刷新的显示器
    dirty_area->inv_p = disp->inv_p;                  // 保存无效区域的数量
    for (int i = 0; i < disp->inv_p; i++) {
        dirty_area->inv_area_joined[i] = disp->inv_area_joined[i]; // 保存已合并区域的状态
        dirty_area->inv_areas[i] = disp->inv_areas[i]; // 保存无效区域
    }
}

/**
 * @brief 探测需要拷贝的脏区域
 *
 * @note 本函数用于防撕裂，仅在 LVGL 直接模式下有效。
 *
 */
static lv_port_flush_probe_t flush_copy_probe(lv_disp_drv_t *drv)
{
    static lv_port_flush_status_t prev_status = FLUSH_STATUS_PART; // 上一次的刷新状态
    lv_port_flush_status_t cur_status;                            // 当前刷新状态
    lv_port_flush_probe_t probe_result;                           // 探测结果
    lv_disp_t *disp_refr = _lv_refr_get_disp_refreshing();       // 获取当前正在刷新的显示器

    uint32_t flush_ver = 0;                                       // 待刷新的垂直尺寸
    uint32_t flush_hor = 0;                                       // 待刷新的水平尺寸
    for (int i = 0; i < disp_refr->inv_p; i++) {
        if (disp_refr->inv_area_joined[i] == 0) {
            flush_ver = (disp_refr->inv_areas[i].y2 + 1 - disp_refr->inv_areas[i].y1); // 计算垂直刷新尺寸
            flush_hor = (disp_refr->inv_areas[i].x2 + 1 - disp_refr->inv_areas[i].x1); // 计算水平刷新尺寸
            break; // 找到首个未合并区域后退出循环
        }
    }
    /* 检查当前是否为全屏刷新 */
    cur_status = ((flush_ver == drv->ver_res) && (flush_hor == drv->hor_res)) ? (FLUSH_STATUS_FULL) : (FLUSH_STATUS_PART);

    // 根据上一次与当前状态确定探测结果
    if (prev_status == FLUSH_STATUS_FULL) {
        if ((cur_status == FLUSH_STATUS_PART)) {
            probe_result = FLUSH_PROBE_FULL_COPY; // 需要全量拷贝
        } else {
            probe_result = FLUSH_PROBE_SKIP_COPY; // 跳过拷贝
        }
    } else {
        probe_result = FLUSH_PROBE_PART_COPY; // 需要部分拷贝
    }
    prev_status = cur_status; // 更新上一次的状态

    return probe_result; // 返回探测结果
}

// 获取下一个刷新用缓冲的内联函数
static inline void *flush_get_next_buf(void *panel_handle)
{
    return get_next_frame_buffer(panel_handle); // 返回下一个帧缓冲
}

/**
 * @brief 拷贝脏区域
 *
 * @note 本函数用于防撕裂，仅在 LVGL 直接模式下有效。
 *
 */
static void flush_dirty_copy(void *dst, void *src, lv_port_dirty_area_t *dirty_area)
{
    lv_coord_t x_start, x_end, y_start, y_end; // 待拷贝区域的坐标
    for (int i = 0; i < dirty_area->inv_p; i++) {
        /* 刷新未合并的区域 */
        if (dirty_area->inv_area_joined[i] == 0) {
            x_start = dirty_area->inv_areas[i].x1; // 起始 X 坐标
            x_end = dirty_area->inv_areas[i].x2;   // 结束 X 坐标
            y_start = dirty_area->inv_areas[i].y1; // 起始 Y 坐标
            y_end = dirty_area->inv_areas[i].y2;   // 结束 Y 坐标

            // 将像素数据从源缓冲旋转拷贝到目标缓冲
            rotate_copy_pixel(src, dst, x_start, y_start, x_end, y_end, LV_HOR_RES, LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);
        }
    }
}


static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data; // 从驱动用户数据中获取面板句柄
    const int offsetx1 = area->x1; // 待刷新区域的起始 X 坐标
    const int offsetx2 = area->x2; // 待刷新区域的结束 X 坐标
    const int offsety1 = area->y1; // 待刷新区域的起始 Y 坐标
    const int offsety2 = area->y2; // 待刷新区域的结束 Y 坐标
    void *next_fb = NULL; // 指向下一帧缓冲的指针
    lv_port_flush_probe_t probe_result = FLUSH_PROBE_PART_COPY; // 默认探测结果
    lv_disp_t *disp = lv_disp_get_default(); // 获取默认显示器

    /* 最后一个区域刷新完成后的处理 */
    if (lv_disp_flush_is_last(drv)) {
        /* 检查 `full_refresh` 标志是否已被触发 */
        if (drv->full_refresh) {
            /* 复位标志 */
            drv->full_refresh = 0;

            // 将整屏 LVGL 缓冲的数据旋转拷贝到下一个帧缓冲
            next_fb = flush_get_next_buf(panel_handle);
            rotate_copy_pixel((uint16_t *)color_map, next_fb, offsetx1, offsety1, offsetx2, offsety2, LV_HOR_RES, LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);

            /* 将当前 RGB 帧缓冲切换为 `next_fb` */
            esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, next_fb);

            /* 等待当前帧缓冲传输完成 */
            ulTaskNotifyValueClear(NULL, ULONG_MAX);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            /* 同步更新另一个帧缓冲的脏区域 */
            flush_dirty_copy(flush_get_next_buf(panel_handle), color_map, &dirty_area);
            flush_get_next_buf(panel_handle);
        } else {
            /* 探测当前脏区域的拷贝方式 */
            probe_result = flush_copy_probe(drv);

            if (probe_result == FLUSH_PROBE_FULL_COPY) {
                /* 为下一个帧缓冲保存当前脏区域 */
                flush_dirty_save(&dirty_area);

                /* 设置 LVGL 全屏刷新标志并提前标记刷新就绪 */
                drv->full_refresh = 1; // 表示需要进行全屏刷新
                disp->rendering_in_progress = false; // 标记渲染未在进行中
                lv_disp_flush_ready(drv); // 标记刷新就绪

                /* 强制刷新整个屏幕，会递归调用 `flush_callback` */
                lv_refr_now(_lv_refr_get_disp_refreshing());
            } else {
                /* 为下一个帧缓冲更新当前脏区域 */
                next_fb = flush_get_next_buf(panel_handle);
                flush_dirty_save(&dirty_area);
                flush_dirty_copy(next_fb, color_map, &dirty_area);

                /* 将当前 RGB 帧缓冲切换为 `next_fb` */
                esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, next_fb);

                /* 等待当前帧缓冲传输完成 */
                ulTaskNotifyValueClear(NULL, ULONG_MAX);
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                if (probe_result == FLUSH_PROBE_PART_COPY) {
                    /* 同步更新另一个帧缓冲的脏区域 */
                    flush_dirty_save(&dirty_area);
                    flush_dirty_copy(flush_get_next_buf(panel_handle), color_map, &dirty_area);
                    flush_get_next_buf(panel_handle);
                }
            }
        }
    }

    lv_disp_flush_ready(drv); // 标记显示器刷新完成
}

#else

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data; // 从驱动用户数据中获取面板句柄
    const int offsetx1 = area->x1; // 待刷新区域的起始 X 坐标
    const int offsetx2 = area->x2; // 待刷新区域的结束 X 坐标
    const int offsety1 = area->y1; // 待刷新区域的起始 Y 坐标
    const int offsety2 = area->y2; // 待刷新区域的结束 Y 坐标

    /* 最后一个区域刷新完成后的处理 */
    if (lv_disp_flush_is_last(drv)) {
        /* 将当前 RGB 帧缓冲切换为 `color_map` */
        esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

        /* 等待最后一个帧缓冲传输完成 */
        ulTaskNotifyValueClear(NULL, ULONG_MAX);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    lv_disp_flush_ready(drv); // 标记显示器刷新完成
}
#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_RGB_BUFFER_NUMS == 2

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data; // 从驱动用户数据中获取面板句柄
    const int offsetx1 = area->x1; // 待刷新区域的起始 X 坐标
    const int offsetx2 = area->x2; // 待刷新区域的结束 X 坐标
    const int offsety1 = area->y1; // 待刷新区域的起始 Y 坐标
    const int offsety2 = area->y2; // 待刷新区域的结束 Y 坐标

    /* 将当前 RGB 帧缓冲切换为 `color_map` */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    /* 等待最后一个帧缓冲传输完成 */
    ulTaskNotifyValueClear(NULL, ULONG_MAX);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    lv_disp_flush_ready(drv); // 标记显示器刷新完成
}

#elif LVGL_PORT_FULL_REFRESH && LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0
static void *lvgl_port_rgb_last_buf = NULL; // 指向最后一个 RGB 缓冲的指针
static void *lvgl_port_rgb_next_buf = NULL; // 指向下一个 RGB 缓冲的指针
static void *lvgl_port_flush_next_buf = NULL; // 指向下一个刷新用缓冲的指针
#endif

void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data; // 从驱动用户数据中获取面板句柄
    const int offsetx1 = area->x1; // 待刷新区域的起始 X 坐标
    const int offsetx2 = area->x2; // 待刷新区域的结束 X 坐标
    const int offsety1 = area->y1; // 待刷新区域的起始 Y 坐标
    const int offsety2 = area->y2; // 待刷新区域的结束 Y 坐标

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0
    void *next_fb = get_next_frame_buffer(panel_handle); // 获取下一个帧缓冲

    /* 将脏区域从当前 LVGL 缓冲旋转拷贝到下一个 RGB 帧缓冲 */
    rotate_copy_pixel((uint16_t *)color_map, next_fb, offsetx1, offsety1, offsetx2, offsety2, LV_HOR_RES, LV_VER_RES, EXAMPLE_LVGL_PORT_ROTATION_DEGREE);

    /* 将当前 RGB 帧缓冲切换为 `next_fb` */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, next_fb);
#else
    drv->draw_buf->buf1 = color_map; // 将缓冲 1 设为 color_map
    drv->draw_buf->buf2 = lvgl_port_flush_next_buf; // 将缓冲 2 设为下一个刷新用缓冲
    lvgl_port_flush_next_buf = color_map; // 将下一个刷新用缓冲更新为 color_map

    /* 将当前 RGB 帧缓冲切换为 `color_map` */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    lvgl_port_rgb_next_buf = color_map; // 更新下一个 RGB 缓冲
#endif

    lv_disp_flush_ready(drv); // 标记显示器刷新完成
}
#endif

#else

void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data; // 从驱动用户数据中获取面板句柄
    const int offsetx1 = area->x1; // 待刷新区域的起始 X 坐标
    const int offsetx2 = area->x2; // 待刷新区域的结束 X 坐标
    const int offsety1 = area->y1; // 待刷新区域的起始 Y 坐标
    const int offsety2 = area->y2; // 待刷新区域的结束 Y 坐标

    /* 仅将颜色图中的数据拷贝到 RGB 帧缓冲 */
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    lv_disp_flush_ready(drv); // 标记显示器刷新完成
}

#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

static lv_disp_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    assert(panel_handle); // 确保面板句柄有效

    static lv_disp_draw_buf_t disp_buf = { 0 };     // 包含内部图形缓冲（即绘制缓冲）
    static lv_disp_drv_t disp_drv = { 0 };          // 包含 LCD 面板句柄与回调函数

    // 分配 LVGL 使用的绘制缓冲
    void *buf1 = NULL; // 指向第一个缓冲的指针
    void *buf2 = NULL; // 指向第二个缓冲的指针
    int buffer_size = 0; // 缓冲的大小

    ESP_LOGD(TAG, "为 LVGL 缓冲分配内存");
#if LVGL_PORT_AVOID_TEAR_ENABLE
    // 为防止撕裂，至少需要两个帧缓冲：一个用于 LVGL 渲染，另一个用于 RGB 输出
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_V_RES;
#if (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0) && LVGL_PORT_FULL_REFRESH
    // 使用三缓冲与全屏刷新时，总有一个缓冲可用于渲染
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 3, &lvgl_port_rgb_last_buf, &buf1, &buf2));
    lvgl_port_rgb_next_buf = lvgl_port_rgb_last_buf; // 设置下一个 RGB 缓冲
    lvgl_port_flush_next_buf = buf2; // 设置下一个刷新用缓冲
#elif (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0)
    // 使用三个帧缓冲：一个用于 LVGL 渲染，两个用于 RGB 驱动（其中一个用于旋转）
    void *fbs[3];
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 3, &fbs[0], &fbs[1], &fbs[2]));
    buf1 = fbs[2]; // 将 buf1 设为第三个帧缓冲
#else
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2)); // 获取两个帧缓冲
#endif
#else
    // 通常 RGB LCD 只需一个缓冲用于 LVGL 渲染
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT; // 计算缓冲大小
    buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LVGL_PORT_BUFFER_MALLOC_CAPS); // 分配内存
    assert(buf1); // 确保分配成功
    ESP_LOGI(TAG, "LVGL 缓冲大小: %dKB", buffer_size * sizeof(lv_color_t) / 1024); // 记录缓冲大小
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

    // 初始化 LVGL 绘制缓冲
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buffer_size); // 初始化绘制缓冲

    ESP_LOGD(TAG, "向 LVGL 注册显示驱动");
    lv_disp_drv_init(&disp_drv); // 初始化显示驱动
#if EXAMPLE_LVGL_PORT_ROTATION_90 || EXAMPLE_LVGL_PORT_ROTATION_270
    disp_drv.hor_res = LVGL_PORT_V_RES; // 设置旋转后的水平分辨率
    disp_drv.ver_res = LVGL_PORT_H_RES; // 设置旋转后的垂直分辨率
#else
    disp_drv.hor_res = LVGL_PORT_H_RES; // 设置水平分辨率
    disp_drv.ver_res = LVGL_PORT_V_RES; // 设置垂直分辨率
#endif
    disp_drv.flush_cb = flush_callback; // 设置刷新回调
    disp_drv.draw_buf = &disp_buf; // 设置绘制缓冲
    disp_drv.user_data = panel_handle; // 将用户数据设为面板句柄
#if LVGL_PORT_FULL_REFRESH
    disp_drv.full_refresh = 1; // 使能全屏刷新
#elif LVGL_PORT_DIRECT_MODE
    disp_drv.direct_mode = 1; // 使能直接模式
#endif
    return lv_disp_drv_register(&disp_drv); // 注册显示驱动
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)indev_drv->user_data; // 从用户数据中获取触摸面板句柄
    assert(tp); // 确保触摸面板句柄有效

    uint16_t touchpad_x; // X 坐标变量
    uint16_t touchpad_y; // Y 坐标变量
    uint8_t touchpad_cnt = 0; // 触摸点数量变量

    /* 从触摸控制器读取数据到内存 */
    esp_lcd_touch_read_data(tp); // 从触摸控制器读取数据

    /* 从触摸控制器读取数据 */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(tp, &touchpad_x, &touchpad_y, NULL, &touchpad_cnt, 1); // 获取触摸坐标
    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x; // 设置 X 坐标
        data->point.y = touchpad_y; // 设置 Y 坐标
        data->state = LV_INDEV_STATE_PRESSED; // 将状态设为按下
        ESP_LOGD(TAG, "触摸位置: %d,%d", touchpad_x, touchpad_y); // 记录触摸位置
    } else {
        data->state = LV_INDEV_STATE_RELEASED; // 将状态设为释放
    }
}

static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp)
{
    assert(tp); // 确保触摸面板句柄有效

    static lv_indev_drv_t indev_drv_tp; // 静态输入设备驱动

    /* 注册触摸面板输入设备 */
    lv_indev_drv_init(&indev_drv_tp); // 初始化输入设备驱动
    indev_drv_tp.type = LV_INDEV_TYPE_POINTER; // 将设备类型设为指针（触摸面板）
    indev_drv_tp.read_cb = touchpad_read; // 设置读取回调函数
    indev_drv_tp.user_data = tp; // 将用户数据设为触摸面板句柄

    return lv_indev_drv_register(&indev_drv_tp); // 注册输入设备驱动
}

static void tick_increment(void *arg)
{
    /* 告知 LVGL 已流逝的毫秒数 */
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS); // 递增 LVGL 时钟节拍计数
}

static esp_err_t tick_init(void)
{
    // LVGL 的时钟节拍接口（使用 esp_timer 产生 2ms 周期事件）
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &tick_increment, // 设置定时器的回调函数
        .name = "LVGL 时钟节拍" // 定时器名称
    };
    esp_timer_handle_t lvgl_tick_timer = NULL; // 定时器句柄
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer)); // 创建定时器
    return esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000); // 启动定时器
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGD(TAG, "启动 LVGL 任务"); // 记录任务启动

    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS; // 设置任务的初始延时
    while (1) {
        if (lvgl_port_lock(-1)) { // 尝试锁定 LVGL 互斥量
            task_delay_ms = lv_timer_handler(); // 处理 LVGL 定时器事件
            lvgl_port_unlock(); // 解锁互斥量
        }
        // 确保延时时间在限制范围内
        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms)); // 按计算出的时间延时任务
    }
}

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle)
{
    lv_init(); // 初始化 LVGL
    ESP_ERROR_CHECK(tick_init()); // 初始化时钟节拍定时器

    lv_disp_t *disp = display_init(lcd_handle); // 初始化显示器
    assert(disp); // 确保显示器初始化成功

    if (tp_handle) {
        lv_indev_t *indev = indev_init(tp_handle); // 初始化触摸面板输入设备
        assert(indev); // 确保输入设备初始化成功

        // 根据旋转设置触摸面板方向
#if EXAMPLE_LVGL_PORT_ROTATION_90
        esp_lcd_touch_set_swap_xy(tp_handle, true); // 交换 X 与 Y 坐标
        esp_lcd_touch_set_mirror_y(tp_handle, true); // 镜像 Y 坐标
#elif EXAMPLE_LVGL_PORT_ROTATION_180
        esp_lcd_touch_set_mirror_x(tp_handle, true); // 镜像 X 坐标
        esp_lcd_touch_set_mirror_y(tp_handle, true); // 镜像 Y 坐标
#elif EXAMPLE_LVGL_PORT_ROTATION_270
        esp_lcd_touch_set_swap_xy(tp_handle, true); // 交换 X 与 Y 坐标
        esp_lcd_touch_set_mirror_x(tp_handle, true); // 镜像 X 坐标
#endif
    }

    lvgl_mux = xSemaphoreCreateRecursiveMutex(); // 为 LVGL 创建递归互斥量
    assert(lvgl_mux); // 确保互斥量创建成功

    ESP_LOGI(TAG, "创建 LVGL 任务"); // 记录任务创建
    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE; // 确定任务所在的核 ID
    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                                             LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id); // 创建 LVGL 任务
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建 LVGL 任务失败"); // 任务创建失败时记录错误
        return ESP_FAIL; // 返回失败
    }

    return ESP_OK; // 返回成功
}

bool lvgl_port_lock(int timeout_ms)
{
    assert(lvgl_mux && "lvgl_port_init must be called first"); // 确保互斥量已初始化

    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms); // 将超时时间转换为时钟节拍
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE; // 尝试获取互斥量
}

void lvgl_port_unlock(void)
{
    assert(lvgl_mux && "lvgl_port_init must be called first"); // 确保互斥量已初始化
    xSemaphoreGiveRecursive(lvgl_mux); // 释放互斥量
}

bool lvgl_port_notify_rgb_vsync(void)
{
    BaseType_t need_yield = pdFALSE; // 判断是否需要让出 CPU 的标志
#if LVGL_PORT_FULL_REFRESH && (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 3) && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0)
    if (lvgl_port_rgb_next_buf != lvgl_port_rgb_last_buf) {
        lvgl_port_flush_next_buf = lvgl_port_rgb_last_buf; // 设置下一个刷新用缓冲
        lvgl_port_rgb_last_buf = lvgl_port_rgb_next_buf; // 更新最后一个缓冲
    }
#elif LVGL_PORT_AVOID_TEAR_ENABLE
    // 通知当前 RGB 帧缓冲已传输完成
    xTaskNotifyFromISR(lvgl_task_handle, ULONG_MAX, eNoAction, &need_yield); // 通知 LVGL 任务
#endif
    return (need_yield == pdTRUE); // 返回是否需要让出 CPU
}
