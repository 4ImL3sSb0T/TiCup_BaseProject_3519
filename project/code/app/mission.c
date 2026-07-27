/**
 * @file mission.c
 * @brief 表驱动 mission：任务编排与循迹退出条件
 */
#include "app/mission.h"

#include "app/track_app.h"
#include "bsp/notice/notice.h"
#include "driver/track.h"
#include "service/imu/imu.h"
#include "service/motion/chassis.h"
#include "service/sys/sys_log.h"
#include "service/sys/sys_time.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
#define MISSION_LINE_STABLE_N     (3u)
#define MISSION_LINE_ARM_MS       (300u)
#define MISSION_ARC_MIN_MS        (400u)
#define MISSION_TRACK_TMO_MS      (20000u)
#define MISSION_STRAIGHT_TMO_MS   (20000u)
#define MISSION_ARC_TMO_MS        (25000u)
#define MISSION_ALIGN_TMO_MS      (10000u)
#define MISSION_ALIGN_MIN_MS      (100u)
#define MISSION_ALIGN_TOL_DEG     (5.0f)
#define MISSION_ALIGN_STABLE_N    (3u)
#define MISSION_NOTIFY_MS_DEF     (120u)

#define MISSION_HDG_AC            (-38.7f)
#define MISSION_HDG_BD            (-141.3f)

/* -------------------------------------------------------------------------- */
typedef enum {
    ST_NOTIFY = 0,
    ST_TRACK_TO_LOST,
    ST_STRAIGHT,
    ST_ALIGN,        /* 原地对齐到 yaw_base+ang */
    ST_ARC,
    ST_STOP,
    ST_LAP,
} step_type_t;

typedef struct {
    uint8_t  type;
    float    ang;        /* STRAIGHT/ALIGN: 相对 heading；ARC: yaw_delta */
    int8_t   track_sign; /* TRACK/ARC: error→ω 符号 */
    uint16_t ms;         /* NOTIFY/STOP 时长；STRAIGHT 非 0 时为固定撞线屏蔽时间 */
} step_t;

static const step_t s_steps_1[] = {
    { ST_NOTIFY,        0.0f,  0, MISSION_NOTIFY_MS_DEF },
    { ST_TRACK_TO_LOST, 0.0f,  1, 0 },
    { ST_STRAIGHT,      0.0f,  0, MISSION_LINE_ARM_MS },
    { ST_NOTIFY,        0.0f,  0, MISSION_NOTIFY_MS_DEF },
    { ST_STOP,          0.0f,  0, MISSION_NOTIFY_MS_DEF },
};

static const step_t s_steps_2[] = {
    { ST_NOTIFY,   0.0f,    0, MISSION_NOTIFY_MS_DEF },
    { ST_STRAIGHT, 0.0f,    0, 0 },
    { ST_NOTIFY,   0.0f,    0, MISSION_NOTIFY_MS_DEF },
    { ST_ARC,     -180.0f,  1, 0 },
    { ST_NOTIFY,   0.0f,    0, MISSION_NOTIFY_MS_DEF },
    /* C→D：离开 C 点线后再认 D，固定屏蔽 2s 避免假撞线 */
    { ST_STRAIGHT, 180.0f,  0, 2000 },
    { ST_NOTIFY,   0.0f,    0, MISSION_NOTIFY_MS_DEF },
    /* DA 与 BC 同极性（现场：BC 正常、原 DA sign=-1 拧反） */
    { ST_ARC,      180.0f,  1, 0 },
    { ST_NOTIFY,   0.0f,    0, MISSION_NOTIFY_MS_DEF },
    { ST_STOP,     0.0f,    0, MISSION_NOTIFY_MS_DEF },
};

/* A→C → 弧CB → B→D(-141.3) 撞线 → 恢复角度(yaw_base) → 循迹至丢线 → 停 */
static const step_t s_steps_3[] = {
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_STRAIGHT,      MISSION_HDG_AC, 0, 0 },
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_ARC,           180.0f,         1, 0 },
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_STRAIGHT,      MISSION_HDG_BD, 0, 0 },
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_ALIGN,         0.0f,           0, 0 },
    { ST_TRACK_TO_LOST, 0.0f,           1, 0 },
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_STOP,          0.0f,           0, MISSION_NOTIFY_MS_DEF },
};

