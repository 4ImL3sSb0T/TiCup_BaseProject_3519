/**
 * @file soft_timer.c
 * @brief 软件定时器实现 - 基于事件系统
 */

#include "soft_timer.h"
#include "service/sys/sys_time.h"
#include "service/sys/sys_log.h"
#include "zf_common_interrupt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// 临界区保护宏
// ============================================================================

#if TIMER_INTERRUPT_SAFE
    #define TIMER_ENTER_CRITICAL()  uint32 primask = interrupt_global_disable(); (void)primask
    #define TIMER_EXIT_CRITICAL()   interrupt_global_enable(primask)
#else
    #define TIMER_ENTER_CRITICAL()  do {} while(0)
    #define TIMER_EXIT_CRITICAL()   do {} while(0)
#endif

// ============================================================================
// 内存管理
// ============================================================================

#if TIMER_USE_DYNAMIC_MEMORY
// 动态内存模式：链表
static soft_timer_t *timer_head = NULL;
#else
// 静态内存模式：数组
static soft_timer_t timers[SOFT_TIMER_MAX_COUNT];
#endif

// 统计信息
static timer_stats_t stats = {0};

// 自动分配事件类型计数器
static event_type_t next_auto_event_type = TIMER_EVENT_BASE;

// ============================================================================
// 定时器系统性能统计
// ============================================================================
#if TIMER_ENABLE_PERF_STATS
typedef struct {
    uint32_t total_process_cycles;     // soft_timer_process 总周期数
    uint32_t total_scan_cycles;        // 扫描定时器的总周期数
    uint32_t total_publish_cycles;     // 事件发布的总周期数
    uint32_t process_count;            // process 调用次数
    uint32_t scan_count;               // 扫描次数
    uint32_t publish_count;            // 发布次数
    uint32_t max_process_cycles;       // 单次 process 最大周期
    uint32_t max_scan_cycles;          // 单次扫描最大周期
    uint32_t max_publish_cycles;       // 单次发布最大周期
    uint32_t last_print_time;          // 上次打印时间
} timer_sys_perf_stats_t;

static timer_sys_perf_stats_t sys_perf_stats = {0};
#define PERF_PRINT_INTERVAL_MS 10000    // 每10000ms打印一次
#endif

#if TIMER_ENABLE_PERF_STATS
// 前向声明：事件系统内部函数，用于发布带性能统计的事件
extern uint16_t event_publish_with_stats(event_type_t type, void *data,
                                         uint16_t data_size, event_dispatch_mode_t mode,
                                         uintptr_t timer_id, uint32_t enqueue_cycles);
#endif

// ============================================================================
// 初始化和释放
// ============================================================================

/**
 * @brief 初始化软件定时器系统
 */
void soft_timer_init(void)
{
    TIMER_ENTER_CRITICAL();

#if TIMER_USE_DYNAMIC_MEMORY
    // 动态内存模式：初始化链表头
    timer_head = NULL;
#else
    // 静态内存模式：清空数组
    memset(timers, 0, sizeof(timers));
#endif

    memset(&stats, 0, sizeof(stats));
    next_auto_event_type = TIMER_EVENT_BASE;
    TIMER_EXIT_CRITICAL();
}

#if TIMER_USE_DYNAMIC_MEMORY
/**
 * @brief 释放定时器系统资源（动态内存模式）
 */
void soft_timer_deinit(void)
{
    TIMER_ENTER_CRITICAL();

    // 释放所有链表节点
    soft_timer_t *current = timer_head;
    while (current != NULL) {
        soft_timer_t *next = current->next;
        if (current->listener_id != EVENT_LISTENER_ID_INVALID) {
            event_unsubscribe(current->listener_id);
        }
        free(current);
        current = next;
    }

    timer_head = NULL;
    memset(&stats, 0, sizeof(stats));

    TIMER_EXIT_CRITICAL();
}
#endif

// ============================================================================
// 定时器创建
// ============================================================================

/**
 * @brief 创建定时器
 */
