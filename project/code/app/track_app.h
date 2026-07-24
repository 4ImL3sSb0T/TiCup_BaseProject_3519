/**
 * @file track_app.h
 * @brief 循迹调试 app：持续 YAW_RATE 循迹，独立用于调参
 *
 * 周期：main 10 ms → track_app_update()
 * 不跑任务状态机，只做「扫线 → PID → 底盘」闭环，便于调参。
 *
 * 控制：
 *   ω* = track_app_ff + track_app_sign * PID(0, track_error)   [deg/s]
 *   再钳 |ω| 到 [track_app_w_min, track_app_w_max]
 *   chassis: YAW_RATE + v = track_app_v
 *
 * 命令：track start|stop|status|scan|pol|cal
 */
#ifndef __TRACK_APP_H__
#define __TRACK_APP_H__

#include "common/tools/common_def.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRACK_APP_IDLE = 0,
    TRACK_APP_RUNNING,
} track_app_status_t;

exit_code_t track_app_init(void);
void track_app_update(void);

exit_code_t track_app_start(void);
void track_app_stop(void);

track_app_status_t track_app_get_status(void);
bool track_app_is_running(void);

cmd_exec_result_t track_app_command_handler(i32 seq, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* __TRACK_APP_H__ */
