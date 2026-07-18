/**
 * @file mjc_render.h
 * @brief Render helpers for MujicaUI Lite.
 */

#ifndef __MJC_RENDER_H
#define __MJC_RENDER_H

#include <stdint.h>
#include "mjc_define.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 渲染单个菜单项（左侧名称，右侧状态），支持选中高亮。
 * @param item  需要渲染的条目
 * @param y     绘制起始 Y 坐标（基于字体左上角）
 * @param selected 是否为选中项，用于应用高亮配色
 */
void mjc_render_item(const mjc_item_t* item, uint16_t y, uint8_t selected);

void mjc_render_page(const mjc_page_t* page);

// 初始化显示驱动（HAL）并清屏，返回 1 成功 / 0 失败
uint8_t mjc_render_init(void);

#ifdef __cplusplus
}
#endif

#endif // __MJC_RENDER_H
