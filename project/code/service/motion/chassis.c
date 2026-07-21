/**
 * @file chassis.c
 * @brief 差速底盘服务实现
 */
#include "chassis.h"
#include "motor.h"
#include "service/imu/imu.h"
#include "service/com/param.h"
#include "service/sys/sys_log.h"
#include "common/pid/pid.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------- */
/* 可调参数（param 注册）                                                       */
/* -------------------------------------------------------------------------- */
static float chassis_half_track = CHASSIS_HALF_TRACK_DEFAULT;
static float chassis_max_v = CHASSIS_MAX_V_DEFAULT;
static float chassis_max_omega = CHASSIS_MAX_OMEGA_DEFAULT;
static float chassis_ol_v_scale = CHASSIS_OL_V_SCALE_DEFAULT;
static float chassis_ol_w_scale = CHASSIS_OL_W_SCALE_DEFAULT;
static float chassis_omega_to_wheel = CHASSIS_OMEGA_TO_WHEEL_DEFAULT;
static float chassis_imu_yaw_sign = CHASSIS_IMU_YAW_SIGN_DEFAULT;

static float chassis_yaw_rate_kp = 1.2f;
static float chassis_yaw_rate_ki = 0.3f;
static float chassis_heading_kp = 2.0f;
static float chassis_heading_ki = 0.0f;
/** 航向 D：对 gyro.z 阻尼（deg/s → 输出 deg/s），比 d(err)/dt 抗噪 */
static float chassis_heading_kd = 0.15f;

/* -------------------------------------------------------------------------- */
/* 运行时状态                                                                   */
/* -------------------------------------------------------------------------- */
static chassis_mode_t s_mode = CHASSIS_MODE_IDLE;
static float s_target_v = 0.0f;
static float s_target_omega = 0.0f;
static float s_target_heading = 0.0f;
static float s_wheel_left = 0.0f;
static float s_wheel_right = 0.0f;
static bool s_imu_ready = false;
static bool s_inited = false;

static pid_controller_t s_yaw_rate_pid;
static pid_controller_t s_heading_pid;

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

/** 角度差 wrap 到 (-180, 180] */
static float wrap_deg(float err)
{
    while (err > 180.0f) {
        err -= 360.0f;
    }
    while (err <= -180.0f) {
        err += 360.0f;
    }
    return err;
}

static void chassis_apply_param_to_pid(void)
{
    pid_update_params(&s_yaw_rate_pid, chassis_yaw_rate_kp, chassis_yaw_rate_ki, 0.0f);
    /* 航向环库内仅 P/I；D 在 update 里用 gyro 阻尼单独叠加 */
    pid_update_params(&s_heading_pid, chassis_heading_kp, chassis_heading_ki, 0.0f);
}

static void chassis_reset_pids(void)
{
    pid_reset(&s_yaw_rate_pid);
    pid_reset(&s_heading_pid);
}

/**
 * 差速运动学：轮速单位与 motor 目标速度一致
 * 车体约定：正 v 前进，正 ω 逆时针（CCW）。
 * 本车左右电机同号轮速对应车体后退，v/ω 正负同时反，进运动学前统一取反：
 *   v_l = -v - (-ω) * half_track
 *   v_r = -v + (-ω) * half_track
 */
static void chassis_kinematics(float v, float omega, float *left, float *right)
{
    float wl;
    float wr;

    /* 车体 → 轮速：符号翻转（见上） */
    v = -v;
    omega = -omega;

    v = clampf(v, -chassis_max_v, chassis_max_v);
    omega = clampf(omega, -chassis_max_omega, chassis_max_omega);

    wl = v - omega * chassis_half_track;
    wr = v + omega * chassis_half_track;

    *left = clampf(wl, -chassis_max_v, chassis_max_v);
    *right = clampf(wr, -chassis_max_v, chassis_max_v);
}

