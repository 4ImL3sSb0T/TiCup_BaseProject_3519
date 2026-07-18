/**
 * @file event.c
 * @brief 轻量级事件系统实现
 *
 * @note 静态内存模式：使用固定大小的数组
 * @note 动态内存模式：使用链表动态增长，无大小限制
 */

#include "event.h"
#include "service/sys/sys_time.h"
#include "service/sys/sys_log.h"
#include "zf_common_interrupt.h"
#include "zf_common_typedef.h"
#if EVENT_ENABLE_ASYNC
#include "zf_common_fifo.h"
#endif
#include <string.h>
#include <stdlib.h>

// ============================================================================
// 临界区保护宏
// ============================================================================

#if EVENT_INTERRUPT_SAFE
    #define EVENT_ENTER_CRITICAL()  uint32 primask = interrupt_global_disable()
    #define EVENT_EXIT_CRITICAL()   interrupt_global_enable(primask)
#else
    #define EVENT_ENTER_CRITICAL()  do {} while(0)
    #define EVENT_EXIT_CRITICAL()   do {} while(0)
#endif

// ============================================================================
// 内存管理
// ============================================================================

#if EVENT_USE_DYNAMIC_MEMORY
// 动态内存模式：链表
static event_listener_t *listener_head = NULL;  // 链表头指针
#else
// 静态内存模式：数组
static event_listener_t listeners[EVENT_MAX_LISTENERS];
#endif

#if EVENT_ENABLE_ASYNC
typedef struct {
    event_t event;
} event_queue_item_t;

static fifo_struct event_queue;
static event_queue_item_t event_queue_buffer[EVENT_ASYNC_QUEUE_SIZE];
#endif

// 统计信息
static event_stats_t stats = {0};

// ============================================================================
// 性能统计支持（与 soft_timer 模块集成）
// ============================================================================

#if TIMER_ENABLE_PERF_STATS
// 前向声明：soft_timer 模块提供的性能记录函数
extern void soft_timer_record_perf(uintptr_t timer_id, uint32_t exec_cycles);
#endif

// ============================================================================
// 内部函数声明
// ============================================================================
static uint16_t event_dispatch_sync(const event_t *event);

// ============================================================================
// 初始化和释放
// ============================================================================

/**
 * @brief 初始化事件系统
 */
void event_init(void)
{
    sys_log_text(info, "Event: Initializing event system...");
    EVENT_ENTER_CRITICAL();

#if EVENT_USE_DYNAMIC_MEMORY
    // 动态内存模式：初始化链表头
    listener_head = NULL;
    sys_log_text(info, "Event: Using dynamic memory mode (linked list)");
#else
    // 静态内存模式：清空数组
    memset(listeners, 0, sizeof(listeners));
    sys_log_text(info, "Event: Using static memory mode (max_listeners=%d)", EVENT_MAX_LISTENERS);
#endif
#if EVENT_ENABLE_ASYNC
    fifo_init(&event_queue, FIFO_DATA_8BIT, event_queue_buffer, sizeof(event_queue_buffer));
    sys_log_text(info, "Event: Async mode enabled (queue_size=%d)", EVENT_ASYNC_QUEUE_SIZE);
#endif

    memset(&stats, 0, sizeof(stats));
    EVENT_EXIT_CRITICAL();
    sys_log_text(info, "Event: Event system initialized successfully");
}

#if EVENT_USE_DYNAMIC_MEMORY
/**
 * @brief 释放事件系统资源（动态内存模式）
 */
void event_deinit(void)
{
    EVENT_ENTER_CRITICAL();

    // 释放所有链表节点
    event_listener_t *current = listener_head;
    while (current != NULL) {
        event_listener_t *next = current->next;
        free(current);
        current = next;
    }

    listener_head = NULL;
    memset(&stats, 0, sizeof(stats));

    EVENT_EXIT_CRITICAL();
}
#endif

// ============================================================================
// 订阅管理
// ============================================================================

/**
 * @brief 注册事件监听者
 */