soft_timer_id_t soft_timer_create(uint32_t interval, timer_mode_t mode,
                                  event_type_t event_type, void *user_data,
                                  uint16_t user_data_size,
                                  event_callback_t callback,
                                  event_dispatch_mode_t dispatch_mode)
{
    if (interval == 0 || event_type == 0) {
        return 0;
    }

    // 自动分配事件类型（和 simple 版本一样）
    if (event_type == TIMER_EVENT_AUTO) {
        TIMER_ENTER_CRITICAL();

        // 检查事件类型是否超出范围
        if (next_auto_event_type > TIMER_EVENT_AUTO_MAX) {
            TIMER_EXIT_CRITICAL();
            return 0;  // 事件类型已用尽
        }

        // 分配唯一的事件类型
        event_type = next_auto_event_type++;

        TIMER_EXIT_CRITICAL();
    }

    TIMER_ENTER_CRITICAL();

#if TIMER_USE_DYNAMIC_MEMORY
    // 动态内存模式：分配新节点并添加到链表头部
    soft_timer_t *new_node = (soft_timer_t*)malloc(sizeof(soft_timer_t));
    if (new_node == NULL) {
        TIMER_EXIT_CRITICAL();
        return 0;  // 内存分配失败
    }

    new_node->mode = mode;
    new_node->state = TIMER_STATE_STOPPED;
    new_node->interval = interval;
    new_node->start_time = 0;
    new_node->elapsed = 0;
    new_node->event_type = event_type;
    new_node->user_data = user_data;
    new_node->user_data_size = (user_data_size == 0) ? sizeof(void*) : user_data_size;
    new_node->dispatch_mode = dispatch_mode;
    new_node->id = (soft_timer_id_t)new_node;
    new_node->listener_id = EVENT_LISTENER_ID_INVALID;
    new_node->name[0] = '\0';  // 默认空名称
#if TIMER_ENABLE_PERF_STATS
    new_node->perf_total_cycles = 0;
    new_node->perf_exec_count = 0;
    new_node->perf_max_cycles = 0;
    new_node->perf_min_cycles = 0;
#endif
    new_node->next = timer_head;  // 插入到链表头部
    timer_head = new_node;

    if (callback != NULL) {
        event_listener_id_t listener_id = event_subscribe(event_type, callback, user_data);
        if (listener_id == EVENT_LISTENER_ID_INVALID) {
            timer_head = new_node->next;
            free(new_node);
            TIMER_EXIT_CRITICAL();
            return 0;
        }
        new_node->listener_id = listener_id;
    }

    TIMER_EXIT_CRITICAL();
    return new_node->id;

#else
    // 静态内存模式：查找空闲槽位
    soft_timer_id_t result = 0;
    for (uint8_t i = 0; i < SOFT_TIMER_MAX_COUNT; i++) {
        if (!timers[i].active) {
            timers[i].active = true;
            timers[i].mode = mode;
            timers[i].state = TIMER_STATE_STOPPED;
            timers[i].interval = interval;
            timers[i].start_time = 0;
            timers[i].elapsed = 0;
            timers[i].event_type = event_type;
            timers[i].user_data = user_data;
            timers[i].user_data_size = (user_data_size == 0) ? sizeof(void*) : user_data_size;
            timers[i].dispatch_mode = dispatch_mode;
            timers[i].id = (soft_timer_id_t)&timers[i];
            timers[i].listener_id = EVENT_LISTENER_ID_INVALID;
            timers[i].name[0] = '\0';  // 默认空名称
#if TIMER_ENABLE_PERF_STATS
            timers[i].perf_total_cycles = 0;
            timers[i].perf_exec_count = 0;
            timers[i].perf_max_cycles = 0;
            timers[i].perf_min_cycles = 0;
#endif

            if (callback != NULL) {
                event_listener_id_t listener_id = event_subscribe(event_type, callback, user_data);
                if (listener_id == EVENT_LISTENER_ID_INVALID) {
                    memset(&timers[i], 0, sizeof(soft_timer_t));
                    result = 0;
                    break;
                }
                timers[i].listener_id = listener_id;
            }

            result = timers[i].id;
            break;
        }
    }

    TIMER_EXIT_CRITICAL();
    return result;
#endif
}

/**
 * @brief 创建定时器（简化版，自动分配事件类型）
 */
soft_timer_id_t soft_timer_create_simple(uint32_t interval, timer_mode_t mode,
                                         event_callback_t callback,
                                         event_dispatch_mode_t dispatch_mode)
{
    if (callback == NULL) {
        return 0;  // 简化版必须提供回调函数
    }

    TIMER_ENTER_CRITICAL();

    // 检查事件类型是否超出范围
    if (next_auto_event_type > TIMER_EVENT_AUTO_MAX) {
        TIMER_EXIT_CRITICAL();
        return 0;  // 事件类型已用尽
    }

    // 分配唯一的事件类型
    event_type_t allocated_event_type = next_auto_event_type++;

    TIMER_EXIT_CRITICAL();

    // 调用完整版创建函数
    return soft_timer_create(interval, mode, allocated_event_type, NULL, 0,
                            callback, dispatch_mode);
}

// ============================================================================
// 辅助函数：查找定时器节点
// ============================================================================

static soft_timer_t* find_timer_by_id(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return NULL;
    }

#if TIMER_USE_DYNAMIC_MEMORY
    soft_timer_t *current = timer_head;
    while (current != NULL) {
        if ((soft_timer_id_t)current == timer_id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
#else
    for (uint8_t i = 0; i < SOFT_TIMER_MAX_COUNT; i++) {
        if (timers[i].active && (soft_timer_id_t)&timers[i] == timer_id) {
            return &timers[i];
        }
    }
    return NULL;
#endif
}

// ============================================================================
// 定时器控制
// ============================================================================

/**
 * @brief 启动定时器
 */
bool soft_timer_start(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    if (timer->state == TIMER_STATE_STOPPED) {
        timer->state = TIMER_STATE_RUNNING;
        timer->start_time = sys_time_get_ms();
        timer->elapsed = 0;
        stats.active_timers++;
    }

    TIMER_EXIT_CRITICAL();
    sys_log_text(info, "Started timer ID: %p", (void*)timer_id);
    return true;
}

/**
 * @brief 停止定时器
 */
bool soft_timer_stop(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    if (timer->state != TIMER_STATE_STOPPED) {
        timer->state = TIMER_STATE_STOPPED;
        timer->elapsed = 0;
        if (stats.active_timers > 0) {
            stats.active_timers--;
        }
    }

    TIMER_EXIT_CRITICAL();
    return true;
}

/**
 * @brief 暂停定时器
 */
bool soft_timer_pause(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    if (timer->state == TIMER_STATE_RUNNING) {
        uint32_t current_time = sys_time_get_ms();
        timer->elapsed += (current_time - timer->start_time);
        timer->state = TIMER_STATE_PAUSED;
    }

    TIMER_EXIT_CRITICAL();
    return true;
}

/**
 * @brief 恢复定时器
 */
bool soft_timer_resume(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    if (timer->state == TIMER_STATE_PAUSED) {
        timer->start_time = sys_time_get_ms();
        timer->state = TIMER_STATE_RUNNING;
    }

    TIMER_EXIT_CRITICAL();
    return true;
}

/**
 * @brief 重置定时器
 */
bool soft_timer_reset(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    if (timer->state == TIMER_STATE_RUNNING) {
        timer->start_time = sys_time_get_ms();
        timer->elapsed = 0;
    }

    TIMER_EXIT_CRITICAL();
    return true;
}

/**
 * @brief 删除定时器
 */
bool soft_timer_delete(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();

#if TIMER_USE_DYNAMIC_MEMORY
    // 动态内存模式：从链表中删除节点
    soft_timer_t *prev = NULL;
    soft_timer_t *current = timer_head;

    while (current != NULL) {
        if ((soft_timer_id_t)current == timer_id) {
            if (prev == NULL) {
                timer_head = current->next;
            } else {
                prev->next = current->next;
            }

            if (current->listener_id != EVENT_LISTENER_ID_INVALID) {
                event_unsubscribe(current->listener_id);
            }
            if (current->state != TIMER_STATE_STOPPED && stats.active_timers > 0) {
                stats.active_timers--;
            }

            free(current);
            TIMER_EXIT_CRITICAL();
            return true;
        }

        prev = current;
        current = current->next;
    }

    TIMER_EXIT_CRITICAL();
    return false;  // 未找到

#else
    bool result = false;
    for (uint8_t i = 0; i < SOFT_TIMER_MAX_COUNT; i++) {
        if (timers[i].active && (soft_timer_id_t)&timers[i] == timer_id) {
            if (timers[i].listener_id != EVENT_LISTENER_ID_INVALID) {
                event_unsubscribe(timers[i].listener_id);
            }
            if (timers[i].state != TIMER_STATE_STOPPED && stats.active_timers > 0) {
                stats.active_timers--;
            }
            memset(&timers[i], 0, sizeof(soft_timer_t));
            result = true;
            break;
        }
    }

    TIMER_EXIT_CRITICAL();
    return result;
#endif
}

/**
 * @brief 更新定时器间隔
 */
bool soft_timer_set_interval(soft_timer_id_t timer_id, uint32_t new_interval)
{
    if (timer_id == 0 || new_interval == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    timer->interval = new_interval;
    TIMER_EXIT_CRITICAL();
    return true;
}

/**
 * @brief 设置定时器事件的发布模式（同步/异步）
 */
bool soft_timer_set_dispatch_mode(soft_timer_id_t timer_id, event_dispatch_mode_t mode)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }
    timer->dispatch_mode = mode;
    TIMER_EXIT_CRITICAL();
    return true;
}

