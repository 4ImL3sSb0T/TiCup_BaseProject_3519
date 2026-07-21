/**
 * @file track_follow.h
 * @brief 循迹控制服务：采样驱动 → 偏差 PID → chassis v/ω
 *
 * 依赖 driver/track（传感）与 chassis（差速底盘）。
 * 周期建议与 chassis 同频 10 ms（TRACK_FOLLOW_DT_S）。
 *
 * 状态：
 *   IDLE   只采样，不写底盘
 *   FOLLOW SPEED 模式，v=base_v，ω=PID(0, error)
 *   LOST   沿 last_error 方向搜索，超时 stop
 */
#ifndef __TRACK_FOLLOW_H__
#define __TRACK_FOLLOW_H__

#include "common/tools/common_def.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 与 soft_timer / chassis 10 ms 一致 */
#define TRACK_FOLLOW_DT_S               (0.01f)

#define TRACK_FOLLOW_BASE_V_DEFAULT     (8.0f)
#define TRACK_FOLLOW_KP_DEFAULT         (1.5f)
#define TRACK_FOLLOW_KI_DEFAULT         (0.0f)
#define TRACK_FOLLOW_KD_DEFAULT         (0.05f)
#define TRACK_FOLLOW_LOST_V_DEFAULT     (3.0f)
#define TRACK_FOLLOW_LOST_W_DEFAULT     (12.0f)
#define TRACK_FOLLOW_LOST_MS_DEFAULT    (1000.0f)
#define TRACK_FOLLOW_OMEGA_MAX_DEFAULT  (40.0f)

typedef enum {
    TRACK_FOLLOW_IDLE = 0,
    TRACK_FOLLOW_FOLLOW,
    TRACK_FOLLOW_LOST
} track_follow_state_t;

/**
 * @brief 注册 param / PID；调用 track_init()（传感）
 * @note  须在 chassis_init 之后；不自动 start
 */
exit_code_t track_follow_init(void);

/**
 * @brief 周期任务：track_scan + 状态机 + chassis_set_velocity
 */
void track_follow_update(void);

void track_follow_start(float base_v);
void track_follow_stop(void);

track_follow_state_t track_follow_get_state(void);
bool track_follow_is_active(void);

cmd_exec_result_t track_follow_command_handler(i32 seq, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* __TRACK_FOLLOW_H__ */
