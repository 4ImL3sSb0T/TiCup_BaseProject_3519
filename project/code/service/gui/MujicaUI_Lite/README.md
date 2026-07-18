# MujicaUI_Lite 使用说明（数据结构 & 显示 HAL）

## 菜单数据结构（静态页面方式）
- 页面：`mjc_page_t { mjc_item_t* items; uint8_t count; mjc_page_t* parent_page; }`
- 条目：`mjc_item_t { name, type, union{checkbox/number/submenu}, parent(page*), action }`
- 关系：子菜单条目通过 `data.submenu -> mjc_page_t` 指向子页面；返回时可用 `current_page->parent_page` 找到上级。

```mermaid
graph TD
    RP[Root Page] --> RI0["items[0]: LABEL"]
    RP --> RI1["items[1]: SUBMENU → sub_page_A"]
    SPA[sub_page_A] --> SA0["items[0]: LABEL"]
    SPA --> SA1["items[1]: SUBMENU → sub_page_B"]
    SPB[sub_page_B] --> SB0["items[0]: LABEL"]

    RI1 --> SPA
    SA1 --> SPB

    SPA -. parent_page .-> RI1
    SPB -. parent_page .-> SA1
```

### 构建示例
```c
// 页面条目
static mjc_item_t root_items[] = {
    { "Title", MJC_ITEM_TYPE_LABEL },
    { "Go Sub", MJC_ITEM_TYPE_SUBMENU },
};
static mjc_page_t root_page = { root_items, 2, 0, NULL };

static mjc_item_t sub_items[] = {
    { "Sub Item", MJC_ITEM_TYPE_LABEL },
};
static mjc_page_t sub_page = { sub_items, 1, 0, NULL };

int main(void) {
    mjc_init(&root_page);                        // 设置根页面
    mjc_add_submenu(&root_page, &root_items[1], &sub_page);  // 绑定子页面
    // 后续在导航逻辑中切换 current_page 指针进入/返回子菜单
}
```

**现状与提醒**
- 现有状态变量：`g_current_page` + `g_selected_index`（全局）。未为每个页面保存独立的选中索引，可按需加“返回栈/每页选中状态”。
- 所有页面/条目为静态数组，无动态内存，适合多级嵌套菜单。

## 显示 HAL 概览
- `mjc_hal` 为 UI 提供屏幕驱动抽象，可在 IPS200、IPS114 之间切换，并可挂接自定义驱动。
- 自动根据方向配置宽高（`mjc_hal_screen_width/height`），内部绘制已做越界裁剪，避免底层断言。

### 快速使用（IPS200 或 IPS114）
```c
#include "mjc_hal.h"
#include "mjc_config.h"

int main(void) {
    mjc_hal_config_t cfg = mjc_hal_get_default_config(); // 默认 IPS200, 横屏 320x240, SPI
    // 如需 IPS114 竖屏 240x135:
    // cfg = MJC_HAL_CONFIG_IPS114_DEFAULT;
    // cfg.display_dir = MJC_DIR_LANDSCAPE; // 改为横屏可选

    if (!mjc_hal_init(&cfg)) {
        // init fail
    }

    const mjc_hal_driver_t* drv = mjc_hal_get_driver();
    drv->fill(0x0000);                     // 清空背景色
    drv->draw_string(0, 0, "Hello");       // 绘制文本
    drv->draw_rect(10, 10, 50, 30, 0xFFFF);
    return 0;
}
```

### HAL API 速览
- 初始化与切换：`mjc_hal_init(cfg)`，`mjc_hal_use_driver(custom)`，`mjc_hal_get_driver()`
- 尺寸：`mjc_hal_screen_width()`，`mjc_hal_screen_height()`
- 绘制接口（在 `mjc_hal_driver_t` 中）：`clear` / `fill` / `set_color` / `draw_point` / `draw_line` / `draw_rect` / `fill_rect` / `draw_char` / `draw_string` / `set_font_size`

```mermaid
flowchart LR
    CFG[mjc_hal_config_t<br/>driver_type/dir/size] --> INIT[mjc_hal_init]
    INIT -->|bind| DRV[mjc_hal_driver_t<br/>width/height/ops]
    DRV --> APP[UI层调用<br/>draw_xxx/set_color]
    DRV --> HW[IPS200/IPS114<br/>或自定义驱动]
    APP -.获取尺寸.-> W[mjc_hal_screen_width/height]
```

### 自定义驱动接入
1. 自行实现一个静态 `mjc_hal_driver_t`，填入 `width/height`、颜色字段和函数指针。
2. 将指针放入 `cfg.custom_driver`，`cfg.driver_type = MJC_DRIVER_CUSTOM`，然后调用 `mjc_hal_init(&cfg)`；或直接 `mjc_hal_use_driver(custom_drv)`。

### 方向与分辨率提示
- IPS200：`MJC_DIR_LANDSCAPE` -> 320x240，`MJC_DIR_PORTRAIT` -> 240x320。
- IPS114：`MJC_DIR_PORTRAIT` -> 240x135（默认），`MJC_DIR_LANDSCAPE` -> 135x240。
- 更改方向请在 `mjc_hal_init` 配置中设置 `display_dir`。驱动内部会同步更新 `width/height`。

## 按键输入（multi_button + 事件系统）
- 新增按键适配位于 [project/code/module/gui/MujicaUI_Lite/mjc_input_button.c](project/code/module/gui/MujicaUI_Lite/mjc_input_button.c)，默认使用 C12/C13/C14/C15 四个按键，分别对应 上/下/确认/返回。
- multi_button 事件通过 `event_publish_ex` 投递，适配层订阅 `BUTTON_EVENT_BASE + BTN_*`，并在回调中调用 `mjc_page_up/down`、进入子菜单或触发选中项。

### 快速接入
1. 先初始化事件系统与 UI：`event_init(); mjc_init(&root_page);`
2. 调用 `mjc_buttons_init();`（内部会将 C12~C15 配置为上拉输入并注册 multi_button 句柄）。
3. 在 5ms 周期（与 `TICKS_INTERVAL` 一致）调用 `mjc_buttons_tick_5ms();`；若 `BUTTON_EVENT_DISPATCH_MODE` 为异步，再在主循环调用 `event_process_async();`。
4. 进入子菜单时自动压栈当前页面；返回按键会出栈恢复上一页面；确认键对非子菜单项调用 `mjc_item_execute(..., MJC_EVENT_TRIGGER)`。