// ============================================================================
// 定时器查询
// ============================================================================

/**
 * @brief 获取定时器状态
 */
timer_state_t soft_timer_get_state(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return TIMER_STATE_STOPPED;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    timer_state_t state = (timer != NULL) ? timer->state : TIMER_STATE_STOPPED;
    TIMER_EXIT_CRITICAL();
    return state;
}

/**
 * @brief 获取定时器剩余时间
 */
uint32_t soft_timer_get_remaining(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return 0;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL || timer->state != TIMER_STATE_RUNNING) {
        TIMER_EXIT_CRITICAL();
        return 0;
    }

    uint32_t current_time = sys_time_get_ms();
    uint32_t total_elapsed = timer->elapsed + (current_time - timer->start_time);

    uint32_t remaining = 0;
    if (total_elapsed < timer->interval) {
        remaining = timer->interval - total_elapsed;
    }

    TIMER_EXIT_CRITICAL();
    return remaining;
}

// ============================================================================
// 定时器处理
// ============================================================================

/**
 * @brief 定时器处理函数
 */
void soft_timer_process(void)
{
#if TIMER_ENABLE_PERF_STATS
    uint32_t process_start = DWT_GET_CYCLES();
#endif

    uint32_t current_time = sys_time_get_ms();

#if TIMER_USE_DYNAMIC_MEMORY
    // 动态内存模式：遍历链表
    soft_timer_t *last = NULL;

    while (1) {
        soft_timer_t *selected = NULL;
        event_type_t event_type = 0;
        void *user_data = NULL;
        uint16_t data_size = 0;
        timer_mode_t mode = TIMER_MODE_ONCE;
        event_dispatch_mode_t dispatch_mode = TIMER_DEFAULT_DISPATCH_MODE;

        {
            TIMER_ENTER_CRITICAL();

            // 从上次处理节点之后开始；若节点已被删除则从头开始
            soft_timer_t *start = timer_head;
            if (last != NULL) {
                soft_timer_t *search = timer_head;
                while (search != NULL && search != last) {
                    search = search->next;
                }
                if (search != NULL) {
                    start = search->next;
                }
            }

            // 找到下一个需要触发的定时器
            while (start != NULL) {
                if (start->state == TIMER_STATE_RUNNING) {
                    uint32_t now = sys_time_get_ms();
                    uint32_t total_elapsed = start->elapsed + (now - start->start_time);
                    if (total_elapsed >= start->interval) {
                        selected = start;
                        // 保存当前时间以复用，避免回调后时间跳变造成偏差
                        current_time = now;
                        event_type = start->event_type;
                        user_data = start->user_data;
                        data_size = start->user_data_size;
                        mode = start->mode;
                        dispatch_mode = start->dispatch_mode;
                        break;
                    }
                }
                start = start->next;
            }

            if (selected == NULL) {
                TIMER_EXIT_CRITICAL();
                break;  // 无待触发定时器
            }

            // 提前计数，表示该定时器已判定触发
            stats.total_triggers++;
            TIMER_EXIT_CRITICAL();
        }

#if TIMER_ENABLE_PERF_STATS
        // 发布带性能统计的事件（传递定时器 ID 和入队时间）
        uint32_t enqueue_cycles = DWT_GET_CYCLES();
        event_publish_with_stats(event_type, user_data, data_size, dispatch_mode,
                                  (uintptr_t)selected, enqueue_cycles);
#else
        event_publish_ex(event_type, user_data, data_size, dispatch_mode);
#endif

        {
            TIMER_ENTER_CRITICAL();

            // 回调期间可能删除该定时器，需重新验证指针是否仍在链表中
            soft_timer_t *verify = timer_head;
            while (verify != NULL && verify != selected) {
                verify = verify->next;
            }

            if (verify != NULL) {
                if (mode == TIMER_MODE_ONCE) {
                    verify->state = TIMER_STATE_STOPPED;
                    verify->elapsed = 0;
                    if (stats.active_timers > 0) {
                        stats.active_timers--;
                    }
                } else {
                    verify->start_time = current_time;
                    verify->elapsed = 0;
                }
                last = verify;
            } else {
                // 节点在回调中被删除，重置迭代起点
                last = NULL;
            }

            TIMER_EXIT_CRITICAL();
        }
    }

#else
    // 静态内存模式：遍历数组
#if TIMER_ENABLE_PERF_STATS
    uint32_t scan_start = DWT_GET_CYCLES();
#endif

    for (uint8_t i = 0; i < SOFT_TIMER_MAX_COUNT; i++) {
        TIMER_ENTER_CRITICAL();

        if (!timers[i].active || timers[i].state != TIMER_STATE_RUNNING) {
            TIMER_EXIT_CRITICAL();
            continue;
        }

        // 计算已运行时间
        uint32_t total_elapsed = timers[i].elapsed + (current_time - timers[i].start_time);

        // 检查是否超时
        if (total_elapsed >= timers[i].interval) {
            // 发布事件
            event_type_t event_type = timers[i].event_type;
            void *user_data = timers[i].user_data;
            uint16_t data_size = timers[i].user_data_size;
            event_dispatch_mode_t dispatch_mode = timers[i].dispatch_mode;

            TIMER_EXIT_CRITICAL();

#if TIMER_ENABLE_PERF_STATS
            // 测量事件发布时间
            uint32_t publish_start = DWT_GET_CYCLES();
            sys_perf_stats.scan_count++;
            if (publish_start - scan_start > sys_perf_stats.max_scan_cycles) {
                sys_perf_stats.max_scan_cycles = publish_start - scan_start;
            }
            scan_start = publish_start;  // 重置扫描计时
#endif

            // 发布带性能统计的事件（传递定时器 ID 和入队时间）
            uint32_t enqueue_cycles = DWT_GET_CYCLES();
            event_publish_with_stats(event_type, user_data, data_size, dispatch_mode,
                                      (uintptr_t)&timers[i], enqueue_cycles);

#if TIMER_ENABLE_PERF_STATS
            uint32_t publish_end = DWT_GET_CYCLES();
            uint32_t publish_cycles = publish_end - publish_start;
            sys_perf_stats.total_publish_cycles += publish_cycles;
            sys_perf_stats.publish_count++;
            if (publish_cycles > sys_perf_stats.max_publish_cycles) {
                sys_perf_stats.max_publish_cycles = publish_cycles;
            }
#endif

            TIMER_ENTER_CRITICAL();

            stats.total_triggers++;

            // 处理不同模式
            if (timers[i].mode == TIMER_MODE_ONCE) {
                // 单次模式：停止定时器
                timers[i].state = TIMER_STATE_STOPPED;
                timers[i].elapsed = 0;
                if (stats.active_timers > 0) {
                    stats.active_timers--;
                }
            } else {
                // 周期模式：重新开始
                timers[i].start_time = current_time;
                timers[i].elapsed = 0;
            }
        }

        TIMER_EXIT_CRITICAL();
    }
#endif

#if TIMER_ENABLE_PERF_STATS
    // 统计整个函数的执行时间
    uint32_t process_end = DWT_GET_CYCLES();
    uint32_t process_cycles = process_end - process_start;
    sys_perf_stats.total_process_cycles += process_cycles;
    sys_perf_stats.process_count++;
    if (process_cycles > sys_perf_stats.max_process_cycles) {
        sys_perf_stats.max_process_cycles = process_cycles;
    }

    // 定期打印性能统计
    if (current_time - sys_perf_stats.last_print_time >= PERF_PRINT_INTERVAL_MS && sys_perf_stats.process_count > 0) {
        #define CYCLES_TO_US(c) ((c) / 600)  // 600MHz CPU

#if TIMER_ENABLE_PERF_LOG
        sys_log_text(info, "[Timer_Perf] process: avg=%uus, max=%uus, calls=%u",
                     CYCLES_TO_US(sys_perf_stats.total_process_cycles / sys_perf_stats.process_count),
                     CYCLES_TO_US(sys_perf_stats.max_process_cycles),
                     sys_perf_stats.process_count);

        sys_log_text(info, "[Timer_Perf] scan: avg=%uus, max=%uus, calls=%u",
                     sys_perf_stats.scan_count ? CYCLES_TO_US(sys_perf_stats.total_scan_cycles / sys_perf_stats.scan_count) : 0,
                     CYCLES_TO_US(sys_perf_stats.max_scan_cycles),
                     sys_perf_stats.scan_count);

        sys_log_text(info, "[Timer_Perf] publish: avg=%uus, max=%uus, calls=%u",
                     sys_perf_stats.publish_count ? CYCLES_TO_US(sys_perf_stats.total_publish_cycles / sys_perf_stats.publish_count) : 0,
                     CYCLES_TO_US(sys_perf_stats.max_publish_cycles),
                     sys_perf_stats.publish_count);
#endif

        // 重置统计
        memset(&sys_perf_stats, 0, sizeof(sys_perf_stats));
        sys_perf_stats.last_print_time = current_time;
    }
#endif
}

