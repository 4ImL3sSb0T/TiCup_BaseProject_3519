#ifndef __MJC_CONFIG_H
#define __MJC_CONFIG_H

#include <stdint.h>
#include "zf_device_ips200.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mjc_hal_driver_t;

typedef enum {
    MJC_DRIVER_IPS200,
    MJC_DRIVER_IPS114,
    MJC_DRIVER_CUSTOM
} mjc_driver_type_t;

typedef enum {
    MJC_DIR_AUTO = 0,
    MJC_DIR_PORTRAIT,
    MJC_DIR_PORTRAIT_180,
    MJC_DIR_LANDSCAPE,
    MJC_DIR_LANDSCAPE_180,
} mjc_display_dir_t;

typedef struct {
    mjc_driver_type_t driver_type;
    mjc_display_dir_t display_dir;
    ips200_type_enum ips200_bus;               // 仅在选择 IPS200 时使用
    struct mjc_hal_driver_t* custom_driver;    // 可选：用于接入自定义驱动
} mjc_hal_config_t;

#define MJC_HAL_CONFIG_IPS200_DEFAULT \
    { MJC_DRIVER_IPS200, MJC_DIR_PORTRAIT, IPS200_TYPE_SPI, NULL }

#define MJC_HAL_CONFIG_IPS114_DEFAULT \
    { MJC_DRIVER_IPS114, MJC_DIR_PORTRAIT, IPS200_TYPE_SPI, NULL }

// 渲染配置（集中管理 UI 相关常量）
#define MJC_RENDER_FONT_WIDTH    8
#define MJC_RENDER_FONT_HEIGHT   16
#define MJC_RENDER_PADDING_X     4
#define MJC_RENDER_GAP_X         8
#define MJC_RENDER_VALUE_BUF     32
#define MJC_RENDER_NAME_BUF      48
#define MJC_RENDER_COLOR_NORMAL_PEN     0xFFFF  // 默认文字颜色（白）
#define MJC_RENDER_COLOR_NORMAL_BG      0x0000  // 默认背景颜色（黑）
#define MJC_RENDER_COLOR_SELECTED_PEN   0x0000  // 选中项文字颜色（黑）
#define MJC_RENDER_COLOR_SELECTED_BG    0x07E0  // 选中项背景颜色（绿）

// 追踪 UI 变化用于增量渲染的最大条目数（超出则退回整页重绘）
#define MJC_RENDER_MAX_TRACKED_ITEMS    32

static inline mjc_hal_config_t mjc_hal_get_default_config(void) {
    const mjc_hal_config_t cfg = MJC_HAL_CONFIG_IPS200_DEFAULT;
    return cfg;
}

#ifdef __cplusplus
}
#endif

#endif // !__MJC_CONFIG_H