/* 同任务3路径 × N 圈 */
static const step_t s_steps_4[] = {
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_STRAIGHT,      MISSION_HDG_AC, 0, 0 },
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_ARC,           180.0f,         1, 0 },
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_STRAIGHT,      MISSION_HDG_BD, 0, 0 },
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_ALIGN,         0.0f,           0, 0 },
    { ST_TRACK_TO_LOST, 0.0f,           1, 0 },
    { ST_NOTIFY,        0.0f,           0, MISSION_NOTIFY_MS_DEF },
    { ST_LAP,           0.0f,           0, 0 },
    { ST_STOP,          0.0f,           0, MISSION_NOTIFY_MS_DEF },
};

/* -------------------------------------------------------------------------- */
static bool s_inited;
static mission_id_t s_id;
static mission_status_t s_status;

static const step_t *s_steps;
static uint8_t s_step_n;
static uint8_t s_step_i;

static uint8_t s_lap;
static uint8_t s_laps_total;

static float s_yaw_base;
static float s_yaw_enter;
static uint32_t s_phase_ms;
static uint8_t s_line_stable;
static bool s_line_armed;

/* -------------------------------------------------------------------------- */
static float wrap_deg(float e)
{
    while (e > 180.0f) {
        e -= 360.0f;
    }
    while (e <= -180.0f) {
        e += 360.0f;
    }
    return e;
}

static float read_yaw(void)
{
    return imu_get_attitude().z;
}

static void mission_fail(const char *why)
{
    notice_off();
    chassis_stop();
    s_status = MISSION_STATUS_FAILED;
    sys_log_text(mission, "status=failed step=%u reason=%s",
                 (unsigned)s_step_i + 1u, why);
}

static const step_t *cur_step(void)
{
    if (s_steps == NULL || s_step_i >= s_step_n) {
        return NULL;
    }
    return &s_steps[s_step_i];
}

static const char *step_name(step_type_t type)
{
    switch (type) {
        case ST_NOTIFY:        return "notify";
        case ST_TRACK_TO_LOST: return "track_to_lost";
        case ST_STRAIGHT:      return "straight";
        case ST_ALIGN:         return "align";
        case ST_ARC:           return "arc";
        case ST_STOP:          return "stop";
        case ST_LAP:           return "lap";
        default:               return "unknown";
    }
}

static void enter_step(void);

static void next_step(void)
{
    s_step_i++;
    if (s_step_i >= s_step_n) {
        chassis_stop();
        notice_off();
        s_status = MISSION_STATUS_COMPLETED;
        sys_log_text(mission, "status=completed");
        return;
    }
    enter_step();
}

static void enter_step(void)
{
    const step_t *st = cur_step();

    s_phase_ms = sys_time_get_ms();
    s_line_stable = 0;
    s_line_armed = false;
    track_control_reset();

    if (st == NULL) {
        mission_fail("null_step");
        return;
    }

    sys_log_text(mission, "step id=%u index=%u/%u type=%s",
                 (unsigned)s_id + 1u, (unsigned)s_step_i + 1u,
                 (unsigned)s_step_n, step_name((step_type_t)st->type));

    switch ((step_type_t)st->type) {
        case ST_NOTIFY:
            chassis_stop();
            notice_beep(st->ms ? st->ms : MISSION_NOTIFY_MS_DEF);
            break;

        case ST_STRAIGHT:
            notice_off();
            chassis_set_mode(CHASSIS_MODE_HEADING);
            chassis_set_heading(wrap_deg(s_yaw_base + st->ang));
            chassis_set_velocity(track_control_get_velocity(), 0.0f);
            break;

        case ST_ALIGN:
            /* 原地转到 yaw_base+ang，供撞线后恢复航向再循迹 */
            notice_off();
            chassis_set_mode(CHASSIS_MODE_HEADING);
            chassis_set_heading(wrap_deg(s_yaw_base + st->ang));
            chassis_set_velocity(0.0f, 0.0f);
            break;

        case ST_TRACK_TO_LOST:
            notice_off();
            chassis_set_mode(CHASSIS_MODE_YAW_RATE);
            chassis_set_velocity(track_control_get_velocity(), 0.0f);
            break;

        case ST_ARC:
            notice_off();
            s_yaw_enter = read_yaw();
            chassis_set_mode(CHASSIS_MODE_YAW_RATE);
            chassis_set_velocity(track_control_get_velocity(), 0.0f);
            break;

        case ST_STOP:
            chassis_stop();
            notice_beep(st->ms ? st->ms : MISSION_NOTIFY_MS_DEF);
            break;

        case ST_LAP:
            notice_off();
            chassis_stop();
            break;

        default:
            mission_fail("bad_type");
            break;
    }
}