// ============================================================================
// 统计信息
// ============================================================================

/**
 * @brief 获取定时器系统统计信息
 */
void soft_timer_get_stats(timer_stats_t *stats_out)
{
    if (stats_out != NULL) {
        TIMER_ENTER_CRITICAL();
        memcpy(stats_out, &stats, sizeof(timer_stats_t));
        TIMER_EXIT_CRITICAL();
    }
}

/**
 * @brief 获取活跃定时器数量
 */
uint16_t soft_timer_get_active_count(void)
{
    TIMER_ENTER_CRITICAL();
    uint16_t count = stats.active_timers;
    TIMER_EXIT_CRITICAL();
    return count;
}

/**
 * @brief 定时器状态字符串转换
 */
static const char* timer_state_to_string(timer_state_t state) {
    switch (state) {
        case TIMER_STATE_RUNNING: return "RUNNING";
        case TIMER_STATE_PAUSED:  return "PAUSED";
        case TIMER_STATE_STOPPED: return "STOPPED";
        default:                  return "UNKNOWN";
    }
}

/**
 * @brief 定时器模式字符串转换
 */
static const char* timer_mode_to_string(timer_mode_t mode) {
    switch (mode) {
        case TIMER_MODE_ONCE:     return "ONCE";
        case TIMER_MODE_PERIODIC: return "PERIODIC";
        default:                  return "UNKNOWN";
    }
}

