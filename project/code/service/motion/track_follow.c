/**
 * @file track_follow.c
 * @brief 循迹控制服务实现
 */
#include "track_follow.h"

#include "driver/track.h"
#include "service/motion/chassis.h"
#include "service/com/param.h"
#include "service/sys/sys_log.h"
#include "common/pid/pid.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* -------------------------------------------------------------------------- */
/* 可调参数                                                                     */
/* -------------------------------------------------------------------------- */
static float track_base_v = TRACK_FOLLOW_BASE_V_DEFAULT;
static float track_kp = TRACK_FOLLOW_KP_DEFAULT;
static float track_ki = TRACK_FOLLOW_KI_DEFAULT;
static float track_kd = TRACK_FOLLOW_KD_DEFAULT;
static float track_lost_v = TRACK_FOLLOW_LOST_V_DEFAULT;
static float track_lost_w = TRACK_FOLLOW_LOST_W_DEFAULT;
static float track_lost_ms = TRACK_FOLLOW_LOST_MS_DEFAULT;
static float track_omega_max = TRACK_FOLLOW_OMEGA_MAX_DEFAULT;
static uint8_t track_polarity = TRACK_ACTIVE_LEVEL_DEF;
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
static uint8_t track_gs_thr = 30;
#endif

/* -------------------------------------------------------------------------- */
/* 运行时                                                                       */
/* -------------------------------------------------------------------------- */
static bool s_inited;
static track_follow_state_t s_state = TRACK_FOLLOW_IDLE;
static pid_controller_t s_pid;
static float s_run_v = TRACK_FOLLOW_BASE_V_DEFAULT;
static float s_lost_accum_ms;

/* -------------------------------------------------------------------------- */
/* 工具                                                                         */
/* -------------------------------------------------------------------------- */
static float clampf(float x, float lo, float hi)
{
    if (x > hi) {
        return hi;
    }
    if (x < lo) {
        return lo;
    }
    return x;
}

static const char *state_name(track_follow_state_t st)
{
    switch (st) {
        case TRACK_FOLLOW_IDLE:   return "idle";
        case TRACK_FOLLOW_FOLLOW: return "follow";
        case TRACK_FOLLOW_LOST:   return "lost";
        default:                  return "unknown";
    }
}

static void apply_pid_params(void)
{
    pid_update_params(&s_pid, track_kp, track_ki, track_kd);
    s_pid.output_max = track_omega_max;
    s_pid.output_min = -track_omega_max;
    pid_set_integral_limits(&s_pid, -track_omega_max, track_omega_max);
}

static void sync_polarity_to_driver(void)
{
    track_set_polarity(track_polarity);
}

#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
static void sync_gs_thr_to_driver(void)
{
    (void)track_set_gs_threshold(track_gs_thr);
}
#endif