static bool line_hit(void)
{
    track_scan();
    if (track_get_on_count() >= 1u) {
        if (s_line_stable < 255u) {
            s_line_stable++;
        }
    } else {
        s_line_stable = 0;
    }
    return s_line_stable >= MISSION_LINE_STABLE_N;
}

/** 纯循迹，确认见线后连续丢线 3 帧；切直行用 start 时的 yaw_base。 */
static void run_track_to_lost(const step_t *st)
{
    uint32_t now = sys_time_get_ms();
    int8_t tsign = (st->track_sign >= 0) ? 1 : -1;

    track_control_update(tsign);

    if (track_get_on_count() > 0u) {
        s_line_armed = true;
        s_line_stable = 0u;
    } else if (s_line_armed) {
        if (s_line_stable < 255u) {
            s_line_stable++;
        }
        if (s_line_stable >= MISSION_LINE_STABLE_N) {
            sys_log_text(mission, "event=track_lost yaw_base=%.1f", s_yaw_base);
            next_step();
            return;
        }
    }

    if ((now - s_phase_ms) > MISSION_TRACK_TMO_MS) {
        mission_fail("track_timeout");
        return;
    }

}

static void run_straight(const step_t *st)
{
    uint32_t now = sys_time_get_ms();
    uint32_t elapsed = now - s_phase_ms;

    chassis_set_mode(CHASSIS_MODE_HEADING);
    chassis_set_heading(wrap_deg(s_yaw_base + st->ang));
    chassis_set_velocity(track_control_get_velocity(), 0.0f);

    if (elapsed > MISSION_STRAIGHT_TMO_MS) {
        mission_fail("straight_timeout");
        return;
    }

    if (!s_line_armed) {
        track_scan();
        if ((st->ms != 0u && elapsed >= st->ms) ||
            (st->ms == 0u &&
             (track_get_on_count() == 0u || elapsed >= MISSION_LINE_ARM_MS))) {
            s_line_armed = true;
            s_line_stable = 0;
        }
        return;
    }

    if (line_hit()) {
        next_step();
    }
}

/** 原地对齐到 yaw_base+ang，航向误差连续稳定后进入下一步。 */
static void run_align(const step_t *st)
{
    uint32_t now = sys_time_get_ms();
    uint32_t elapsed = now - s_phase_ms;
    float target = wrap_deg(s_yaw_base + st->ang);
    float err;

    chassis_set_mode(CHASSIS_MODE_HEADING);
    chassis_set_heading(target);
    chassis_set_velocity(0.0f, 0.0f);

    if (elapsed > MISSION_ALIGN_TMO_MS) {
        mission_fail("align_timeout");
        return;
    }

    err = wrap_deg(target - read_yaw());
    if (err < 0.0f) {
        err = -err;
    }

    if (err <= MISSION_ALIGN_TOL_DEG) {
        if (s_line_stable < 255u) {
            s_line_stable++;
        }
    } else {
        s_line_stable = 0;
    }

    if (elapsed >= MISSION_ALIGN_MIN_MS &&
        s_line_stable >= MISSION_ALIGN_STABLE_N) {
        sys_log_text(mission, "event=align_ok tgt=%.1f yaw=%.1f",
                     target, read_yaw());
        next_step();
    }
}

/**
 * 弧：YAW_RATE
 *   ω* = track_sign * PID(0, error)   [deg/s]
 * 出弧：|Δyaw| 达 yaw_delta
 */
