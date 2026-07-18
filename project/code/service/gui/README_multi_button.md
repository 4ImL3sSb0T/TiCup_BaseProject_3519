# multi_button 模块使用说明

## 模块概览
- 提供单键去抖与状态机，支持单击、双击、长按、长按保持与重复按压等事件。
- 事件同时以回调形式触发，并按 `BUTTON_EVENT_BASE + event` 发布到通用事件系统，默认异步派发。
- 关键实现位于 [project/code/module/gui/multi_button.c](project/code/module/gui/multi_button.c) 与 [project/code/module/gui/multi_button.h](project/code/module/gui/multi_button.h)。

## 依赖与时间基准
- 依赖通用事件系统头文件 `common/event/event.h`（若未使用事件系统，可仅用回调）。
- 需要一个周期 `TICKS_INTERVAL=5ms` 的定时调用 `button_ticks()`，该周期与去抖/长按阈值计算直接相关。
- 需提供硬件电平读取函数 `uint8_t read_level(uint8_t id)`，返回 GPIO 当前电平。

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
multi_button 在触发每个按键事件时，会调用 `event_publish_ex(BUTTON_EVENT_BASE + ev, ...)` 将事件推送到通用事件系统，方便与其他模块解耦。使用步骤：

1) **初始化事件系统**：在系统启动时调用一次 `event_init();`。
2) **订阅感兴趣的按键事件**（例如单击）：
```c
static void on_btn_event(const event_t* ev, void* user)
{
    // ev->type == BUTTON_EVENT_BASE + BTN_SINGLE_CLICK 等
    // ev->data 指向 Button*，data_size 为 sizeof(Button*)
}

void button_event_subscribe(void)
{
    event_subscribe((event_type_t)(BUTTON_EVENT_BASE + BTN_SINGLE_CLICK),
                    on_btn_event,
                    NULL);
}
```

3) **处理异步队列（若启用）**：当 `BUTTON_EVENT_DISPATCH_MODE` 为 `EVENT_DISPATCH_ASYNC` 时，需要在主循环或周期任务中调用 `event_process_async();`，频率与业务实时性匹配即可。

4) **发布模式选择**：
- `EVENT_DISPATCH_SYNC`：`event_publish_ex` 会立即在当前上下文执行回调，适合快速处理、非中断场景。
- `EVENT_DISPATCH_ASYNC`：事件入队后由 `event_process_async` 拉取并触发回调，适合中断或需要解耦耗时逻辑的场景。

5) **常用 API 速览**（详情见 [project/code/common/event/event.h](project/code/common/event/event.h)）：
- `event_subscribe(type, cb, user_data)` / `event_unsubscribe(id)`：注册或取消监听。
- `event_publish(type, data, size)`：同步发布。
- `event_publish_async(type, data, size)`：异步入队发布。
- `event_publish_ex(type, data, size, mode)`：按需选择同步/异步。
- `event_process_async()`：处理异步队列。

6) **与 multi_button 搭配建议**：
- 若在中断中调用 `button_ticks()`，保持 `BUTTON_EVENT_DISPATCH_MODE` 为异步，避免在中断中执行用户回调；主循环调用 `event_process_async()` 完成分发。
- 若在任务/主循环中调用 `button_ticks()`，且回调足够轻量，可将 `BUTTON_EVENT_DISPATCH_MODE` 设为同步以减少延迟。

7) **生命周期与数据有效性**：按钮事件携带的 `data` 指针为 `Button*`（静态存活），无需额外拷贝；若发布自定义事件且使用异步模式，需保证 `data` 在事件出队前有效。