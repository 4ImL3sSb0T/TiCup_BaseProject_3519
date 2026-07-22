/**
 * @file track_app.c
 * @brief 循迹调试 app：从 mission 弧段抽出的持续循迹闭环
 */
#include "app/track_app.h"

#include "app/mission.h"
#include "common/pid/pid.h"
#include "driver/track.h"
#include "service/com/param.h"
#include "service/motion/chassis.h"
#include "service/sys/sys_log.h"
#include "service/sys/sys_time.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
#define TRACK_APP_DT_S            (0.01f)

/** 与 mission 默认对齐，方便迁参 */
#define TRACK_APP_V_DEFAULT       (15.0f)
#define TRACK_APP_KP_DEFAULT      (40.0f)
#define TRACK_APP_KI_DEFAULT      (0.0f)
#define TRACK_APP_KD_DEFAULT      (0.0f)
#define TRACK_APP_W_MAX_DEFAULT   (80.0f)
#define TRACK_APP_W_MIN_DEFAULT   (0.0f)   /* 调试直行循迹一般不需要最小 |ω| */
#define TRACK_APP_FF_DEFAULT      (0.0f)   /* 弧前馈；0=纯循迹 */
#define TRACK_APP_SIGN_DEFAULT    (1.0f)   /* error→ω 符号；反了改 -1 */

/* -------------------------------------------------------------------------- */
static bool s_inited;
static track_app_status_t s_status;

static pid_controller_t s_pid;
static float s_cmd_omega;
static uint32_t s_lost_since_ms;
static bool s_was_lost;

static float track_app_v = TRACK_APP_V_DEFAULT;
static float track_app_kp = TRACK_APP_KP_DEFAULT;
static float track_app_ki = TRACK_APP_KI_DEFAULT;
static float track_app_kd = TRACK_APP_KD_DEFAULT;
static float track_app_w_max = TRACK_APP_W_MAX_DEFAULT;
static float track_app_w_min = TRACK_APP_W_MIN_DEFAULT;
static float track_app_ff = TRACK_APP_FF_DEFAULT;
static float track_app_sign = TRACK_APP_SIGN_DEFAULT;
static float track_app_lost_ms = 0.0f; /* param 用 float；0=不超时 */

/* -------------------------------------------------------------------------- */
static float clamp_abs_range(float w, float w_min, float w_max)
{
    float aw;
    float lo = w_min;
    float hi = w_max;

    if (hi < 0.0f) {
        hi = 0.0f;
    }
    if (lo < 0.0f) {
        lo = 0.0f;
    }
    if (lo > hi) {
        lo = hi;
    }

    aw = (w >= 0.0f) ? w : -w;
    if (aw > hi) {
        aw = hi;
    }
    if (aw > 0.0f && aw < lo) {
        aw = lo;
    }
    return (w >= 0.0f) ? aw : -aw;
}

static void apply_pid_params(void)
{
    float wmax = track_app_w_max;

    if (wmax < 0.0f) {
        wmax = 0.0f;
    }
    pid_update_params(&s_pid, track_app_kp, track_app_ki, track_app_kd);
    s_pid.output_max = wmax;
    s_pid.output_min = -wmax;
    s_pid.integral_max = wmax;
    s_pid.integral_min = -wmax;
}

static void log_sensor_once(void)
{
    track_scan();
    sys_log_text(terminal,
                 "track: mask=0x%02X on=%u err=%.3f lost=%u pol=%u backend=%s",
                 (unsigned)track_get_mask(),
                 (unsigned)track_get_on_count(),
                 (double)track_get_error(),
                 track_is_lost() ? 1u : 0u,
                 (unsigned)track_get_polarity(),
                 track_backend_name());
}