/**
 * @brief 定时器命令处理器
 *
 * 支持的命令：
 * - timer list                    : 列出所有定时器
 * - timer info <id>               : 查看定时器详情
 * - timer start <id>              : 启动定时器
 * - timer stop <id>               : 停止定时器
 * - timer pause <id>              : 暂停定时器
 * - timer resume <id>             : 恢复定时器
 * - timer reset <id>              : 重置定时器
 * - timer stats                   : 显示系统统计信息
 * - timer perf <id>               : 显示定时器性能统计
 * - timer perf reset <id>         : 重置定时器性能统计
 */
static exit_code_t soft_timer_command_handler_impl(i32 seq, int argc, char **argv) {
    (void)seq;
    if (argc < 2) {
        sys_log_text(terminal, "Usage:");
        sys_log_text(terminal, "  timer list                     - List all timers");
        sys_log_text(terminal, "  timer info <id>                - Show timer details");
        sys_log_text(terminal, "  timer start <id>               - Start a timer");
        sys_log_text(terminal, "  timer stop <id>                - Stop a timer");
        sys_log_text(terminal, "  timer pause <id>               - Pause a timer");
        sys_log_text(terminal, "  timer resume <id>              - Resume a timer");
        sys_log_text(terminal, "  timer reset <id>               - Reset a timer");
        sys_log_text(terminal, "  timer stats                    - Show system statistics");
#if TIMER_ENABLE_PERF_STATS
        sys_log_text(terminal, "  timer perf <id>                - Show timer performance stats");
        sys_log_text(terminal, "  timer perf reset <id>          - Reset timer performance stats");
#endif
        return EXIT_INVALID_PARAM;
    }

    char *cmd = argv[1];

    // ========================================
    // timer list - 列出所有定时器
    // ========================================
    if (strcmp(cmd, "list") == 0) {
        sys_log_text(terminal, "[Timers]  ID         Name             Interval  AvgTime");
        sys_log_text(terminal, "--------------------------------------------------------");

#if TIMER_USE_DYNAMIC_MEMORY
        // 动态内存模式：遍历链表
        soft_timer_t *current = timer_head;
        int count = 0;
        while (current != NULL) {
            const char *s = (current->state == TIMER_STATE_RUNNING) ? "*" : ".";
            const char *name = current->name[0] ? current->name : "?";
#if TIMER_ENABLE_PERF_STATS
            // 计算平均时间（微秒）
            uint32_t avg_us = 0;
            if (current->perf_exec_count > 0) {
                avg_us = soft_timer_cycles_to_us(current->perf_total_cycles / current->perf_exec_count);
            }
            sys_log_text(terminal, " %s %08X  %-16s %6ums %7uus",
                         s, (unsigned int)current->id, name, current->interval, avg_us);
#else
            sys_log_text(terminal, " %s %08X  %-16s %6ums",
                         s, (unsigned int)current->id, name, current->interval);
#endif
            current = current->next;
            count++;
        }
        sys_log_text(terminal, "--------------------------------------------------------");
        sys_log_text(terminal, "Total:%d", count);

#else
        // 静态内存模式：遍历数组
        int count = 0;
        for (uint8_t i = 0; i < SOFT_TIMER_MAX_COUNT; i++) {
            if (timers[i].active) {
                const char *s = (timers[i].state == TIMER_STATE_RUNNING) ? "*" : ".";
                const char *name = timers[i].name[0] ? timers[i].name : "?";
#if TIMER_ENABLE_PERF_STATS
                // 计算平均时间（微秒）
                uint32_t avg_us = 0;
                if (timers[i].perf_exec_count > 0) {
                    avg_us = soft_timer_cycles_to_us(timers[i].perf_total_cycles / timers[i].perf_exec_count);
                }
                sys_log_text(terminal, " %s %08X  %-16s %6ums %7uus",
                             s, (unsigned int)timers[i].id, name, timers[i].interval, avg_us);
#else
                sys_log_text(terminal, " %s %08X  %-16s %6ums",
                             s, (unsigned int)timers[i].id, name, timers[i].interval);
#endif
                count++;
            }
        }
        sys_log_text(terminal, "--------------------------------------------------------");
        sys_log_text(terminal, "Total:%d/%d", count, SOFT_TIMER_MAX_COUNT);
#endif
        return EXIT_OK;
    }

    // ========================================
    // 需要解析 ID 的命令
    // ========================================
    if (argc < 3) {
        sys_log_text(terminal, "Error: Missing timer ID");
        sys_log_text(terminal, "Usage: timer %s <id>", cmd);
        return EXIT_INVALID_PARAM;
    }

    // 解析定时器 ID（支持十六进制和十进制）
    soft_timer_id_t timer_id = 0;
    if (argv[2][0] == '0' && (argv[2][1] == 'x' || argv[2][1] == 'X')) {
        // 十六进制
        timer_id = (soft_timer_id_t)strtoul(argv[2], NULL, 16);
    } else {
        // 十进制
        timer_id = (soft_timer_id_t)atoi(argv[2]);
    }

    // ========================================
    // timer info <id> - 查看定时器详情
    // ========================================
    if (strcmp(cmd, "info") == 0) {
        TIMER_ENTER_CRITICAL();
        soft_timer_t *timer = find_timer_by_id(timer_id);
        TIMER_EXIT_CRITICAL();

        if (timer == NULL) {
            sys_log_text(terminal, "Error: Timer %08X not found", (unsigned int)timer_id);
            return EXIT_DOES_NOT_EXIST;
        }

        sys_log_text(terminal, "=== Timer Info ===");
        sys_log_text(terminal, "Name:      %s", timer->name[0] ? timer->name : "(unnamed)");
        sys_log_text(terminal, "ID:        %08X", (unsigned int)timer->id);
        sys_log_text(terminal, "State:     %s", timer_state_to_string(timer->state));
        sys_log_text(terminal, "Mode:      %s", timer_mode_to_string(timer->mode));
        sys_log_text(terminal, "Interval:  %u ms", timer->interval);
        sys_log_text(terminal, "Elapsed:   %u ms", timer->elapsed);
        sys_log_text(terminal, "Dispatch:  %s", (timer->dispatch_mode == EVENT_DISPATCH_SYNC) ? "SYNC" : "ASYNC");
        sys_log_text(terminal, "Event:     0x%04X", timer->event_type);

        if (timer->state == TIMER_STATE_RUNNING) {
            uint32_t remaining = soft_timer_get_remaining(timer_id);
            sys_log_text(terminal, "Remaining: %u ms", remaining);
        }
        return EXIT_OK;
    }

    // ========================================
    // timer start <id> - 启动定时器
    // ========================================
    if (strcmp(cmd, "start") == 0) {
        if (soft_timer_start(timer_id)) {
            sys_log_text(terminal, "Timer %08X started", (unsigned int)timer_id);
            return EXIT_OK;
        } else {
            sys_log_text(terminal, "Error: Failed to start timer %08X", (unsigned int)timer_id);
            return EXIT_FAIL;
        }
    }

    // ========================================
    // timer stop <id> - 停止定时器
    // ========================================
    if (strcmp(cmd, "stop") == 0) {
        if (soft_timer_stop(timer_id)) {
            sys_log_text(terminal, "Timer %08X stopped", (unsigned int)timer_id);
            return EXIT_OK;
        } else {
            sys_log_text(terminal, "Error: Failed to stop timer %08X", (unsigned int)timer_id);
            return EXIT_FAIL;
        }
    }

    // ========================================
    // timer pause <id> - 暂停定时器
    // ========================================
    if (strcmp(cmd, "pause") == 0) {
        if (soft_timer_pause(timer_id)) {
            sys_log_text(terminal, "Timer %08X paused", (unsigned int)timer_id);
            return EXIT_OK;
        } else {
            sys_log_text(terminal, "Error: Failed to pause timer %08X", (unsigned int)timer_id);
            return EXIT_FAIL;
        }
    }

    // ========================================
    // timer resume <id> - 恢复定时器
    // ========================================
    if (strcmp(cmd, "resume") == 0) {
        if (soft_timer_resume(timer_id)) {
            sys_log_text(terminal, "Timer %08X resumed", (unsigned int)timer_id);
            return EXIT_OK;
        } else {
            sys_log_text(terminal, "Error: Failed to resume timer %08X", (unsigned int)timer_id);
            return EXIT_FAIL;
        }
    }

    // ========================================
    // timer reset <id> - 重置定时器
    // ========================================
    if (strcmp(cmd, "reset") == 0) {
        if (soft_timer_reset(timer_id)) {
            sys_log_text(terminal, "Timer %08X reset", (unsigned int)timer_id);
            return EXIT_OK;
        } else {
            sys_log_text(terminal, "Error: Failed to reset timer %08X", (unsigned int)timer_id);
            return EXIT_FAIL;
        }
    }

    // ========================================
    // timer stats - 系统统计信息
    // ========================================
    if (strcmp(cmd, "stats") == 0) {
        timer_stats_t stats;
        soft_timer_get_stats(&stats);

        sys_log_text(terminal, "=== Timer Statistics ===");
        sys_log_text(terminal, "Active Timers:  %u", stats.active_timers);
        sys_log_text(terminal, "Total Triggers: %u", stats.total_triggers);
#if !TIMER_USE_DYNAMIC_MEMORY
        sys_log_text(terminal, "Max Timers:     %d", SOFT_TIMER_MAX_COUNT);
#endif
        return EXIT_OK;
    }

#if TIMER_ENABLE_PERF_STATS
    // ========================================
    // timer perf <id> - 显示定时器性能统计
    // ========================================
    if (strcmp(cmd, "perf") == 0) {
        if (argc < 3) {
            sys_log_text(terminal, "Error: Missing timer ID");
            sys_log_text(terminal, "Usage: timer perf <id>");
            return EXIT_INVALID_PARAM;
        }

        // 检查是否是 reset 子命令
        if (strcmp(argv[2], "reset") == 0) {
            if (argc < 4) {
                sys_log_text(terminal, "Error: Missing timer ID");
                sys_log_text(terminal, "Usage: timer perf reset <id>");
                return EXIT_INVALID_PARAM;
            }

            soft_timer_id_t timer_id = 0;
            if (argv[3][0] == '0' && (argv[3][1] == 'x' || argv[3][1] == 'X')) {
                timer_id = (soft_timer_id_t)strtoul(argv[3], NULL, 16);
            } else {
                timer_id = (soft_timer_id_t)atoi(argv[3]);
            }

            if (soft_timer_reset_perf_stats(timer_id)) {
                sys_log_text(terminal, "Timer %08X performance stats reset", (unsigned int)timer_id);
                return EXIT_OK;
            } else {
                sys_log_text(terminal, "Error: Failed to reset stats for timer %08X", (unsigned int)timer_id);
                return EXIT_FAIL;
            }
        }

        // 解析定时器 ID
        soft_timer_id_t timer_id = 0;
        if (argv[2][0] == '0' && (argv[2][1] == 'x' || argv[2][1] == 'X')) {
            timer_id = (soft_timer_id_t)strtoul(argv[2], NULL, 16);
        } else {
            timer_id = (soft_timer_id_t)atoi(argv[2]);
        }

        // 获取性能统计
        timer_perf_stats_t perf_stats;
        if (!soft_timer_get_perf_stats(timer_id, &perf_stats)) {
            sys_log_text(terminal, "Error: Timer %08X not found", (unsigned int)timer_id);
            return EXIT_DOES_NOT_EXIST;
        }

        // 获取定时器名称
        TIMER_ENTER_CRITICAL();
        soft_timer_t *timer = find_timer_by_id(timer_id);
        const char *name = (timer != NULL && timer->name[0]) ? timer->name : "(unnamed)";
        TIMER_EXIT_CRITICAL();

        sys_log_text(terminal, "=== Timer Performance Stats: %s ===", name);
        sys_log_text(terminal, "Execution Count:  %u", perf_stats.exec_count);
        sys_log_text(terminal, "Total Cycles:     %u", perf_stats.total_cycles);
        sys_log_text(terminal, "Average Cycles:   %u", perf_stats.avg_cycles);
        sys_log_text(terminal, "Max Cycles:       %u", perf_stats.max_cycles);
        sys_log_text(terminal, "Min Cycles:       %u", perf_stats.min_cycles);

        // 转换为微秒显示（假设 600MHz CPU）
        sys_log_text(terminal, "");
        sys_log_text(terminal, "Time (600MHz CPU):");
        sys_log_text(terminal, "  Total:      %u us", soft_timer_cycles_to_us(perf_stats.total_cycles));
        sys_log_text(terminal, "  Average:    %u us", soft_timer_cycles_to_us(perf_stats.avg_cycles));
        sys_log_text(terminal, "  Max:        %u us", soft_timer_cycles_to_us(perf_stats.max_cycles));
        sys_log_text(terminal, "  Min:        %u us", soft_timer_cycles_to_us(perf_stats.min_cycles));
        return EXIT_OK;
    }
#endif

    // ========================================
    // 未知命令
    // ========================================
    sys_log_text(terminal, "Error: Unknown command '%s'", cmd);
    sys_log_text(terminal, "Use 'timer' to see available commands");
    return EXIT_NOT_SUPPORTED;
}

