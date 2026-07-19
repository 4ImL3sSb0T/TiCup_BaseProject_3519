# multi_button 模块使用说明

## 模块概览
- 提供单键去抖与状态机，支持单击、双击、长按、长按保持与重复按压等事件。
- 事件同时以 **本地回调** `button_attach` 与 **全局事件** 两种方式输出。
- 全局：`event_publish_ex(BUTTON_EVENT_BASE + ButtonEvent, Button* handle, …, BUTTON_EVENT_DISPATCH_MODE)`  
  - `BUTTON_EVENT_BASE = 0x0200`（**不要**用 0x0100，与 soft_timer 自动事件号冲突）  
  - 默认 `BUTTON_EVENT_DISPATCH_MODE = EVENT_DISPATCH_ASYNC`  
  - `data` 为静态存活的 `Button*`（异步队列只存指针）
- 关键路径：`project/code/service/gui/multi_button.c` / `multi_button.h`

## 依赖与时间基准
- 依赖 `common/event/event.h`（发布全局事件时需要；也可只用 `button_attach` 回调）。
- 需要周期 `TICKS_INTERVAL=5ms` 调用 `button_ticks()`。
- 需提供 `uint8_t read_level(uint8_t id)` 读 GPIO。
- 异步模式下主循环需 `event_process_async()`。

## 快速接入步骤
1) **定义硬件读脚函数**（根据实际 GPIO 读写封装）：
```c
static uint8_t read_button_level(uint8_t id)
{
    /* 返回指定按键的电平，0/1 对应实际 GPIO 电平 */
    return gpio_read(id);
}
```
2) **声明按键句柄**（可静态定义多个）：
```c
static Button btn_user;
```
3) **初始化并配置有效电平/ID**：
```c
button_init(&btn_user, read_button_level, /*active_level=*/0, /*button_id=*/0);
```
`active_level` 设为按下时的电平（上拉输入通常为 0，若下拉则设 1）。

4) **注册需要的事件回调**（可按需选择）：
```c
static void on_single_click(Button* btn) { /* TODO */ }
static void on_long_start(Button* btn)  { /* TODO */ }

button_attach(&btn_user, BTN_SINGLE_CLICK, on_single_click);
button_attach(&btn_user, BTN_LONG_PRESS_START, on_long_start);
```

5) **开始工作**：在系统初始化阶段调用一次 `button_start(&btn_user);`。

6) **周期调用调度函数**：在 5ms 定时中断、RTOS 定时任务或主循环中调用 `button_ticks();`，保证调用周期与 `TICKS_INTERVAL` 一致。

7) **可选停止/复位**：`button_stop(&btn_user);` 停止扫描；`button_reset(&btn_user);` 清空状态。

## 事件列表
- `BTN_PRESS_DOWN`：按下沿。
- `BTN_PRESS_UP`：抬起沿。
- `BTN_PRESS_REPEAT`：重复按压计数自增（在双击/多击窗口内）。
- `BTN_SINGLE_CLICK`：单击确认。
- `BTN_DOUBLE_CLICK`：双击确认。
- `BTN_LONG_PRESS_START`：长按阈值达到时触发。
- `BTN_LONG_PRESS_HOLD`：长按保持期间周期触发。

## 参数与行为说明
- 去抖深度 `DEBOUNCE_TICKS`、短按阈值 `SHORT_TICKS`、长按阈值 `LONG_TICKS` 由头文件宏定义；若调整 `TICKS_INTERVAL`，请同步修改这些阈值或其计算公式。
- `PRESS_REPEAT_MAX_NUM` 限制重复计数上限（默认 15）。
- 事件发布：类型号为 `BUTTON_EVENT_BASE + ev`，派发模式由 `BUTTON_EVENT_DISPATCH_MODE` 控制（默认 `EVENT_DISPATCH_ASYNC`）。
- 辅助接口：`button_get_event()` 获取最近事件；`button_get_repeat_count()` 查询重复次数；`button_is_pressed()` 判断当前按键电平是否处于按下态。

## 最小可用示例
```c
static Button btn_user;

static uint8_t read_button_level(uint8_t id)
{
    return gpio_read(id); // 根据平台实现
}

static void on_single_click(Button* btn)
{
    // 单击处理，例如切换模式或发消息
}

void button_app_init(void)
{
    button_init(&btn_user, read_button_level, 0, 0);
    button_attach(&btn_user, BTN_SINGLE_CLICK, on_single_click);
    button_start(&btn_user);
}

// 例如在 5ms Tick 中断或任务中调用
void button_poll_5ms(void)
{
    button_ticks();
}
```
按需为不同按键定义多个 `Button` 句柄并重复上述流程即可。

## 事件系统集成说明

`button_emit` 会对每个 `ButtonEvent` 调用：

```c
event_publish_ex(
    (event_type_t)(BUTTON_EVENT_BASE + ev),
    handle,                    /* Button*，须静态或生命周期覆盖异步出队 */
    (uint16_t)sizeof(Button),
    BUTTON_EVENT_DISPATCH_MODE);
```

1) **初始化**：`event_init();`
2) **订阅**：
```c
static void on_btn_event(const event_t* ev, void* user)
{
    (void)user;
    Button* btn = (Button*)ev->data;
    ButtonEvent bev = (ButtonEvent)(ev->type - BUTTON_EVENT_BASE);
    /* btn->button_id, bev */
}

event_subscribe((event_type_t)(BUTTON_EVENT_BASE + BTN_SINGLE_CLICK),
                on_btn_event, NULL);
```
3) **异步**：默认 ASYNC → 主循环 `event_process_async()`。
4) **与 MujicaUI**：菜单默认 handler 在 `mjc_input_button`；业务页可 `mjc_input_set_enabled(0)` 后自行订阅，避免双处理。
5) **生命周期**：`data` 为 `Button*` 且对象须在出队前有效（列表内静态句柄即可）。