/* -------------------------------------------------------------------------- */
exit_code_t track_app_init(void)
{
    exit_code_t tec;
    float wmax;

    if (s_inited) {
        return EXIT_ALREADY_INITIALIZED;
    }

    tec = track_init();
    if (tec != EXIT_OK && tec != EXIT_ALREADY_INITIALIZED) {
        sys_log_text(error, "track_app: track_init failed");
        return tec;
    }

    wmax = TRACK_APP_W_MAX_DEFAULT;
    pid_init(&s_pid, track_app_kp, track_app_ki, track_app_kd,
             TRACK_APP_DT_S, -wmax, wmax);
    pid_reset(&s_pid);

    (void)param_add("track_app_v", PARAM_TYPE_FLOAT, &track_app_v, true);
    (void)param_add("track_app_kp", PARAM_TYPE_FLOAT, &track_app_kp, true);
    (void)param_add("track_app_ki", PARAM_TYPE_FLOAT, &track_app_ki, true);
    (void)param_add("track_app_kd", PARAM_TYPE_FLOAT, &track_app_kd, true);
    (void)param_add("track_app_w_max", PARAM_TYPE_FLOAT, &track_app_w_max, true);
    (void)param_add("track_app_w_min", PARAM_TYPE_FLOAT, &track_app_w_min, true);
    (void)param_add("track_app_ff", PARAM_TYPE_FLOAT, &track_app_ff, true);
    (void)param_add("track_app_sign", PARAM_TYPE_FLOAT, &track_app_sign, true);
    (void)param_add("track_app_lost_ms", PARAM_TYPE_FLOAT, &track_app_lost_ms, true);

    s_status = TRACK_APP_IDLE;
    s_cmd_omega = 0.0f;
    s_was_lost = true;
    s_inited = true;

    sys_log_text(info, "track_app ready (v=%.1f kp=%.1f sign=%.0f)",
                 (double)track_app_v, (double)track_app_kp,
                 (double)track_app_sign);
    return EXIT_OK;
}

exit_code_t track_app_start(void)
{
    if (!s_inited) {
        return EXIT_NOT_INITIALIZED;
    }
    if (s_status == TRACK_APP_RUNNING) {
        return EXIT_BUSY;
    }
    if (mission_get_status() == MISSION_STATUS_RUNNING) {
        sys_log_text(warning, "track_app: mission running, stop mission first");
        return EXIT_BUSY;
    }

    apply_pid_params();
    pid_reset(&s_pid);
    s_cmd_omega = 0.0f;
    s_was_lost = true;
    s_lost_since_ms = sys_time_get_ms();
    s_status = TRACK_APP_RUNNING;

    chassis_set_mode(CHASSIS_MODE_YAW_RATE);
    chassis_set_velocity(track_app_v, 0.0f);

    sys_log_text(info, "track_app START v=%.1f ff=%.1f sign=%.0f",
                 (double)track_app_v, (double)track_app_ff,
                 (double)track_app_sign);
    return EXIT_OK;
}

void track_app_stop(void)
{
    if (!s_inited) {
        return;
    }
    chassis_stop();
    s_cmd_omega = 0.0f;
    s_status = TRACK_APP_IDLE;
    sys_log_text(info, "track_app STOP");
}

track_app_status_t track_app_get_status(void)
{
    return s_status;
}

bool track_app_is_running(void)
{
    return s_status == TRACK_APP_RUNNING;
}

void track_app_update(void)
{
    float err;
    float corr;
    float sign;
    float omega;
    uint32_t now;
    uint32_t lost_lim;

    if (!s_inited || s_status != TRACK_APP_RUNNING) {
        return;
    }

    /* 运行中允许热改 PID / 限幅 */
    apply_pid_params();

    track_scan();
    err = track_get_error();

    /* 与 mission 弧段相同：corr = PID(0, error)，再乘 track_sign */
    corr = pid_calculate(&s_pid, 0.0f, err);
    sign = (track_app_sign >= 0.0f) ? 1.0f : -1.0f;
    omega = track_app_ff + sign * corr;
    omega = clamp_abs_range(omega, track_app_w_min, track_app_w_max);
    s_cmd_omega = omega;

    chassis_set_mode(CHASSIS_MODE_YAW_RATE);
    chassis_set_velocity(track_app_v, omega);

    now = sys_time_get_ms();
    if (track_is_lost()) {
        if (!s_was_lost) {
            s_lost_since_ms = now;
            s_was_lost = true;
        }
        lost_lim = (uint32_t)(track_app_lost_ms > 0.0f ? track_app_lost_ms : 0.0f);
        if (lost_lim > 0u && (now - s_lost_since_ms) >= lost_lim) {
            sys_log_text(warning, "track_app: lost timeout %ums", (unsigned)lost_lim);
            track_app_stop();
        }
    } else {
        s_was_lost = false;
    }
}