event_listener_id_t event_subscribe(event_type_t type, event_callback_t callback, void *user_data)
{
    if (type == 0 || callback == NULL) {
        return EVENT_LISTENER_ID_INVALID;
    }

    EVENT_ENTER_CRITICAL();

#if EVENT_USE_DYNAMIC_MEMORY
    // 动态内存模式：分配新节点并添加到链表头部
    event_listener_t *new_node = (event_listener_t*)malloc(sizeof(event_listener_t));
    if (new_node == NULL) {
        EVENT_EXIT_CRITICAL();
        return EVENT_LISTENER_ID_INVALID;  // 内存分配失败
    }

    new_node->type = type;
    new_node->callback = callback;
    new_node->user_data = user_data;
    new_node->enabled = true;
    new_node->next = listener_head;  // 插入到链表头部
    listener_head = new_node;

    stats.active_listeners++;

    EVENT_EXIT_CRITICAL();
    return (event_listener_id_t)new_node;

#else
    // 静态内存模式：查找空闲槽位
    event_listener_id_t result = EVENT_LISTENER_ID_INVALID;
    for (int i = 0; i < EVENT_MAX_LISTENERS; i++) {
        if (listeners[i].type == 0 && listeners[i].callback == NULL) {
            listeners[i].type = type;
            listeners[i].callback = callback;
            listeners[i].user_data = user_data;
            listeners[i].enabled = true;
            stats.active_listeners++;
            result = (event_listener_id_t)(i + 1);  // 编码为 index+1，0 表示无效
            break;
        }
    }

    EVENT_EXIT_CRITICAL();
    return result;
#endif
}

/**
 * @brief 取消事件监听
 */
bool event_unsubscribe(event_listener_id_t listener_id)
{
    if (listener_id == EVENT_LISTENER_ID_INVALID) {
        return false;
    }

    EVENT_ENTER_CRITICAL();

#if EVENT_USE_DYNAMIC_MEMORY
    // 动态内存模式：从链表中删除节点
    event_listener_t *target = (event_listener_t*)((uintptr_t)listener_id);
    event_listener_t *prev = NULL;
    event_listener_t *current = listener_head;

    while (current != NULL) {
        if (current == target) {
            // 找到目标节点，从链表中移除
            if (prev == NULL) {
                // 删除头节点
                listener_head = current->next;
            } else {
                // 删除中间或尾节点
                prev->next = current->next;
            }

            free(current);
            if (stats.active_listeners > 0) {
                stats.active_listeners--;
            }

            EVENT_EXIT_CRITICAL();
            return true;
        }

        prev = current;
        current = current->next;
    }

    EVENT_EXIT_CRITICAL();
    return false;  // 未找到

#else
    // 静态内存模式：清空槽位
    if (listener_id == EVENT_LISTENER_ID_INVALID) {
        EVENT_EXIT_CRITICAL();
        return false;
    }
    int index = (int)listener_id - 1;
    if (index < 0 || index >= EVENT_MAX_LISTENERS) {
        EVENT_EXIT_CRITICAL();
        return false;
    }

    bool result = false;
    if (listeners[index].callback != NULL) {
        memset(&listeners[index], 0, sizeof(event_listener_t));
        if (stats.active_listeners > 0) {
            stats.active_listeners--;
        }
        result = true;
    }

    EVENT_EXIT_CRITICAL();
    return result;
#endif
}

/**
 * @brief 启用/禁用监听者
 */
bool event_set_listener_enabled(event_listener_id_t listener_id, bool enabled)
{
    if (listener_id == EVENT_LISTENER_ID_INVALID) {
        return false;
    }

    EVENT_ENTER_CRITICAL();

#if EVENT_USE_DYNAMIC_MEMORY
    // 动态内存模式：遍历链表查找节点
    event_listener_t *target = (event_listener_t*)((uintptr_t)listener_id);
    event_listener_t *current = listener_head;

    while (current != NULL) {
        if (current == target) {
            current->enabled = enabled;
            EVENT_EXIT_CRITICAL();
            return true;
        }
        current = current->next;
    }

    EVENT_EXIT_CRITICAL();
    return false;

#else
    // 静态内存模式：直接访问数组
    int index = (int)listener_id - 1;
    if (index < 0 || index >= EVENT_MAX_LISTENERS) {
        EVENT_EXIT_CRITICAL();
        return false;
    }

    bool result = false;
    if (listeners[index].callback != NULL) {
        listeners[index].enabled = enabled;
        result = true;
    }

    EVENT_EXIT_CRITICAL();
    return result;
#endif
}

// ============================================================================
// 事件发布
// ============================================================================

uint16_t event_publish_ex(event_type_t type, void *data, uint16_t data_size, event_dispatch_mode_t mode)
{
    if (type == 0) {
        return 0;
    }

    // 构建事件
    event_t event;
    event.type = type;
    event.data = data;
    event.data_size = data_size;
    event.timestamp = sys_time_get_ms();

    if (mode == EVENT_DISPATCH_ASYNC) {
#if EVENT_ENABLE_ASYNC
        event_queue_item_t item;
        item.event = event;

        EVENT_ENTER_CRITICAL();
        fifo_state_enum state = fifo_write_buffer(&event_queue, &item, sizeof(item));
        if (state == FIFO_SUCCESS) {
            stats.total_events++;
        }
        EVENT_EXIT_CRITICAL();

        return (state == FIFO_SUCCESS) ? 1 : 0;
#else
        mode = EVENT_DISPATCH_SYNC;  // 异步未启用，退化为同步
#endif
    }

    // 同步分发
    uint16_t dispatch_count = event_dispatch_sync(&event);

    EVENT_ENTER_CRITICAL();
    stats.total_events++;
    EVENT_EXIT_CRITICAL();

    return dispatch_count;
}

