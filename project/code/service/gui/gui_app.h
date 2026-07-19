/**
 * @file gui_app.h
 * @brief MujicaUI 应用入口：菜单树 + 按键 + 周期刷新
 *
 * 依赖：event / soft_timer 已 init；SPI0 屏脚空闲（WiFi SPI 暂关）。
 * 按键：主板 A30/A31/B0/B1（见 docs/hardware.md §1 / §4.3）
 *
 * 启动顺序要求：全部业务 param_add（及可选 param_load）完成后再调 gui_app_init，
 * 以便按参数注册表动态生成 Params 分组页。
 */
#ifndef __GUI_APP_H__
#define __GUI_APP_H__

#include "common/tools/common_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 IPS200 菜单 UI、板载按键，并注册 5ms/100ms soft_timer
 * @note  会根据当前 param 注册表动态构建 Params 子菜单（按 name 前缀分组）
 * @return EXIT_OK 成功；否则失败（屏/按键/定时器/参数页）
 */
exit_code_t gui_app_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __GUI_APP_H__ */
