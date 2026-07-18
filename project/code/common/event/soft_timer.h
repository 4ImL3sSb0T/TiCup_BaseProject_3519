/**
 * @file soft_timer.h
 * @brief 软件定时器 - 基于事件系统的轻量级定时器
 * @author Jupiter Smart Car Team
 * @date 2024-12-24
 */

#ifndef __SOFT_TIMER_H__
#define __SOFT_TIMER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "common/tools/common_def.h"
#include "event.h"

typedef uintptr_t soft_timer_id_t;

// ============================================================================
// 配置参数
// ============================================================================

/**
 * @brief 启用动态内存分配
 * 0 = 使用静态内存数组（编译时分配）
 * 1 = 使用动态内存分配（运行时分配，需要 malloc/free）
 */
#ifndef TIMER_USE_DYNAMIC_MEMORY
#define TIMER_USE_DYNAMIC_MEMORY    0
#endif

/**
 * @brief 启用中断安全保护
 * 0 = 不使用临界区保护（仅主循环使用）
 * 1 = 使用临界区保护（可在中断中使用）
 */
#ifndef TIMER_INTERRUPT_SAFE
#define TIMER_INTERRUPT_SAFE        1
#endif

/**
 * @brief 最大定时器数量（仅静态内存模式有效）
 */
#ifndef SOFT_TIMER_MAX_COUNT
#define SOFT_TIMER_MAX_COUNT        32
#endif

/**
 * @brief 定时器名称最大长度
 */
#ifndef SOFT_TIMER_NAME_MAX_LEN
#define SOFT_TIMER_NAME_MAX_LEN     32
#endif

/**
 * @brief 定时器事件发布模式（同步/异步）
 * 默认同步，如需异步请定义为 EVENT_DISPATCH_ASYNC
 */
#ifndef TIMER_DEFAULT_DISPATCH_MODE
#define TIMER_DEFAULT_DISPATCH_MODE EVENT_DISPATCH_SYNC
#endif

/**
 * @brief 启用性能统计功能
 * 0 = 关闭（节省内存）
 * 1 = 开启（使用 DWT 周期计数器测量任务执行时间）
 */
#ifndef TIMER_ENABLE_PERF_STATS
#define TIMER_ENABLE_PERF_STATS        1
#endif

/**
 * @brief 启用性能统计日志打印
 * 0 = 关闭（不打印性能日志，但仍收集统计数据）
 * 1 = 开启（定期打印性能统计日志）
 * @note 仅当 TIMER_ENABLE_PERF_STATS = 1 时有效
 */
#ifndef TIMER_ENABLE_PERF_LOG
#define TIMER_ENABLE_PERF_LOG          0
#endif

// ============================================================================
// 定时器事件类型
// ============================================================================

/**
 * @brief 定时器事件类型定义
 *
 * 当定时器超时时，会发布对应的事件
 * 用户可以在创建定时器时指定自定义事件类型
 */
#define TIMER_EVENT_BASE            0x0100  // 定时器事件基础值
#define TIMER_EVENT_AUTO            0xFFFF  // 自动分配事件号起始值
#define TIMER_EVENT_AUTO_MAX        0x01FF  // 自动分配事件号上限（支持 255 个定时器）

/**
 * @brief 定时器模式
 */
typedef enum {
    TIMER_MODE_ONCE,        // 单次触发
    TIMER_MODE_PERIODIC     // 周期触发
} timer_mode_t;

/**
 * @brief 定时器状态
 */
typedef enum {
    TIMER_STATE_STOPPED,    // 停止
    TIMER_STATE_RUNNING,    // 运行中
    TIMER_STATE_PAUSED      // 暂停
} timer_state_t;

/**
 * @brief 软件定时器结构（内部使用）
 */