#if TIMER_ENABLE_PERF_STATS
/**
 * @brief 发布带性能统计的事件（由 soft_timer 模块调用）
 * @param type 事件类型
 * @param data 事件数据指针
 * @param data_size 数据大小
 * @param mode 发布模式（同步/异步）
 * @param timer_id 触发此事件的定时器 ID
 * @param enqueue_cycles 入队时的 DWT 周期数
 * @return 同步：回调触发数量；异步：入队成功返回1，失败返回0
 */
uint16_t event_publish_with_stats(event_type_t type, void *data,
                                   uint16_t data_size, event_dispatch_mode_t mode,
                                   uintptr_t timer_id, uint32_t enqueue_cycles)
{
    if (type == 0) {
        return 0;
    }

    // 构建事件（包含性能统计字段）
    event_t event;
    event.type = type;
    event.data = data;
    event.data_size = data_size;
    event.timestamp = sys_time_get_ms();
    event.timer_id = timer_id;
    event.enqueue_cycles = enqueue_cycles;

    if (mode == EVENT_DISPATCH_ASYNC) {
#if EVENT_ENABLE_ASYNC
        event_queue_item_t item;
        item.event = event;

        EVENT_ENTER_CRITICAL();
        fifo_state_enum state = fifo_write_buffer(&event_queue, &item, sizeof(item));
        if (state == FIFO_SUCCESS) {
            stats.total_events++;
        }
        EVENT_EXIT_CRITICAL();

        return (state == FIFO_SUCCESS) ? 1 : 0;
#else
        mode = EVENT_DISPATCH_SYNC;  // 异步未启用，退化为同步
#endif
    }

    // 同步分发
    uint16_t dispatch_count = event_dispatch_sync(&event);

    EVENT_ENTER_CRITICAL();
    stats.total_events++;
    EVENT_EXIT_CRITICAL();

    return dispatch_count;
}
#endif

/**
 * @brief 发布事件
 */
uint16_t event_publish(event_type_t type, void *data, uint16_t data_size)
{
    return event_publish_ex(type, data, data_size, EVENT_DISPATCH_SYNC);
}

/**
 * @brief 发布事件（异步入队）
 */
uint16_t event_publish_async(event_type_t type, void *data, uint16_t data_size)
{
    return event_publish_ex(type, data, data_size, EVENT_DISPATCH_ASYNC);
}

/**
 * @brief 发布事件（不带数据）
 */
uint16_t event_publish_simple(event_type_t type)
{
    return event_publish(type, NULL, 0);
}

// ============================================================================
// 批量操作
// ============================================================================

/**
 * @brief 取消注册所有指定类型的监听者
 */
uint16_t event_unsubscribe_all(event_type_t type)
{
    if (type == 0) {
        return 0;
    }

    uint16_t count = 0;

    EVENT_ENTER_CRITICAL();

#if EVENT_USE_DYNAMIC_MEMORY
    // 动态内存模式：遍历链表删除匹配节点
    event_listener_t *prev = NULL;
    event_listener_t *current = listener_head;

    while (current != NULL) {
        event_listener_t *next = current->next;

        if (current->type == type) {
            // 删除匹配的节点
            if (prev == NULL) {
                listener_head = next;
            } else {
                prev->next = next;
            }

            free(current);
            count++;
            if (stats.active_listeners > 0) {
                stats.active_listeners--;
            }

            current = next;
        } else {
            prev = current;
            current = next;
        }
    }

#else
    // 静态内存模式：清空匹配的槽位
    for (int i = 0; i < EVENT_MAX_LISTENERS; i++) {
        if (listeners[i].type == type) {
            memset(&listeners[i], 0, sizeof(event_listener_t));
            count++;
            if (stats.active_listeners > 0) {
                stats.active_listeners--;
            }
        }
    }
#endif

    EVENT_EXIT_CRITICAL();
    return count;
}

/**
 * @brief 清空所有监听者
 */
void event_clear_all(void)
{
    EVENT_ENTER_CRITICAL();

#if EVENT_USE_DYNAMIC_MEMORY
    // 动态内存模式：释放所有节点
    event_listener_t *current = listener_head;
    while (current != NULL) {
        event_listener_t *next = current->next;
        free(current);
        current = next;
    }
    listener_head = NULL;

#else
    // 静态内存模式：清空数组
    memset(listeners, 0, sizeof(listeners));
#endif

    stats.active_listeners = 0;
    EVENT_EXIT_CRITICAL();
}