cmd_exec_result_t soft_timer_command_handler(i32 seq, int argc, char **argv)
{
    exit_code_t code = soft_timer_command_handler_impl(seq, argc, argv);
    if (code == EXIT_OK && argc >= 2 && argv != NULL && argv[1] != NULL) {
        static char ctx[96];
        snprintf(ctx, sizeof(ctx), "subcmd=%s", argv[1]);
        return CMD_EXEC_CTX(code, ctx);
    }
    return CMD_EXEC_CODE(code);
}

// ============================================================================
// 扩展 API 实现 - 带名称的定时器操作
// ============================================================================

/**
 * @brief 设置定时器名称
 */
bool soft_timer_set_name(soft_timer_id_t timer_id, const char *name)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    if (name != NULL) {
        strncpy(timer->name, name, SOFT_TIMER_NAME_MAX_LEN - 1);
        timer->name[SOFT_TIMER_NAME_MAX_LEN - 1] = '\0';
    } else {
        timer->name[0] = '\0';
    }

    TIMER_EXIT_CRITICAL();
    return true;
}

/**
 * @brief 启动定时器并设置名称
 */
bool soft_timer_start_named(soft_timer_id_t timer_id, const char *name)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    // 设置名称
    if (name != NULL) {
        strncpy(timer->name, name, SOFT_TIMER_NAME_MAX_LEN - 1);
        timer->name[SOFT_TIMER_NAME_MAX_LEN - 1] = '\0';
    }

    // 启动定时器
    if (timer->state == TIMER_STATE_STOPPED) {
        timer->state = TIMER_STATE_RUNNING;
        timer->start_time = sys_time_get_ms();
        timer->elapsed = 0;
        stats.active_timers++;
    }

    TIMER_EXIT_CRITICAL();
    sys_log_text(info, "Started timer '%s' ID: %p", timer->name[0] ? timer->name : "(unnamed)", (void*)timer_id);
    return true;
}