static void run_arc(const step_t *st)
{
    uint32_t now = sys_time_get_ms();
    float yaw_delta = st->ang;
    float sign = (yaw_delta >= 0.0f) ? 1.0f : -1.0f;
    float target = (yaw_delta >= 0.0f) ? yaw_delta : -yaw_delta;
    float turned;
    int8_t tsign = (st->track_sign >= 0) ? 1 : -1;

    if (target < 1.0f) {
        target = 180.0f;
    }

    track_control_update(tsign);

    if ((now - s_phase_ms) > MISSION_ARC_TMO_MS) {
        mission_fail("arc_timeout");
        return;
    }

    turned = wrap_deg(read_yaw() - s_yaw_enter);
    if (sign < 0.0f) {
        turned = -turned;
    }
    if (turned < 0.0f) {
        turned = 0.0f;
    }

    if ((now - s_phase_ms) >= MISSION_ARC_MIN_MS && turned >= (target - 5.0f)) {
        next_step();
    }
}

static void run_notify(const step_t *st)
{
    uint16_t ms = st->ms ? st->ms : MISSION_NOTIFY_MS_DEF;

    /* beep 由 notice_update 关断；本步用时长推进 */
    if ((sys_time_get_ms() - s_phase_ms) >= ms) {
        notice_off();
        next_step();
    }
}

static void run_stop(const step_t *st)
{
    uint16_t ms = st->ms ? st->ms : MISSION_NOTIFY_MS_DEF;

    chassis_stop();
    if ((sys_time_get_ms() - s_phase_ms) >= ms) {
        notice_off();
        s_status = MISSION_STATUS_COMPLETED;
        sys_log_text(mission, "status=completed step=stop");
    }
}

static void run_lap(void)
{
    if (s_lap < s_laps_total) {
        s_lap++;
        sys_log_text(mission, "event=lap lap=%u/%u",
                     (unsigned)s_lap, (unsigned)s_laps_total);
        s_step_i = 1;
        enter_step();
    } else {
        next_step();
    }
}

static void select_table(mission_id_t id)
{
    switch (id) {
        case MISSION_ID_1:
            s_steps = s_steps_1;
            s_step_n = (uint8_t)(sizeof(s_steps_1) / sizeof(s_steps_1[0]));
            break;
        case MISSION_ID_2:
            s_steps = s_steps_2;
            s_step_n = (uint8_t)(sizeof(s_steps_2) / sizeof(s_steps_2[0]));
            break;
        case MISSION_ID_3:
            s_steps = s_steps_3;
            s_step_n = (uint8_t)(sizeof(s_steps_3) / sizeof(s_steps_3[0]));
            break;
        case MISSION_ID_4:
            s_steps = s_steps_4;
            s_step_n = (uint8_t)(sizeof(s_steps_4) / sizeof(s_steps_4[0]));
            break;
        default:
            s_steps = NULL;
            s_step_n = 0;
            break;
    }
}

/* -------------------------------------------------------------------------- */
exit_code_t mission_init(void)
{
    exit_code_t nec;
    exit_code_t tec;

    if (s_inited) {
        return EXIT_ALREADY_INITIALIZED;
    }

    nec = notice_init();
    if (nec != EXIT_OK && nec != EXIT_ALREADY_INITIALIZED) {
        sys_log_text(error, "mission: notice_init failed");
        return nec;
    }

    tec = track_app_init();
    if (tec != EXIT_OK && tec != EXIT_ALREADY_INITIALIZED) {
        sys_log_text(error, "mission: track_app_init failed");
        return tec;
    }

    s_status = MISSION_STATUS_IDLE;
    s_inited = true;
    sys_log_text(mission, "status=idle v=%.1f", track_control_get_velocity());
    return EXIT_OK;
}

