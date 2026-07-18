/**
 * @file event.h
 * @brief 轻量级事件系统 - 支持发布/订阅模式
 * @author Jupiter Smart Car Team
 * @date 2024-12-24
 */

#ifndef __EVENT_H__
#define __EVENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// 配置参数
// ============================================================================

#define EVENT_ASYNC_PROCESS_INTERVAL_MS 1  // 异步事件处理间隔（毫秒）

/**
 * @brief 启用动态内存分配
 * 0 = 使用静态内存数组（编译时分配）
 * 1 = 使用动态内存分配（运行时分配，需要 malloc/free）
 */
#ifndef EVENT_USE_DYNAMIC_MEMORY
#define EVENT_USE_DYNAMIC_MEMORY    0
#endif

/**
 * @brief 启用中断安全保护
 * 0 = 不使用临界区保护（仅主循环使用）
 * 1 = 使用临界区保护（可在中断中使用）
 */
#ifndef EVENT_INTERRUPT_SAFE
#define EVENT_INTERRUPT_SAFE        1
#endif

/**
 * @brief 最大监听者数量（仅静态内存模式有效）
 */
#ifndef EVENT_MAX_LISTENERS
#define EVENT_MAX_LISTENERS         32
#endif

/**
 * @brief 最大事件类型数量（预留，暂未使用）
 */
#ifndef EVENT_MAX_TYPES
#define EVENT_MAX_TYPES             64
#endif

/**
 * @brief 是否启用异步发布队列（基于 zf_common_fifo）
 * 0 = 不启用，仅同步发布
 * 1 = 启用，可选择异步入队，需周期调用 event_process_async()
 */
#ifndef EVENT_ENABLE_ASYNC
#define EVENT_ENABLE_ASYNC          1
#endif

/**
 * @brief 异步事件队列容量（事件个数，异步模式有效）
 */
#ifndef EVENT_ASYNC_QUEUE_SIZE
#define EVENT_ASYNC_QUEUE_SIZE      32
#endif

/**
 * @brief 启用定时器性能统计（与 soft_timer.h 的 TIMER_ENABLE_PERF_STATS 同步）
 * 0 = 关闭
 * 1 = 开启
 */
#ifndef TIMER_ENABLE_PERF_STATS
#define TIMER_ENABLE_PERF_STATS     1
#endif

/**
 * @brief 事件类型定义
 */
typedef uint16_t event_type_t;
typedef uintptr_t event_listener_id_t;
#define EVENT_LISTENER_ID_INVALID ((event_listener_id_t)0)

/**
 * @brief 事件数据结构
 */
typedef struct {
    event_type_t type;      // 事件类型
    void *data;             // 事件数据指针
    uint16_t data_size;     // 数据大小（字节）
    uint32_t timestamp;     // 事件时间戳（毫秒）
#if TIMER_ENABLE_PERF_STATS
    uintptr_t timer_id;     // 触发此事件的定时器 ID（0 表示非定时器事件）
    uint32_t enqueue_cycles;// 入队时的 DWT 周期数（用于测量队列延迟）
#endif
} event_t;

/**
 * @brief 事件发布模式
 */
typedef enum {
    EVENT_DISPATCH_SYNC = 0,    // 同步：立即触发回调
    EVENT_DISPATCH_ASYNC = 1,   // 异步：入队，等待 event_process_async 处理
} event_dispatch_mode_t;

/**
 * @brief 事件回调函数类型
 * @param event 事件指针
 * @param user_data 用户自定义数据
 */
typedef void (*event_callback_t)(const event_t *event, void *user_data);

/**
 * @brief 事件监听者结构（内部使用）
 */
typedef struct event_listener_node {
    event_type_t type;              // 监听的事件类型
    event_callback_t callback;      // 回调函数
    void *user_data;                // 用户数据
    bool enabled;                   // 是否启用
#if EVENT_USE_DYNAMIC_MEMORY
    struct event_listener_node *next;  // 链表指针（动态内存模式）
#endif
} event_listener_t;

/**
 * @brief 事件系统统计信息
 */
typedef struct {
    uint32_t total_events;          // 总发布事件数
    uint32_t total_dispatches;      // 总分发次数
    uint16_t active_listeners;      // 活跃监听者数量
} event_stats_t;

/**
 * @brief 初始化事件系统
 *
 * @note 静态内存模式：初始化静态数组
 * @note 动态内存模式：初始化链表头指针
 */
void event_init(void);

#if EVENT_USE_DYNAMIC_MEMORY
/**
 * @brief 释放事件系统资源（动态内存模式）
 *
 * @note 释放所有动态分配的监听者节点
 */
void event_deinit(void);
#endif

/**
 * @brief 注册事件监听者
 * @param type 事件类型
 * @param callback 回调函数
 * @param user_data 用户数据（可选，可为NULL）
 * @return 监听者ID（0表示失败）
 */
event_listener_id_t event_subscribe(event_type_t type, event_callback_t callback, void *user_data);

/**
 * @brief 取消事件监听
 * @param listener_id 监听者ID
 * @return true=成功, false=失败
 */
bool event_unsubscribe(event_listener_id_t listener_id);

/**
 * @brief 启用/禁用监听者
 * @param listener_id 监听者ID
 * @param enabled true=启用, false=禁用
 * @return true=成功, false=失败
 */
bool event_set_listener_enabled(event_listener_id_t listener_id, bool enabled);

/**
 * @brief 发布事件
 * @param type 事件类型
 * @param data 事件数据指针（可选）
 * @param data_size 数据大小
 * @return 被触发的监听者数量
 */
uint16_t event_publish(event_type_t type, void *data, uint16_t data_size);

/**
 * @brief 发布事件（可选同步/异步）
 * @param type 事件类型
 * @param data 事件数据指针（异步时需保证在事件出队前有效）
 * @param data_size 数据大小
 * @param mode 发布模式（同步/异步）
 * @return 同步：回调触发数量；异步：入队成功返回1，失败返回0
 */
uint16_t event_publish_ex(event_type_t type, void *data, uint16_t data_size, event_dispatch_mode_t mode);

/**
 * @brief 发布事件（异步入队）
 * @param type 事件类型
 * @param data 事件数据指针（需保证在事件出队前有效）
 * @param data_size 数据大小
 * @return 入队成功返回1，失败返回0
 */
uint16_t event_publish_async(event_type_t type, void *data, uint16_t data_size);

/**
 * @brief 发布事件（不带数据）
 * @param type 事件类型
 * @return 被触发的监听者数量
 */
uint16_t event_publish_simple(event_type_t type);

/**
 * @brief 取消注册所有指定类型的监听者
 * @param type 事件类型
 * @return 被取消的监听者数量
 */
uint16_t event_unsubscribe_all(event_type_t type);

/**
 * @brief 清空所有监听者
 */
void event_clear_all(void);

/**
 * @brief 获取事件系统统计信息
 * @param stats 统计信息结构指针
 */
void event_get_stats(event_stats_t *stats);

/**
 * @brief 获取活跃监听者数量
 * @return 活跃监听者数量
 */
uint16_t event_get_listener_count(void);

/**
 * @brief 处理异步事件队列（仅 EVENT_ENABLE_ASYNC = 1 有效）
 * @return 本次处理触发的回调次数
 */
uint16_t event_process_async(void);

#ifdef __cplusplus
}
#endif

#endif // __EVENT_H__
