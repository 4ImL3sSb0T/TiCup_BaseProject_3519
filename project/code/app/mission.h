/**
 * @file mission.h
 * @brief H 题任务状态机：直行 HEADING 撞线 + 弧段 YAW_RATE（前馈 ω + 循迹 PID）
 *
 * 周期：main 独立 10 ms → mission_update()
 * 声光：bsp/notice（A14）
 */
#ifndef __MISSION_H__
#define __MISSION_H__

#include "common/tools/common_def.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MISSION_ID_1 = 0, /* A→B 停车 */
    MISSION_ID_2,     /* A→B→弧BC→C→D→弧DA→A */
    MISSION_ID_3,     /* A→C→弧CB→B→D→弧DA→A */
    MISSION_ID_4,     /* 路径3 × N 圈 */
} mission_id_t;

typedef enum {
    MISSION_STATUS_IDLE = 0,
    MISSION_STATUS_RUNNING,
    MISSION_STATUS_COMPLETED,
    MISSION_STATUS_FAILED,
    MISSION_STATUS_ABORTED,
} mission_status_t;

exit_code_t mission_init(void);
void mission_update(void);

/** laps 仅 ID_4 有效；其它忽略。0 → 默认 4 圈 */
exit_code_t mission_start(mission_id_t id, uint8_t laps);
void mission_stop(void);

mission_status_t mission_get_status(void);
mission_id_t mission_get_id(void);

cmd_exec_result_t mission_command_handler(i32 seq, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* __MISSION_H__ */