/**
 * @brief 创建带名称的定时器（扩展版）
 */
soft_timer_id_t soft_timer_create_named(uint32_t interval, timer_mode_t mode,
                                        event_type_t event_type, void *user_data,
                                        uint16_t user_data_size,
                                        event_callback_t callback,
                                        event_dispatch_mode_t dispatch_mode,
                                        const char *name)
{
    if (interval == 0 || event_type == 0) {
        return 0;
    }

    // 自动分配事件类型
    if (event_type == TIMER_EVENT_AUTO) {
        TIMER_ENTER_CRITICAL();

        if (next_auto_event_type > TIMER_EVENT_AUTO_MAX) {
            TIMER_EXIT_CRITICAL();
            return 0;
        }

        event_type = next_auto_event_type++;
        TIMER_EXIT_CRITICAL();
    }

    TIMER_ENTER_CRITICAL();

#if TIMER_USE_DYNAMIC_MEMORY
    soft_timer_t *new_node = (soft_timer_t*)malloc(sizeof(soft_timer_t));
    if (new_node == NULL) {
        TIMER_EXIT_CRITICAL();
        return 0;
    }

    new_node->mode = mode;
    new_node->state = TIMER_STATE_STOPPED;
    new_node->interval = interval;
    new_node->start_time = 0;
    new_node->elapsed = 0;
    new_node->event_type = event_type;
    new_node->user_data = user_data;
    new_node->user_data_size = (user_data_size == 0) ? sizeof(void*) : user_data_size;
    new_node->dispatch_mode = dispatch_mode;
    new_node->id = (soft_timer_id_t)new_node;
    new_node->listener_id = EVENT_LISTENER_ID_INVALID;

    // 设置名称
    if (name != NULL) {
        strncpy(new_node->name, name, SOFT_TIMER_NAME_MAX_LEN - 1);
        new_node->name[SOFT_TIMER_NAME_MAX_LEN - 1] = '\0';
    } else {
        new_node->name[0] = '\0';
    }

#if TIMER_ENABLE_PERF_STATS
    new_node->perf_total_cycles = 0;
    new_node->perf_exec_count = 0;
    new_node->perf_max_cycles = 0;
    new_node->perf_min_cycles = 0;
#endif

    new_node->next = timer_head;
    timer_head = new_node;

    if (callback != NULL) {
        event_listener_id_t listener_id = event_subscribe(event_type, callback, user_data);
        if (listener_id == EVENT_LISTENER_ID_INVALID) {
            timer_head = new_node->next;
            free(new_node);
            TIMER_EXIT_CRITICAL();
            return 0;
        }
        new_node->listener_id = listener_id;
    }

    TIMER_EXIT_CRITICAL();
    return new_node->id;

#else
    soft_timer_id_t result = 0;
    for (uint8_t i = 0; i < SOFT_TIMER_MAX_COUNT; i++) {
        if (!timers[i].active) {
            timers[i].active = true;
            timers[i].mode = mode;
            timers[i].state = TIMER_STATE_STOPPED;
            timers[i].interval = interval;
            timers[i].start_time = 0;
            timers[i].elapsed = 0;
            timers[i].event_type = event_type;
            timers[i].user_data = user_data;
            timers[i].user_data_size = (user_data_size == 0) ? sizeof(void*) : user_data_size;
            timers[i].dispatch_mode = dispatch_mode;
            timers[i].id = (soft_timer_id_t)&timers[i];
            timers[i].listener_id = EVENT_LISTENER_ID_INVALID;

            // 设置名称
            if (name != NULL) {
                strncpy(timers[i].name, name, SOFT_TIMER_NAME_MAX_LEN - 1);
                timers[i].name[SOFT_TIMER_NAME_MAX_LEN - 1] = '\0';
            } else {
                timers[i].name[0] = '\0';
            }

#if TIMER_ENABLE_PERF_STATS
            timers[i].perf_total_cycles = 0;
            timers[i].perf_exec_count = 0;
            timers[i].perf_max_cycles = 0;
            timers[i].perf_min_cycles = 0;
#endif

            if (callback != NULL) {
                event_listener_id_t listener_id = event_subscribe(event_type, callback, user_data);
                if (listener_id == EVENT_LISTENER_ID_INVALID) {
                    memset(&timers[i], 0, sizeof(soft_timer_t));
                    result = 0;
                    break;
                }
                timers[i].listener_id = listener_id;
            }

            result = timers[i].id;
            break;
        }
    }

    TIMER_EXIT_CRITICAL();
    return result;
#endif
}