// ============================================================================
// 查询接口
// ============================================================================

/**
 * @brief 获取事件系统统计信息
 */
void event_get_stats(event_stats_t *stats_out)
{
    if (stats_out != NULL) {
        EVENT_ENTER_CRITICAL();
        memcpy(stats_out, &stats, sizeof(event_stats_t));
        EVENT_EXIT_CRITICAL();
    }
}

/**
 * @brief 获取活跃监听者数量
 */
uint16_t event_get_listener_count(void)
{
    EVENT_ENTER_CRITICAL();
    uint16_t count = stats.active_listeners;
    EVENT_EXIT_CRITICAL();
    return count;
}

/**
 * @brief 处理异步事件队列
 */
uint16_t event_process_async(void)
{
    // static uint32 last_process_time = 0;
    // if (sys_time_get_ms() - last_process_time < EVENT_ASYNC_PROCESS_INTERVAL_MS) {
    //     return 0;  // 距离上次处理时间不足，跳过
    // }
    // last_process_time = sys_time_get_ms();
    
#if EVENT_ENABLE_ASYNC
    uint16_t total_dispatch = 0;

    while (1) {
        event_queue_item_t item;
        uint32 length = sizeof(item);

        uint32 lock = interrupt_global_disable();
        fifo_state_enum state = fifo_read_buffer(&event_queue, &item, &length, FIFO_READ_AND_CLEAN);
        interrupt_global_enable(lock);

        if (state != FIFO_SUCCESS || length != sizeof(item)) {
            break;
        }

        total_dispatch += event_dispatch_sync(&item.event);
    }

    return total_dispatch;
#else
    return 0;
#endif
}

// ============================================================================
// 内部函数
// ============================================================================
static uint16_t event_dispatch_sync(const event_t *event)
{
    uint16_t dispatch_count = 0;

#if EVENT_USE_DYNAMIC_MEMORY
    // 动态内存模式：遍历链表
    event_listener_t *last = NULL;

    while (1) {
        event_listener_t *selected = NULL;
        event_callback_t callback = NULL;
        void *user_data = NULL;

        uint32 lock = interrupt_global_disable();

        // 确定本次扫描的起点：上一次处理的节点之后；若节点已被删除则从头开始
        event_listener_t *start = listener_head;
        if (last != NULL) {
            event_listener_t *search = listener_head;
            while (search != NULL && search != last) {
                search = search->next;
            }
            if (search != NULL) {
                start = search->next;
            }
        }

        // 找到下一个匹配的监听者
        while (start != NULL) {
            if (start->type == event->type && start->enabled && start->callback != NULL) {
                selected = start;
                callback = start->callback;
                user_data = start->user_data;
                break;
            }
            start = start->next;
        }

        interrupt_global_enable(lock);

        if (selected == NULL) {
            break;  // 无更多匹配监听者
        }

        // 在临界区外调用回调，避免长时间关闭中断
#if TIMER_ENABLE_PERF_STATS
        uint32_t callback_start = DWT_GET_CYCLES();
        callback(event, user_data);
        uint32_t callback_end = DWT_GET_CYCLES();

        // 如果是定时器事件，记录性能统计
        if (event->timer_id != 0) {
            soft_timer_record_perf(event->timer_id, callback_end - callback_start);
        }
#else
        callback(event, user_data);
#endif

        uint32 lock_stats = interrupt_global_disable();
        dispatch_count++;
        stats.total_dispatches++;
        last = selected;  // 记录已处理节点的地址，用于下一次起点
        interrupt_global_enable(lock_stats);
    }

#else
    // 静态内存模式：遍历数组
    uint32 lock = interrupt_global_disable();

    for (int i = 0; i < EVENT_MAX_LISTENERS; i++) {
        if (listeners[i].type == event->type && listeners[i].enabled && listeners[i].callback != NULL) {
            event_callback_t callback = listeners[i].callback;
            void *user_data = listeners[i].user_data;

            interrupt_global_enable(lock);

#if TIMER_ENABLE_PERF_STATS
            uint32_t callback_start = DWT_GET_CYCLES();
            callback(event, user_data);
            uint32_t callback_end = DWT_GET_CYCLES();

            // 如果是定时器事件，记录性能统计
            if (event->timer_id != 0) {
                soft_timer_record_perf(event->timer_id, callback_end - callback_start);
            }
#else
            callback(event, user_data);
#endif

            lock = interrupt_global_disable();

            dispatch_count++;
            stats.total_dispatches++;
        }
    }

    interrupt_global_enable(lock);
#endif

    return dispatch_count;
}