static void chassis_apply_openloop(float v, float omega)
{
    float duty_l;
    float duty_r;

    /* 与 kinematics 相同：车体 v/ω 符号翻转到电机 duty */
    v = -v;
    omega = -omega;

    v = clampf(v, -chassis_max_v, chassis_max_v);
    omega = clampf(omega, -chassis_max_omega, chassis_max_omega);

    duty_l = v * chassis_ol_v_scale - omega * chassis_ol_w_scale;
    duty_r = v * chassis_ol_v_scale + omega * chassis_ol_w_scale;

    duty_l = clampf(duty_l, (float)(-MOTOR_MAX_DUTY), (float)MOTOR_MAX_DUTY);
    duty_r = clampf(duty_r, (float)(-MOTOR_MAX_DUTY), (float)MOTOR_MAX_DUTY);

    s_wheel_left = duty_l;
    s_wheel_right = duty_r;

    motor_set_target_pwm(&motor_left, (int32)duty_l);
    motor_set_target_pwm(&motor_right, (int32)duty_r);
}

static void chassis_apply_wheel_speed(float left, float right)
{
    s_wheel_left = left;
    s_wheel_right = right;
    motor_set_target_speed(&motor_left, left);
    motor_set_target_speed(&motor_right, right);
}

static void chassis_set_motors_mode(motor_mode_t mode)
{
    motor_set_mode(&motor_left, mode);
    motor_set_mode(&motor_right, mode);
}

static float chassis_read_gyro_z(void)
{
    vec3f gyro = imu_get_gyro();
    return gyro.z * chassis_imu_yaw_sign;
}

static float chassis_read_yaw_deg(void)
{
    vec3f att = imu_get_attitude();
    return att.z * chassis_imu_yaw_sign;
}