typedef struct soft_timer_node {
    timer_mode_t mode;          // 定时器模式
    timer_state_t state;        // 定时器状态
    uint32_t interval;          // 定时间隔（毫秒）
    uint32_t start_time;        // 启动时间
    uint32_t elapsed;           // 已运行时间（用于暂停/恢复）
    event_type_t event_type;    // 事件类型
    void *user_data;            // 用户数据
    uint16_t user_data_size;    // 用户数据大小（字节），0 则按指针大小传递
    event_dispatch_mode_t dispatch_mode; // 事件发布模式（同步/异步）
    soft_timer_id_t id;         // 定时器唯一ID（指针地址）
    event_listener_id_t listener_id; // 自动订阅的监听者ID（0 表示未订阅）
    char name[SOFT_TIMER_NAME_MAX_LEN]; // 定时器名称（用于调试）
#if TIMER_ENABLE_PERF_STATS
    uint32_t perf_total_cycles;     // 总执行周期数（累计）
    uint32_t perf_exec_count;       // 执行次数
    uint32_t perf_max_cycles;       // 单次最大执行周期
    uint32_t perf_min_cycles;       // 单次最小执行周期
#endif
#if TIMER_USE_DYNAMIC_MEMORY
    struct soft_timer_node *next;  // 链表指针（动态内存模式）
#else
    bool active;                // 是否已分配（静态内存模式）
#endif
} soft_timer_t;

/**
 * @brief 定时器统计信息
 */
typedef struct {
    uint32_t total_triggers;    // 总触发次数
    uint16_t active_timers;     // 活跃定时器数量
} timer_stats_t;

#if TIMER_ENABLE_PERF_STATS
/**
 * @brief 定时器性能统计信息
 */
typedef struct {
    uint32_t total_cycles;      // 总执行周期
    uint32_t exec_count;        // 执行次数
    uint32_t max_cycles;        // 最大执行周期
    uint32_t min_cycles;        // 最小执行周期
    uint32_t avg_cycles;        // 平均执行周期
} timer_perf_stats_t;
#endif

/**
 * @brief 初始化软件定时器系统
 *
 * @note 静态内存模式：初始化静态数组
 * @note 动态内存模式：初始化链表头指针
 */
void soft_timer_init(void);

#if TIMER_USE_DYNAMIC_MEMORY
/**
 * @brief 释放定时器系统资源（动态内存模式）
 *
 * @note 释放所有动态分配的定时器节点
 */
void soft_timer_deinit(void);
#endif

/**
 * @brief 创建定时器（完整版，手动指定事件类型）
 * @param interval 定时间隔（毫秒）
 * @param mode 定时器模式（单次/周期）
 * @param event_type 超时时发布的事件类型（可使用 TIMER_EVENT_AUTO 自动分配）
 * @param user_data 用户数据（可选，会作为事件数据传递）
 * @param user_data_size 用户数据长度（字节），0 则按 sizeof(void*)
 * @param callback 超时回调（可选），若非空则自动完成事件订阅
 * @param dispatch_mode 事件发布模式（同步/异步）
 * @return 定时器ID（0 表示失败）
 *
 * @note 定时器超时时会发布指定的事件类型
 * @note user_data 会随事件一起传递，也会作为自动订阅的 user_data 传入 callback
 * @note 当 event_type == TIMER_EVENT_AUTO 时，自动分配唯一事件类型（和 simple 版本一样）
 */
soft_timer_id_t soft_timer_create(uint32_t interval, timer_mode_t mode,
                                  event_type_t event_type, void *user_data,
                                  uint16_t user_data_size,
                                  event_callback_t callback,
                                  event_dispatch_mode_t dispatch_mode);

/**
 * @brief 创建定时器（简化版，自动分配事件类型）
 * @param interval 定时间隔（毫秒）
 * @param mode 定时器模式（单次/周期）
 * @param callback 超时回调
 * @param dispatch_mode 事件发布模式（同步/异步）
 * @return 定时器ID（0 表示失败）
 *
 * @note 简化版 API，自动为每个定时器分配唯一的事件类型
 * @note 推荐使用此接口，避免多个定时器共享同一事件号导致的问题
 * @note 事件类型从 TIMER_EVENT_BASE 开始自动递增分配
 */
soft_timer_id_t soft_timer_create_simple(uint32_t interval, timer_mode_t mode,
                                         event_callback_t callback,
                                         event_dispatch_mode_t dispatch_mode);

