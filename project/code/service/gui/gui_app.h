/**
 * @file gui_app.h
 * @brief MujicaUI 应用入口：菜单树 + 按键 + 周期刷新
 *
 * 依赖：event / soft_timer 已 init；SPI0 屏脚空闲（WiFi SPI 暂关）。
 * 按键：主板 A30/A31/B0/B1（见 docs/hardware.md §1 / §4.3）
 */
#ifndef __GUI_APP_H__
#define __GUI_APP_H__

#include "common/tools/common_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 IPS200 菜单 UI、板载按键，并注册 5ms/100ms soft_timer
 * @return EXIT_OK 成功；否则失败（屏/按键/定时器）
 */
exit_code_t gui_app_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __GUI_APP_H__ */