/**
 * @brief 创建带名称的定时器（简化版）
 */
soft_timer_id_t soft_timer_create_simple_named(uint32_t interval, timer_mode_t mode,
                                               event_callback_t callback,
                                               event_dispatch_mode_t dispatch_mode,
                                               const char *name)
{
    if (callback == NULL) {
        return 0;
    }

    TIMER_ENTER_CRITICAL();

    if (next_auto_event_type > TIMER_EVENT_AUTO_MAX) {
        TIMER_EXIT_CRITICAL();
        return 0;
    }

    event_type_t allocated_event_type = next_auto_event_type++;
    TIMER_EXIT_CRITICAL();

    return soft_timer_create_named(interval, mode, allocated_event_type, NULL, 0,
                                   callback, dispatch_mode, name);
}

// ============================================================================
// 性能统计 API 实现
// ============================================================================

#if TIMER_ENABLE_PERF_STATS

/**
 * @brief 内部函数：由事件系统调用，记录定时器任务执行时间
 */
void soft_timer_record_perf(soft_timer_id_t timer_id, uint32_t exec_cycles)
{
    if (timer_id == 0) {
        return;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer != NULL) {
        timer->perf_total_cycles += exec_cycles;
        timer->perf_exec_count++;
        if (exec_cycles > timer->perf_max_cycles) {
            timer->perf_max_cycles = exec_cycles;
        }
        if (timer->perf_min_cycles == 0 || exec_cycles < timer->perf_min_cycles) {
            timer->perf_min_cycles = exec_cycles;
        }
    }
    TIMER_EXIT_CRITICAL();
}

/**
 * @brief 获取定时器性能统计信息
 */
bool soft_timer_get_perf_stats(soft_timer_id_t timer_id, timer_perf_stats_t *stats)
{
    if (timer_id == 0 || stats == NULL) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    stats->total_cycles = timer->perf_total_cycles;
    stats->exec_count = timer->perf_exec_count;
    stats->max_cycles = timer->perf_max_cycles;
    stats->min_cycles = timer->perf_min_cycles;
    // 计算平均值（避免除零）
    stats->avg_cycles = (timer->perf_exec_count > 0) ?
                        (timer->perf_total_cycles / timer->perf_exec_count) : 0;

    TIMER_EXIT_CRITICAL();
    return true;
}

/**
 * @brief 重置定时器性能统计信息
 */
bool soft_timer_reset_perf_stats(soft_timer_id_t timer_id)
{
    if (timer_id == 0) {
        return false;
    }

    TIMER_ENTER_CRITICAL();
    soft_timer_t *timer = find_timer_by_id(timer_id);
    if (timer == NULL) {
        TIMER_EXIT_CRITICAL();
        return false;
    }

    timer->perf_total_cycles = 0;
    timer->perf_exec_count = 0;
    timer->perf_max_cycles = 0;
    timer->perf_min_cycles = 0;

    TIMER_EXIT_CRITICAL();
    return true;
}

#endif // TIMER_ENABLE_PERF_STATS