static const char *chassis_mode_name(chassis_mode_t mode)
{
    switch (mode) {
        case CHASSIS_MODE_IDLE:     return "idle";
        case CHASSIS_MODE_OPENLOOP: return "openloop";
        case CHASSIS_MODE_SPEED:    return "speed";
        case CHASSIS_MODE_YAW_RATE: return "yaw_rate";
        case CHASSIS_MODE_HEADING:  return "heading";
        default:                    return "unknown";
    }
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                     */
/* -------------------------------------------------------------------------- */
exit_code_t chassis_init(void)
{
    if (s_inited) {
        return EXIT_ALREADY_INITIALIZED;
    }

    sys_log_text(info, "Chassis: init (dt=%.0fms)...", CHASSIS_DT_S * 1000.0f);

    /* 角速度环输出：修正后的 ω（deg/s 域），再乘 omega_to_wheel 进运动学 */
    pid_init(&s_yaw_rate_pid, chassis_yaw_rate_kp, chassis_yaw_rate_ki, 0.0f,
             CHASSIS_DT_S, -chassis_max_omega, chassis_max_omega);
    pid_reset(&s_yaw_rate_pid);

    /* 航向环：P(+可选I) 输出期望角速度；D 用 gyro 阻尼在 update 叠加 */
    pid_init(&s_heading_pid, chassis_heading_kp, chassis_heading_ki, 0.0f,
             CHASSIS_DT_S, -chassis_max_omega, chassis_max_omega);
    pid_reset(&s_heading_pid);

    assert_fun(param_add("chassis_half_track", PARAM_TYPE_FLOAT, &chassis_half_track, true));
    assert_fun(param_add("chassis_max_v", PARAM_TYPE_FLOAT, &chassis_max_v, true));
    assert_fun(param_add("chassis_max_omega", PARAM_TYPE_FLOAT, &chassis_max_omega, true));
    assert_fun(param_add("chassis_ol_v_scale", PARAM_TYPE_FLOAT, &chassis_ol_v_scale, true));
    assert_fun(param_add("chassis_ol_w_scale", PARAM_TYPE_FLOAT, &chassis_ol_w_scale, true));
    assert_fun(param_add("chassis_omega_to_wheel", PARAM_TYPE_FLOAT, &chassis_omega_to_wheel, true));
    /* 符号约定随固件，不落盘，避免旧 Flash 覆盖默认值 */
    assert_fun(param_add("chassis_imu_yaw_sign", PARAM_TYPE_FLOAT, &chassis_imu_yaw_sign, false));
    assert_fun(param_add("chassis_yaw_rate_kp", PARAM_TYPE_FLOAT, &chassis_yaw_rate_kp, true));
    assert_fun(param_add("chassis_yaw_rate_ki", PARAM_TYPE_FLOAT, &chassis_yaw_rate_ki, true));
    assert_fun(param_add("chassis_heading_kp", PARAM_TYPE_FLOAT, &chassis_heading_kp, true));
    assert_fun(param_add("chassis_heading_ki", PARAM_TYPE_FLOAT, &chassis_heading_ki, true));
    assert_fun(param_add("chassis_heading_kd", PARAM_TYPE_FLOAT, &chassis_heading_kd, true));

    s_mode = CHASSIS_MODE_IDLE;
    s_target_v = 0.0f;
    s_target_omega = 0.0f;
    s_target_heading = 0.0f;
    s_wheel_left = 0.0f;
    s_wheel_right = 0.0f;
    s_imu_ready = false;
    s_inited = true;

    sys_log_text(info, "Chassis: ready (mode=idle)");
    return EXIT_OK;
}

void chassis_set_mode(chassis_mode_t mode)
{
    if (!s_inited) {
        sys_log_text(error, "Chassis: set_mode before init");
        return;
    }

    if (mode == CHASSIS_MODE_YAW_RATE || mode == CHASSIS_MODE_HEADING) {
        if (!s_imu_ready) {
            sys_log_text(error, "Chassis: IMU not ready, cannot enter %s",
                         chassis_mode_name(mode));
            return;
        }
    }

    s_mode = mode;
    chassis_reset_pids();
    chassis_apply_param_to_pid();

    switch (mode) {
        case CHASSIS_MODE_IDLE:
            s_target_v = 0.0f;
            s_target_omega = 0.0f;
            s_wheel_left = 0.0f;
            s_wheel_right = 0.0f;
            motor_stop_all();
            break;

        case CHASSIS_MODE_OPENLOOP:
            chassis_set_motors_mode(MOTOR_MODE_OPENLOOP);
            break;

        case CHASSIS_MODE_SPEED:
        case CHASSIS_MODE_YAW_RATE:
            chassis_set_motors_mode(MOTOR_MODE_SPEED);
            break;

        case CHASSIS_MODE_HEADING:
            chassis_set_motors_mode(MOTOR_MODE_SPEED);
            /* 切入时钉住当前航向，避免突然甩头 */
            s_target_heading = chassis_read_yaw_deg();
            break;

        default:
            s_mode = CHASSIS_MODE_IDLE;
            motor_stop_all();
            break;
    }

    sys_log_text(info, "Chassis: mode -> %s", chassis_mode_name(s_mode));
}

chassis_mode_t chassis_get_mode(void)
{
    return s_mode;
}

void chassis_set_velocity(float v, float omega)
{
    s_target_v = clampf(v, -chassis_max_v, chassis_max_v);
    s_target_omega = clampf(omega, -chassis_max_omega, chassis_max_omega);
}

void chassis_set_heading(float yaw_deg)
{
    s_target_heading = yaw_deg;
}

void chassis_stop(void)
{
    chassis_set_mode(CHASSIS_MODE_IDLE);
}

void chassis_get_wheel_target(float *left, float *right)
{
    if (left != NULL) {
        *left = s_wheel_left;
    }
    if (right != NULL) {
        *right = s_wheel_right;
    }
}

float chassis_get_target_v(void)
{
    return s_target_v;
}

float chassis_get_target_omega(void)
{
    return s_target_omega;
}

float chassis_get_target_heading(void)
{
    return s_target_heading;
}

void chassis_set_imu_ready(bool ready)
{
    s_imu_ready = ready;
}

void chassis_update(void)
{
    float v;
    float omega_wheel;
    float left;
    float right;
    float omega_cmd_deg;
    float omega_out_deg;
    float gyro_z;
    float yaw;
    float heading_err;

    if (!s_inited) {
        return;
    }

    /* 热更新 PID 参数（param set 后下一拍生效） */
    chassis_apply_param_to_pid();

    /* 同步输出限幅（max_omega 可能被 param 改） */
    s_yaw_rate_pid.output_max = chassis_max_omega;
    s_yaw_rate_pid.output_min = -chassis_max_omega;
    s_heading_pid.output_max = chassis_max_omega;
    s_heading_pid.output_min = -chassis_max_omega;

    v = s_target_v;

    switch (s_mode) {
        case CHASSIS_MODE_IDLE:
            return;

        case CHASSIS_MODE_OPENLOOP:
            chassis_apply_openloop(s_target_v, s_target_omega);
            return;

        case CHASSIS_MODE_SPEED:
            chassis_kinematics(s_target_v, s_target_omega, &left, &right);
            chassis_apply_wheel_speed(left, right);
            return;

        case CHASSIS_MODE_YAW_RATE:
            if (!s_imu_ready) {
                return;
            }
            /*
             * YAW_RATE：target_omega 按 deg/s 理解，与 gyro.z 同域。
             * PID 输出修正后的角速度 (deg/s)，再 * omega_to_wheel 进差速运动学。
             */
            omega_cmd_deg = s_target_omega;
            gyro_z = chassis_read_gyro_z();
            omega_out_deg = pid_calculate(&s_yaw_rate_pid, omega_cmd_deg, gyro_z);
            omega_wheel = omega_out_deg * chassis_omega_to_wheel;
            chassis_kinematics(v, omega_wheel, &left, &right);
            chassis_apply_wheel_speed(left, right);
            return;

        case CHASSIS_MODE_HEADING:
            if (!s_imu_ready) {
                return;
            }
            /*
             * 航向 PD(+可选 I)：
             *   ω* = Kp·e + Ki·∫e  −  Kd·gyro.z
             * D 用角速度反馈阻尼，避免对 yaw 噪声求导放大。
             * 输出限幅后作为角速度内环设定。
             */
            yaw = chassis_read_yaw_deg();
            heading_err = wrap_deg(s_target_heading - yaw);
            gyro_z = chassis_read_gyro_z();
            // omega_cmd_deg = pid_calculate(&s_heading_pid, heading_err, 0.0f);
            if (heading_err > 3.0f) omega_cmd_deg = 75.0f;
            else if (heading_err < -3.0f) omega_cmd_deg = -75.0f;
            else omega_cmd_deg = 0.0f;
            omega_cmd_deg = clampf(omega_cmd_deg, -chassis_max_omega, chassis_max_omega);
            omega_out_deg = pid_calculate(&s_yaw_rate_pid, omega_cmd_deg, gyro_z);
            omega_wheel = omega_out_deg * chassis_omega_to_wheel;
            chassis_kinematics(v, omega_wheel, &left, &right);
            chassis_apply_wheel_speed(left, right);
            return;

        default:
            chassis_set_mode(CHASSIS_MODE_IDLE);
            return;
    }
}

/* -------------------------------------------------------------------------- */
/* 命令                                                                         */
/* -------------------------------------------------------------------------- */
static void chassis_print_status(void)
{
    float yaw = 0.0f;
    float gyro_z = 0.0f;

    if (s_imu_ready) {
        yaw = chassis_read_yaw_deg();
        gyro_z = chassis_read_gyro_z();
    }

    sys_log_text(terminal,
                 "chassis: mode=%s v=%.2f w=%.2f hdg_tgt=%.1f yaw=%.1f gz=%.1f "
                 "wl=%.2f wr=%.2f imu=%d",
                 chassis_mode_name(s_mode), s_target_v, s_target_omega,
                 s_target_heading, yaw, gyro_z, s_wheel_left, s_wheel_right,
                 s_imu_ready ? 1 : 0);
}

cmd_exec_result_t chassis_command_handler(i32 seq, int argc, char **argv)
{
    const char *cmd;
    const char *a1;
    const char *a2;

    (void)seq;

    if (argc < 2) {
        sys_log_text(terminal,
                     "Usage: chassis <mode|set|heading|stop|status|param> ...");
        sys_log_text(terminal,
                     "  mode idle|openloop|speed|yaw_rate|heading");
        sys_log_text(terminal, "  set <v> <omega>");
        sys_log_text(terminal, "  heading <deg>");
        return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage_chassis");
    }

    if (!s_inited) {
        return CMD_EXEC_CTX(EXIT_NOT_INITIALIZED, "chassis_not_init");
    }

    cmd = argv[1];
    a1 = (argc >= 3) ? argv[2] : NULL;
    a2 = (argc >= 4) ? argv[3] : NULL;

    if (strcmp(cmd, "stop") == 0) {
        chassis_stop();
        sys_log_text(terminal, "chassis stopped (idle)");
        return CMD_EXEC_CTX(EXIT_OK, "stopped");
    }

    if (strcmp(cmd, "status") == 0) {
        chassis_print_status();
        return CMD_EXEC_CTX(EXIT_OK, "status_printed");
    }

    if (strcmp(cmd, "param") == 0) {
        chassis_apply_param_to_pid();
        sys_log_text(terminal,
                     "chassis PID applied yr_kp=%.2f yr_ki=%.2f "
                     "hd_kp=%.2f hd_ki=%.2f hd_kd=%.2f",
                     chassis_yaw_rate_kp, chassis_yaw_rate_ki,
                     chassis_heading_kp, chassis_heading_ki, chassis_heading_kd);
        return CMD_EXEC_CTX(EXIT_OK, "param_applied");
    }

    if (strcmp(cmd, "mode") == 0) {
        chassis_mode_t mode;

        if (a1 == NULL) {
            sys_log_text(terminal, "need mode name");
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "need_mode");
        }
        if (strcmp(a1, "idle") == 0) {
            mode = CHASSIS_MODE_IDLE;
        } else if (strcmp(a1, "openloop") == 0) {
            mode = CHASSIS_MODE_OPENLOOP;
        } else if (strcmp(a1, "speed") == 0) {
            mode = CHASSIS_MODE_SPEED;
        } else if (strcmp(a1, "yaw_rate") == 0) {
            mode = CHASSIS_MODE_YAW_RATE;
        } else if (strcmp(a1, "heading") == 0) {
            mode = CHASSIS_MODE_HEADING;
        } else {
            sys_log_text(terminal, "invalid mode: %s", a1);
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "invalid_mode");
        }

        if ((mode == CHASSIS_MODE_YAW_RATE || mode == CHASSIS_MODE_HEADING) &&
            !s_imu_ready) {
            sys_log_text(terminal, "IMU not ready");
            return CMD_EXEC_CTX(EXIT_NOT_INITIALIZED, "imu_not_ready");
        }

        chassis_set_mode(mode);
        sys_log_text(terminal, "chassis mode -> %s", a1);
        return CMD_EXEC_CTX(EXIT_OK, "mode_set");
    }

    if (strcmp(cmd, "set") == 0) {
        float v;
        float w;

        if (a1 == NULL || a2 == NULL) {
            sys_log_text(terminal, "Usage: chassis set <v> <omega>");
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "need_v_omega");
        }
        v = (float)atof(a1);
        w = (float)atof(a2);
        chassis_set_velocity(v, w);
        sys_log_text(terminal, "chassis set v=%.2f w=%.2f", s_target_v, s_target_omega);
        return CMD_EXEC_CTX(EXIT_OK, "set_ok");
    }

    if (strcmp(cmd, "heading") == 0) {
        float hdg;

        if (a1 == NULL) {
            sys_log_text(terminal, "Usage: chassis heading <deg>");
            return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "need_heading");
        }
        hdg = (float)atof(a1);
        chassis_set_heading(hdg);
        sys_log_text(terminal, "chassis heading tgt=%.1f", s_target_heading);
        return CMD_EXEC_CTX(EXIT_OK, "heading_set");
    }

    sys_log_text(terminal, "Unknown chassis cmd: %s", cmd);
    return CMD_EXEC_CTX(EXIT_NOT_SUPPORTED, "unknown_cmd");
}