/**
 * @brief 启动定时器
 * @param timer_id 定时器ID
 * @return true=成功, false=失败
 */
bool soft_timer_start(soft_timer_id_t timer_id);

/**
 * @brief 停止定时器
 * @param timer_id 定时器ID
 * @return true=成功, false=失败
 */
bool soft_timer_stop(soft_timer_id_t timer_id);

/**
 * @brief 暂停定时器
 * @param timer_id 定时器ID
 * @return true=成功, false=失败
 */
bool soft_timer_pause(soft_timer_id_t timer_id);

/**
 * @brief 恢复定时器
 * @param timer_id 定时器ID
 * @return true=成功, false=失败
 */
bool soft_timer_resume(soft_timer_id_t timer_id);

/**
 * @brief 重置定时器（重新开始计时）
 * @param timer_id 定时器ID
 * @return true=成功, false=失败
 */
bool soft_timer_reset(soft_timer_id_t timer_id);

/**
 * @brief 删除定时器
 * @param timer_id 定时器ID
 * @return true=成功, false=失败
 */
bool soft_timer_delete(soft_timer_id_t timer_id);

/**
 * @brief 更新定时器间隔
 * @param timer_id 定时器ID
 * @param new_interval 新的定时间隔（毫秒）
 * @return true=成功, false=失败
 */
bool soft_timer_set_interval(soft_timer_id_t timer_id, uint32_t new_interval);

/**
 * @brief 获取定时器状态
 * @param timer_id 定时器ID
 * @return 定时器状态
 */
timer_state_t soft_timer_get_state(soft_timer_id_t timer_id);

/**
 * @brief 获取定时器剩余时间
 * @param timer_id 定时器ID
 * @return 剩余时间（毫秒），如果定时器无效返回0
 */
uint32_t soft_timer_get_remaining(soft_timer_id_t timer_id);

/**
 * @brief 定时器处理函数（需要在主循环中周期调用）
 *
 * 建议调用频率：1-10ms
 * 可以在 main loop 或低优先级定时器中断中调用
 */
void soft_timer_process(void);

/**
 * @brief 获取定时器系统统计信息
 * @param stats 统计信息结构指针
 */
void soft_timer_get_stats(timer_stats_t *stats);

/**
 * @brief 获取活跃定时器数量
 * @return 活跃定时器数量
 */
uint16_t soft_timer_get_active_count(void);

/**
 * @brief 设置定时器事件的发布模式（同步/异步）
 * @param timer_id 定时器ID
 * @param mode EVENT_DISPATCH_SYNC 或 EVENT_DISPATCH_ASYNC
 * @return true=成功, false=失败
 */
bool soft_timer_set_dispatch_mode(soft_timer_id_t timer_id, event_dispatch_mode_t mode);

cmd_exec_result_t soft_timer_command_handler(i32 seq, int argc, char **argv);

#if TIMER_ENABLE_PERF_STATS
/**
 * @brief 获取定时器性能统计信息
 * @param timer_id 定时器ID
 * @param stats 性能统计信息结构指针
 * @return true=成功, false=失败（定时器不存在）
 */
bool soft_timer_get_perf_stats(soft_timer_id_t timer_id, timer_perf_stats_t *stats);

/**
 * @brief 重置定时器性能统计信息
 * @param timer_id 定时器ID
 * @return true=成功, false=失败（定时器不存在）
 */
bool soft_timer_reset_perf_stats(soft_timer_id_t timer_id);

/**
 * @brief 将 DWT 周期数转换为微秒
 * @param cycles DWT 周期数
 * @return 微秒数（基于 600MHz CPU 时钟）
 *
 * @note 600MHz 时 1 周期 ≈ 1.67ns
 * @note 使用整数运算避免浮点开销
 */
static inline uint32_t soft_timer_cycles_to_us(uint32_t cycles) {
    // 600MHz → 1 周期 = 1/600 us = 1.667ns
    // cycles * 10 / 6000 = cycles / 600 (us)
    // 使用先乘后除避免精度损失
    return cycles / 600UL;
}