static void enter_idle(void)
{
    s_state = TRACK_FOLLOW_IDLE;
    s_lost_accum_ms = 0.0f;
    pid_reset(&s_pid);
    chassis_stop();
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                     */
/* -------------------------------------------------------------------------- */
exit_code_t track_follow_init(void)
{
    exit_code_t ret;

    if (s_inited) {
        return EXIT_ALREADY_INITIALIZED;
    }

    ret = track_init();
    if (ret != EXIT_OK && ret != EXIT_ALREADY_INITIALIZED) {
        sys_log_text(error, "TrackFollow: track_init failed %s",
                     error_code_name(ret));
        return ret;
    }

    pid_init(&s_pid, track_kp, track_ki, track_kd, TRACK_FOLLOW_DT_S,
             -track_omega_max, track_omega_max);
    pid_set_integral_limits(&s_pid, -track_omega_max, track_omega_max);
    pid_reset(&s_pid);

    assert_fun(param_add("track_base_v", PARAM_TYPE_FLOAT, &track_base_v, true));
    assert_fun(param_add("track_kp", PARAM_TYPE_FLOAT, &track_kp, true));
    assert_fun(param_add("track_ki", PARAM_TYPE_FLOAT, &track_ki, true));
    assert_fun(param_add("track_kd", PARAM_TYPE_FLOAT, &track_kd, true));
    assert_fun(param_add("track_lost_v", PARAM_TYPE_FLOAT, &track_lost_v, true));
    assert_fun(param_add("track_lost_w", PARAM_TYPE_FLOAT, &track_lost_w, true));
    assert_fun(param_add("track_lost_ms", PARAM_TYPE_FLOAT, &track_lost_ms, true));
    assert_fun(param_add("track_omega_max", PARAM_TYPE_FLOAT, &track_omega_max, true));
    assert_fun(param_add("track_polarity", PARAM_TYPE_UINT8, &track_polarity, true));
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
    assert_fun(param_add("track_gs_thr", PARAM_TYPE_UINT8, &track_gs_thr, true));
#endif

    sync_polarity_to_driver();
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
    sync_gs_thr_to_driver();
#endif

    s_state = TRACK_FOLLOW_IDLE;
    s_run_v = track_base_v;
    s_lost_accum_ms = 0.0f;
    s_inited = true;

    sys_log_text(info, "TrackFollow: ready (backend=%s)", track_backend_name());
    return EXIT_OK;
}

void track_follow_start(float base_v)
{
    if (!s_inited) {
        sys_log_text(error, "TrackFollow: start before init");
        return;
    }

    if (base_v > 0.0f) {
        s_run_v = base_v;
        track_base_v = base_v;
    } else {
        s_run_v = track_base_v;
    }

    apply_pid_params();
    pid_reset(&s_pid);
    sync_polarity_to_driver();
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
    sync_gs_thr_to_driver();
#endif

    s_lost_accum_ms = 0.0f;
    s_state = TRACK_FOLLOW_FOLLOW;
    chassis_set_mode(CHASSIS_MODE_SPEED);
    chassis_set_velocity(s_run_v, 0.0f);

    sys_log_text(info, "TrackFollow: start v=%.2f", s_run_v);
}

void track_follow_stop(void)
{
    if (!s_inited) {
        return;
    }
    enter_idle();
    sys_log_text(info, "TrackFollow: stop");
}

track_follow_state_t track_follow_get_state(void)
{
    return s_state;
}

bool track_follow_is_active(void)
{
    return (s_state == TRACK_FOLLOW_FOLLOW) || (s_state == TRACK_FOLLOW_LOST);
}

void track_follow_update(void)
{
    float err;
    float omega;
    float v;
    float abs_err;

    if (!s_inited) {
        return;
    }

    /* 始终采样，便于 status / 调试 */
    track_scan();
    sync_polarity_to_driver();

    if (s_state == TRACK_FOLLOW_IDLE) {
        return;
    }

    apply_pid_params();
    err = track_get_error();

    if (track_is_lost()) {
        if (s_state != TRACK_FOLLOW_LOST) {
            s_state = TRACK_FOLLOW_LOST;
            s_lost_accum_ms = 0.0f;
            pid_reset(&s_pid);
            sys_log_text(warning, "TrackFollow: lost (last_err=%.2f)", err);
        }

        s_lost_accum_ms += TRACK_FOLLOW_DT_S * 1000.0f;
        if (s_lost_accum_ms >= track_lost_ms) {
            sys_log_text(warning, "TrackFollow: lost timeout -> stop");
            enter_idle();
            return;
        }

        /* 沿上次偏差方向搜索 */
        omega = (err >= 0.0f) ? track_lost_w : -track_lost_w;
        if (fabsf(err) < 1e-4f) {
            omega = track_lost_w; /* 无历史时默认一侧 */
        }
        chassis_set_velocity(track_lost_v, omega);
        return;
    }

    /* 找回线 */
    if (s_state == TRACK_FOLLOW_LOST) {
        s_state = TRACK_FOLLOW_FOLLOW;
        s_lost_accum_ms = 0.0f;
        pid_reset(&s_pid);
        sys_log_text(info, "TrackFollow: line recovered");
    }

    /* error>0 线偏右 → 应右转(ω>0) 视 chassis 符号；PID: setpoint=0, feedback=err */
    omega = pid_calculate(&s_pid, 0.0f, err);
    omega = clampf(omega, -track_omega_max, track_omega_max);

    abs_err = fabsf(err);
    v = s_run_v * (1.0f - 0.5f * abs_err);
    v = clampf(v, s_run_v * 0.3f, s_run_v);

    chassis_set_velocity(v, omega);
}

static void print_status(void)
{
    sys_log_text(terminal,
                 "track st=%s backend=%s mask=0x%02X on=%u lost=%u err=%.3f "
                 "pol=%u v=%.2f kp=%.2f ki=%.2f kd=%.2f",
                 state_name(s_state),
                 track_backend_name(),
                 (unsigned)track_get_mask(),
                 (unsigned)track_get_on_count(),
                 track_is_lost() ? 1u : 0u,
                 track_get_error(),
                 (unsigned)track_get_polarity(),
                 s_run_v,
                 track_kp, track_ki, track_kd);
}

cmd_exec_result_t track_follow_command_handler(i32 seq, int argc, char **argv)
{
    const char *cmd;
    const char *a1;

    (void)seq;

    if (argc < 2) {
        sys_log_text(terminal,
                     "Usage: track <status|start|stop|polarity|cal|param> ...");
        sys_log_text(terminal, "  start [v]");
        sys_log_text(terminal, "  polarity 0|1");
        sys_log_text(terminal, "  cal max|min   (GS08 only)");
        return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage_track");
    }

    if (!s_inited) {
        return CMD_EXEC_CTX(EXIT_NOT_INITIALIZED, "track_not_init");
    }

    cmd = argv[1];
    a1 = (argc >= 3) ? argv[2] : NULL;

    if (strcmp(cmd, "status") == 0) {
        print_status();
        return CMD_EXEC_CTX(EXIT_OK, "status_printed");
    }

    if (strcmp(cmd, "stop") == 0) {
        track_follow_stop();
        sys_log_text(terminal, "track stopped");
        return CMD_EXEC_CTX(EXIT_OK, "stopped");
    }

    if (strcmp(cmd, "start") == 0) {
        float v = track_base_v;
        if (a1 != NULL) {
            v = (float)atof(a1);
        }
        track_follow_start(v);
        sys_log_text(terminal, "track started v=%.2f", s_run_v);
        return CMD_EXEC_CTX(EXIT_OK, "started");
    }

    if (strcmp(cmd, "polarity") == 0) {
        if (a1 == NULL) {
            sys_log_text(terminal, "Usage: track polarity 0|1 (now=%u)",
                         (unsigned)track_get_polarity());
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "need_polarity");
        }
        track_polarity = (uint8_t)((atoi(a1) != 0) ? 1 : 0);
        track_set_polarity(track_polarity);
        sys_log_text(terminal, "track polarity=%u", (unsigned)track_polarity);
        return CMD_EXEC_CTX(EXIT_OK, "polarity_set");
    }

    if (strcmp(cmd, "param") == 0) {
        apply_pid_params();
        sync_polarity_to_driver();
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
        sync_gs_thr_to_driver();
#endif
        sys_log_text(terminal,
                     "track param applied kp=%.2f ki=%.2f kd=%.2f v=%.2f",
                     track_kp, track_ki, track_kd, track_base_v);
        return CMD_EXEC_CTX(EXIT_OK, "param_applied");
    }

    if (strcmp(cmd, "cal") == 0) {
        exit_code_t r;
        if (a1 == NULL) {
            sys_log_text(terminal, "Usage: track cal max|min");
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "need_cal_mode");
        }
        if (strcmp(a1, "max") == 0) {
            r = track_cal_set_max();
        } else if (strcmp(a1, "min") == 0) {
            r = track_cal_set_min();
        } else {
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "invalid_cal");
        }
        if (r == EXIT_NOT_SUPPORTED) {
            sys_log_text(terminal, "track cal only for GS08 backend");
            return CMD_EXEC_CTX(EXIT_NOT_SUPPORTED, "not_gs08");
        }
        if (r != EXIT_OK) {
            return CMD_EXEC_CTX(r, "cal_fail");
        }
        sys_log_text(terminal, "track cal %s ok", a1);
        return CMD_EXEC_CTX(EXIT_OK, "cal_ok");
    }

    sys_log_text(terminal, "Unknown track cmd: %s", cmd);
    return CMD_EXEC_CTX(EXIT_NOT_SUPPORTED, "unknown_cmd");
}