exit_code_t mission_start(mission_id_t id, uint8_t laps)
{
    if (!s_inited) {
        return EXIT_NOT_INITIALIZED;
    }
    if (s_status == MISSION_STATUS_RUNNING) {
        return EXIT_BUSY;
    }
    if (track_app_is_running()) {
        sys_log_text(warning, "mission: track_app running, stop track first");
        return EXIT_BUSY;
    }
    if (id > MISSION_ID_4) {
        return EXIT_INVALID_PARAM;
    }

    select_table(id);
    if (s_steps == NULL || s_step_n == 0u) {
        return EXIT_INVALID_PARAM;
    }

    s_id = id;
    s_step_i = 0;
    s_laps_total = (id == MISSION_ID_4) ? ((laps == 0u) ? 4u : laps) : 1u;
    s_lap = 1;
    s_yaw_base = read_yaw();
    s_status = MISSION_STATUS_RUNNING;

    sys_log_text(mission, "status=running id=%u laps=%u yaw_base=%.1f",
                 (unsigned)id + 1u, (unsigned)s_laps_total, s_yaw_base);
    notice_off();
    enter_step();
    return EXIT_OK;
}

void mission_stop(void)
{
    if (!s_inited) {
        return;
    }
    notice_off();
    chassis_stop();
    s_status = MISSION_STATUS_ABORTED;
    sys_log_text(mission, "status=aborted");
}

mission_status_t mission_get_status(void)
{
    return s_status;
}

mission_id_t mission_get_id(void)
{
    return s_id;
}

void mission_update(void)
{
    const step_t *st;

    notice_update();

    if (!s_inited || s_status != MISSION_STATUS_RUNNING) {
        return;
    }

    st = cur_step();
    if (st == NULL) {
        mission_fail("no_step");
        return;
    }

    switch ((step_type_t)st->type) {
        case ST_NOTIFY:
            run_notify(st);
            break;
        case ST_TRACK_TO_LOST:
            run_track_to_lost(st);
            break;
        case ST_STRAIGHT:
            run_straight(st);
            break;
        case ST_ALIGN:
            run_align(st);
            break;
        case ST_ARC:
            run_arc(st);
            break;
        case ST_STOP:
            run_stop(st);
            break;
        case ST_LAP:
            run_lap();
            break;
        default:
            mission_fail("bad_type");
            break;
    }
}

static const char *status_name(mission_status_t s)
{
    switch (s) {
        case MISSION_STATUS_IDLE:      return "idle";
        case MISSION_STATUS_RUNNING:   return "running";
        case MISSION_STATUS_COMPLETED: return "completed";
        case MISSION_STATUS_FAILED:    return "failed";
        case MISSION_STATUS_ABORTED:   return "aborted";
        default:                       return "?";
    }
}

cmd_exec_result_t mission_command_handler(i32 seq, int argc, char **argv)
{
    const char *cmd;
    int id_n;
    int laps;

    (void)seq;

    if (!s_inited) {
        return CMD_EXEC_CTX(EXIT_NOT_INITIALIZED, "mission_not_init");
    }
    if (argc < 2) {
        sys_log_text(terminal, "Usage: mission start <1-4> [laps]|stop|status");
        return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage");
    }

    cmd = argv[1];

    if (strcmp(cmd, "stop") == 0) {
        mission_stop();
        return CMD_EXEC_CTX(EXIT_OK, "stopped");
    }

    if (strcmp(cmd, "status") == 0) {
        sys_log_text(terminal,
                     "mission: %s id=%u step=%u/%u lap=%u/%u v=%.1f",
                     status_name(s_status), (unsigned)s_id + 1u,
                     (unsigned)s_step_i, (unsigned)s_step_n,
                     (unsigned)s_lap, (unsigned)s_laps_total,
                     track_control_get_velocity());
        return CMD_EXEC_CTX(EXIT_OK, "status");
    }

    if (strcmp(cmd, "start") == 0) {
        if (argc < 3) {
            sys_log_text(terminal, "Usage: mission start <1-4> [laps]");
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage");
        }
        id_n = atoi(argv[2]);
        laps = (argc >= 4) ? atoi(argv[3]) : 0;
        if (id_n < 1 || id_n > 4) {
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "id");
        }
        {
            exit_code_t ec = mission_start((mission_id_t)(id_n - 1), (uint8_t)laps);
            if (ec != EXIT_OK) {
                return CMD_EXEC_CTX(ec, "start_fail");
            }
        }
        return CMD_EXEC_CTX(EXIT_OK, "started");
    }

    sys_log_text(terminal, "Usage: mission start <1-4> [laps]|stop|status");
    return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage");
}