/* -------------------------------------------------------------------------- */
cmd_exec_result_t track_app_command_handler(i32 seq, int argc, char **argv)
{
    const char *cmd;

    (void)seq;

    if (!s_inited) {
        return CMD_EXEC_CTX(EXIT_NOT_INITIALIZED, "track_app_not_init");
    }
    if (argc < 2) {
        sys_log_text(terminal,
                     "Usage: track start|stop|status|scan|pol <0|1>|cal <max|min>");
        return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage");
    }

    cmd = argv[1];

    if (strcmp(cmd, "stop") == 0) {
        track_app_stop();
        return CMD_EXEC_CTX(EXIT_OK, "stopped");
    }

    if (strcmp(cmd, "start") == 0) {
        {
            exit_code_t ec = track_app_start();
            if (ec != EXIT_OK) {
                return CMD_EXEC_CTX(ec, "start_fail");
            }
        }
        return CMD_EXEC_CTX(EXIT_OK, "started");
    }

    if (strcmp(cmd, "status") == 0) {
        track_scan();
        sys_log_text(terminal,
                     "track: %s mask=0x%02X on=%u err=%.3f lost=%u",
                     (s_status == TRACK_APP_RUNNING) ? "run" : "idle",
                     (unsigned)track_get_mask(),
                     (unsigned)track_get_on_count(),
                     (double)track_get_error(),
                     track_is_lost() ? 1u : 0u);
        sys_log_text(terminal,
                     "  v=%.1f omega_cmd=%.1f ff=%.1f sign=%.0f",
                     (double)track_app_v, (double)s_cmd_omega,
                     (double)track_app_ff, (double)track_app_sign);
        sys_log_text(terminal,
                     "  pid kp=%.1f ki=%.1f kd=%.1f wmax=%.1f wmin=%.1f lost_ms=%.0f",
                     (double)track_app_kp, (double)track_app_ki,
                     (double)track_app_kd, (double)track_app_w_max,
                     (double)track_app_w_min, (double)track_app_lost_ms);
        sys_log_text(terminal, "  backend=%s pol=%u ch=%u",
                     track_backend_name(),
                     (unsigned)track_get_polarity(),
                     (unsigned)track_channel_count());
        return CMD_EXEC_CTX(EXIT_OK, "status");
    }

    if (strcmp(cmd, "scan") == 0) {
        log_sensor_once();
        return CMD_EXEC_CTX(EXIT_OK, "scan");
    }

    if (strcmp(cmd, "pol") == 0 || strcmp(cmd, "polarity") == 0) {
        if (argc < 3) {
            sys_log_text(terminal, "Usage: track pol <0|1>  (0=black line / dig low)");
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage");
        }
        track_set_polarity((uint8_t)atoi(argv[2]));
        sys_log_text(terminal, "track polarity=%u",
                     (unsigned)track_get_polarity());
        return CMD_EXEC_CTX(EXIT_OK, "pol");
    }

    if (strcmp(cmd, "cal") == 0) {
        exit_code_t ec;

        if (argc < 3) {
            sys_log_text(terminal, "Usage: track cal <max|min>  (GS08 only)");
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage");
        }
        if (strcmp(argv[2], "max") == 0) {
            ec = track_cal_set_max();
        } else if (strcmp(argv[2], "min") == 0) {
            ec = track_cal_set_min();
        } else {
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "cal_arg");
        }
        if (ec != EXIT_OK) {
            return CMD_EXEC_CTX(ec, "cal_fail");
        }
        sys_log_text(terminal, "track cal %s ok", argv[2]);
        return CMD_EXEC_CTX(EXIT_OK, "cal");
    }

    sys_log_text(terminal,
                 "Usage: track start|stop|status|scan|pol <0|1>|cal <max|min>");
    return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage");
}