/**
 * @brief 内部函数：由事件系统调用，记录定时器任务执行时间
 * @param timer_id 定时器ID
 * @param exec_cycles 执行周期数
 */
void soft_timer_record_perf(soft_timer_id_t timer_id, uint32_t exec_cycles);
#endif

// ============================================================================
// 扩展 API - 带名称的定时器操作（不影响原有 API）
// ============================================================================

/**
 * @brief 设置定时器名称
 * @param timer_id 定时器ID
 * @param name 定时器名称
 * @return true=成功, false=失败
 */
bool soft_timer_set_name(soft_timer_id_t timer_id, const char *name);

/**
 * @brief 启动定时器并设置名称
 * @param timer_id 定时器ID
 * @param name 定时器名称
 * @return true=成功, false=失败
 */
bool soft_timer_start_named(soft_timer_id_t timer_id, const char *name);

/**
 * @brief 创建带名称的定时器（扩展版）
 * @param interval 定时间隔（毫秒）
 * @param mode 定时器模式（单次/周期）
 * @param event_type 超时时发布的事件类型
 * @param user_data 用户数据
 * @param user_data_size 用户数据长度
 * @param callback 超时回调
 * @param dispatch_mode 事件发布模式
 * @param name 定时器名称
 * @return 定时器ID（0 表示失败）
 */
soft_timer_id_t soft_timer_create_named(uint32_t interval, timer_mode_t mode,
                                        event_type_t event_type, void *user_data,
                                        uint16_t user_data_size,
                                        event_callback_t callback,
                                        event_dispatch_mode_t dispatch_mode,
                                        const char *name);

/**
 * @brief 创建带名称的定时器（简化版）
 * @param interval 定时间隔（毫秒）
 * @param mode 定时器模式（单次/周期）
 * @param callback 超时回调
 * @param dispatch_mode 事件发布模式
 * @param name 定时器名称
 * @return 定时器ID（0 表示失败）
 */
soft_timer_id_t soft_timer_create_simple_named(uint32_t interval, timer_mode_t mode,
                                               event_callback_t callback,
                                               event_dispatch_mode_t dispatch_mode,
                                               const char *name);

// ============================================================================
// 便捷宏 - 自动使用变量名作为定时器名称
// ============================================================================

/**
 * @brief 创建定时器，自动使用变量名作为定时器名称
 * @param var 存储定时器ID的变量名
 * @param ... 其他参数（同 soft_timer_create_named）
 *
 * 示例:
 *   soft_timer_id_t led_timer = SOFT_TIMER_CREATE(led_timer, 500, TIMER_MODE_PERIODIC, TIMER_EVENT_AUTO, NULL, 0, led_callback, EVENT_DISPATCH_SYNC);
 */
#define SOFT_TIMER_CREATE(var, ...) \
    ((var) = soft_timer_create_named(__VA_ARGS__, #var))

/**
 * @brief 创建定时器（简化版），自动使用变量名作为定时器名称
 * @param var 存储定时器ID的变量名
 * @param ... 其他参数（同 soft_timer_create_simple_named）
 *
 * 示例:
 *   soft_timer_id_t blink_timer = SOFT_TIMER_CREATE_SIMPLE(blink_timer, 1000, TIMER_MODE_PERIODIC, blink_callback, EVENT_DISPATCH_SYNC);
 */
#define SOFT_TIMER_CREATE_SIMPLE(var, ...) \
    ((var) = soft_timer_create_simple_named(__VA_ARGS__, #var))

/**
 * @brief 启动定时器，自动使用变量名作为定时器名称
 * @param var 存储定时器ID的变量名
 *
 * 示例:
 *   SOFT_TIMER_START(led_timer);
 */
#define SOFT_TIMER_START(var) \
    soft_timer_start_named((var), #var)

/**
 * @brief 设置定时器名称，自动使用变量名
 * @param var 存储定时器ID的变量名
 *
 * 示例:
 *   SOFT_TIMER_SET_NAME(led_timer);
 */
#define SOFT_TIMER_SET_NAME(var) \
    soft_timer_set_name((var), #var)

#ifdef __cplusplus
}
#endif

#endif // __SOFT_TIMER_H__
