/**
 * @file motor.c
 * @brief 双电机控制模块（DRV8701：DIR + PWM）
 *
 * 电机：MG310 霍尔；引脚：主板有刷电机座 — 左 B8/B10，右 B12/B13（TIM_A0）
 * 编码器：E2_01_encoder_quadrature_demo（正交）
 * 命令 mask: bit0=左电机, bit1=右电机
 */
#include "motor.h"
#include "common/tools/common_def.h"
#include "service/com/param.h"
#include "service/sys/sys_log.h"
#include "zf_common_typedef.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

/*
 * 速度单位改为 counts/2ms（满量程约 20）后，原 kp/ki 相对「大速度单位」会偏弱。
 * 初值：error≈5 → P 约 500~600 duty；I 负责抬到额定区。串口 set motor_kp/ki 再精调。
 */
static float motor_speed_pid_kp = 120.0f;
static float motor_speed_pid_ki = 200.0f;

/* 左电机 + 左编码器 */
motor_t motor_left = {
    .pwm_channel = MOTOR_LEFT_PWM,
    .dir_pin = MOTOR_LEFT_DIR,
    .encoder = &encoder_left,
    .target_speed = 0.0f,
    .target_position = 0.0f,
    .target_pwm = 0,
    .mode = MOTOR_MODE_SPEED,
    .deadzone = 0,
    .min_speed_threshold = MOTOR_DEADSPEED_THRESHOLD_DEFAULT,
    .dir_reverse = 0
};

/* 右电机 + 右编码器 */
motor_t motor_right = {
    .pwm_channel = MOTOR_RIGHT_PWM,
    .dir_pin = MOTOR_RIGHT_DIR,
    .encoder = &encoder_right,
    .target_speed = 0.0f,
    .target_position = 0.0f,
    .target_pwm = 0,
    .mode = MOTOR_MODE_SPEED,
    .deadzone = 0,
    .min_speed_threshold = MOTOR_DEADSPEED_THRESHOLD_DEFAULT,
    .dir_reverse = 1
};

static motor_t *const motor_list[] = {
    &motor_left,
    &motor_right,
};

#define MOTOR_COUNT  (sizeof(motor_list) / sizeof(motor_list[0]))

static void motor_config_output(motor_t *motor)
{
    if (motor == NULL) {
        return;
    }

    pwm_init(motor->pwm_channel, MOTOR_PWM_FREQ_HZ, 0);
    gpio_init(motor->dir_pin, GPO, GPIO_HIGH, GPO_PUSH_PULL);
}

static void motor_init_single(motor_t *motor)
{
    motor_config_output(motor);

    /* 速度环 / 位置环：默认按 500Hz 控制周期 dt=2ms 调参 */
    pid_init(&motor->speed_pid, motor_speed_pid_kp, motor_speed_pid_ki, 0.0f,
             0.002f, -MOTOR_MAX_DUTY, MOTOR_MAX_DUTY);
    pid_reset(&motor->speed_pid);

    pid_init(&motor->position_pid, 0.05f, 0.05f, 0.0f, 0.002f,
             -MOTOR_MAX_TARGET_SPEED, MOTOR_MAX_TARGET_SPEED);
    pid_reset(&motor->position_pid);
}

exit_code_t motor_init(void)
{
    uint8 i;

    sys_log_text(info, "Motor: MG310 hall init (pwm=%uHz, duty_max=%d, spd_max=%.1f c/2ms)...",
                 (unsigned)MOTOR_PWM_FREQ_HZ, (int)MOTOR_MAX_DUTY,
                 (double)MOTOR_MAX_TARGET_SPEED);
    sys_log_text(info,
                 "Motor: gear=%.3f enc=%dx%d counts/rev≈%.0f supply≈%.1fV rated=%.1fV",
                 (double)MOTOR_GEAR_RATIO, (int)MOTOR_ENC_LINES, (int)MOTOR_ENC_QUAD,
                 (double)MOTOR_COUNTS_PER_OUT_REV,
                 (double)MOTOR_SUPPLY_VOLT_V, (double)MOTOR_RATED_VOLT_V);

    sys_log_text(info, "Motor: init encoders...");
    encoder_init();

    sys_log_text(info, "Motor: register PID params...");
    assert_fun(param_add("motor_kp", PARAM_TYPE_FLOAT, &motor_speed_pid_kp, true));
    assert_fun(param_add("motor_ki", PARAM_TYPE_FLOAT, &motor_speed_pid_ki, true));

    for (i = 0; i < MOTOR_COUNT; i++) {
        motor_init_single(motor_list[i]);
    }

    sys_log_text(info, "Motor: L=PWM B8 + DIR B10, R=PWM B12 + DIR B13 (motherboard motor hdr)");
    return EXIT_OK;
}

exit_code_t motor_set_duty(motor_t *motor, int32 duty)
{
    int8 forward_flag;

    if (motor == NULL) {
        sys_log_text(error, "Motor: set_duty NULL");
        return EXIT_INVALID_PARAM;
    }

    if (duty > 0) {
        duty += motor->deadzone;
        if (duty > MOTOR_MAX_DUTY) {
            duty = MOTOR_MAX_DUTY;
        }
    } else if (duty < 0) {
        duty -= motor->deadzone;
        if (duty < -MOTOR_MAX_DUTY) {
            duty = -MOTOR_MAX_DUTY;
        }
    }

    forward_flag = (duty > 0) ? 1 : 0;
    if (motor->dir_reverse) {
        forward_flag = !forward_flag;
    }

    if (duty < 0) {
        duty = -duty;
    }

    /* DIR GPIO + 单 PWM（DRV8701） */
    gpio_set_level(motor->dir_pin, forward_flag ? GPIO_HIGH : GPIO_LOW);
    pwm_set_duty(motor->pwm_channel, (uint32)duty);

    return EXIT_OK;
}

exit_code_t motor_stop_all(void)
{
    uint8 i;
    for (i = 0; i < MOTOR_COUNT; i++) {
        motor_stop(motor_list[i]);
    }
    return EXIT_OK;
}

exit_code_t motor_stop(motor_t *motor)
{
    if (motor == NULL) {
        sys_log_text(error, "Motor: stop NULL");
        return EXIT_INVALID_PARAM;
    }

    /* 清目标，避免 motor_update 在下一拍把 duty 推回去 */
    motor->target_speed = 0.0f;
    motor->target_pwm = 0;
    /* 位置模式：把目标钉在当前位置，相当于“就地停住”而不是回零 */
    if (motor->encoder != NULL) {
        motor->target_position = (float)encoder_get_position(motor->encoder);
    } else {
        motor->target_position = 0.0f;
    }

    pid_reset(&motor->speed_pid);
    pid_reset(&motor->position_pid);

    return motor_set_duty(motor, 0);
}

void motor_set_target_speed(motor_t *motor, float speed)
{
    if (motor == NULL) {
        sys_log_text(error, "Motor: set_target_speed NULL");
        return;
    }

    if (speed > MOTOR_MAX_TARGET_SPEED) {
        speed = MOTOR_MAX_TARGET_SPEED;
    } else if (speed < -MOTOR_MAX_TARGET_SPEED) {
        speed = -MOTOR_MAX_TARGET_SPEED;
    }

    motor->target_speed = speed;
}

void motor_set_target_pwm(motor_t *motor, int32 pwm)
{
    if (motor == NULL) {
        sys_log_text(error, "Motor: set_target_pwm NULL");
        return;
    }

    if (pwm > MOTOR_MAX_DUTY) {
        pwm = MOTOR_MAX_DUTY;
    } else if (pwm < -MOTOR_MAX_DUTY) {
        pwm = -MOTOR_MAX_DUTY;
    }

    motor->target_pwm = pwm;
}

void motor_update_single(motor_t *motor)
{
    float target_speed;
    float current_speed;
    float speed_error;
    float abs_error;
    float duty;

    if (motor == NULL) {
        sys_log_text(error, "Motor: update NULL");
        return;
    }

    if (motor->mode == MOTOR_MODE_POSITION) {
        int32 current_position = encoder_get_position(motor->encoder);
        target_speed = pid_calculate(&motor->position_pid, motor->target_position,
                                     (float)current_position);
    } else if (motor->mode == MOTOR_MODE_OPENLOOP) {
        motor_set_duty(motor, motor->target_pwm);
        return;
    } else {
        target_speed = motor->target_speed;
    }

    current_speed = encoder_get_filtered_speed(motor->encoder);
    speed_error = target_speed - current_speed;
    abs_error = (speed_error > 0.0f) ? speed_error : -speed_error;

    /*
     * 目标接近 0：清速度环积分，避免停车后残留输出把电机推走。
     * 必须用本周期级联目标 target_speed，不能用 motor->target_speed
     * （位置模式下后者常为 0，会导致积分每拍被误清）。
     */
    if (fabsf(target_speed) < MOTOR_DEADSPEED_THRESHOLD_DEFAULT) {
        motor->speed_pid.integral = 0.0f;
        motor->speed_pid.error_prev = 0.0f;
    }

    /* 速度误差死区：近目标时不再继续推积分 */
    if (abs_error < motor->min_speed_threshold) {
        if (fabsf(target_speed) < MOTOR_DEADSPEED_THRESHOLD_DEFAULT) {
            /* 停车准停：彻底清零输出 */
            pid_reset(&motor->speed_pid);
            motor_set_duty(motor, 0);
        } else {
            /* 稳速：保持上一拍输出，避免积分蠕动与反复调节 */
            motor_set_duty(motor, (int32)motor->speed_pid.output);
        }
        return;
    }

    duty = pid_calculate(&motor->speed_pid, target_speed, current_speed);
    motor_set_duty(motor, (int32)duty);
}

static void motor_update_param(void)
{
    uint8 i;
    for (i = 0; i < MOTOR_COUNT; i++) {
        motor_list[i]->speed_pid.kp = motor_speed_pid_kp;
        motor_list[i]->speed_pid.ki = motor_speed_pid_ki;
    }
}

void motor_update(void)
{
    uint8 i;
    for (i = 0; i < MOTOR_COUNT; i++) {
        motor_update_single(motor_list[i]);
    }
}

exit_code_t motor_calibrate_deadzone(void)
{
    /* TODO: 死区自动校准 */
    return EXIT_OK;
}

int32 motor_get_deadzone(motor_t *motor)
{
    if (motor != NULL) {
        return motor->deadzone;
    }
    return 0;
}

void motor_set_mode(motor_t *motor, motor_mode_t mode)
{
    if (motor == NULL) {
        return;
    }

    motor->mode = mode;
    if (mode == MOTOR_MODE_POSITION) {
        pid_reset(&motor->position_pid);
    } else if (mode == MOTOR_MODE_OPENLOOP) {
        motor->target_pwm = 0;
        pid_reset(&motor->speed_pid);
        pid_reset(&motor->position_pid);
        (void)motor_set_duty(motor, 0);
    } else {
        pid_reset(&motor->speed_pid);
    }
}

motor_mode_t motor_get_mode(motor_t *motor)
{
    if (motor != NULL) {
        return motor->mode;
    }
    return MOTOR_MODE_SPEED;
}

void motor_set_target_position(motor_t *motor, float position)
{
    if (motor != NULL) {
        motor->target_position = position;
    }
}

int32 motor_get_position(motor_t *motor)
{
    if (motor != NULL) {
        return encoder_get_position(motor->encoder);
    }
    return 0;
}

void motor_clear_position(motor_t *motor)
{
    if (motor == NULL) {
        return;
    }
    encoder_clear_position(motor->encoder);
    motor->target_position = 0.0f;
}

static void motor_print_status(motor_t *motor, const char *name)
{
    int32 position;
    float speed;
    const char *mode_str;

    if (motor == NULL || name == NULL) {
        return;
    }

    position = motor_get_position(motor);
    speed = encoder_get_filtered_speed(motor->encoder);

    if (motor->mode == MOTOR_MODE_SPEED) {
        mode_str = "Speed";
    } else if (motor->mode == MOTOR_MODE_POSITION) {
        mode_str = "Position";
    } else {
        mode_str = "OpenLoop";
    }

    sys_log_text(terminal,
                 "%s: pos=%d spd=%.2f tgt_spd=%.2f tgt_pwm=%d mode=%s dz=%d",
                 name, position, speed, motor->target_speed, motor->target_pwm,
                 mode_str, motor->deadzone);
}

cmd_exec_result_t motor_command_handler(i32 seq, int argc, char **argv)
{
    const char *index_arg;
    const char *cmd_arg;
    const char *param_arg;
    u8 motor_mask;
    bool processed;
    exit_code_t result;
    u8 i;

    (void)seq;

    if (argc <= 2) {
        sys_log_text(terminal, "Usage: motor <mask> <stop|set|mode|status|param> [val]");
        sys_log_text(terminal, "  mask: bit0=Left bit1=Right (0x1/0x2/0x3)");
        return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "usage_motor");
    }

    index_arg = argv[1];
    cmd_arg = argv[2];
    param_arg = (argc >= 4) ? argv[3] : NULL;

    motor_mask = (u8)strtol(index_arg, NULL, 0);
    if (motor_mask == 0U) {
        sys_log_text(terminal, "Invalid motor mask: %s", index_arg);
        return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "invalid_mask");
    }

    processed = false;
    result = EXIT_OK;

    for (i = 0; i < MOTOR_COUNT; i++) {
        motor_t *motor;
        const char *motor_name;

        if (!((motor_mask >> i) & 0x01)) {
            continue;
        }

        if (i == 0) {
            motor = &motor_left;
            motor_name = "Left";
        } else {
            motor = &motor_right;
            motor_name = "Right";
        }

        processed = true;

        if (strcmp(cmd_arg, "stop") == 0) {
            exit_code_t stop_ret = motor_stop(motor);
            if (stop_ret != EXIT_OK) {
                result = stop_ret;
                continue;
            }
            sys_log_text(terminal, "%s stopped", motor_name);
        } else if (strcmp(cmd_arg, "set") == 0 && param_arg != NULL) {
            f32 value = (f32)atof(param_arg);
            switch (motor->mode) {
                case MOTOR_MODE_SPEED:
                    motor_set_target_speed(motor, value);
                    break;
                case MOTOR_MODE_POSITION:
                    motor_set_target_position(motor, value);
                    break;
                case MOTOR_MODE_OPENLOOP:
                    motor_set_target_pwm(motor, (int32)value);
                    break;
                default:
                    sys_log_text(terminal, "%s unknown mode", motor_name);
                    result = EXIT_NOT_SUPPORTED;
                    break;
            }
            sys_log_text(terminal, "%s set %.2f", motor_name, value);
        } else if (strcmp(cmd_arg, "status") == 0) {
            motor_print_status(motor, motor_name);
        } else if (strcmp(cmd_arg, "mode") == 0 && param_arg != NULL) {
            motor_mode_t mode;
            if (strcmp(param_arg, "speed") == 0) {
                mode = MOTOR_MODE_SPEED;
            } else if (strcmp(param_arg, "position") == 0) {
                mode = MOTOR_MODE_POSITION;
            } else if (strcmp(param_arg, "openloop") == 0) {
                mode = MOTOR_MODE_OPENLOOP;
            } else {
                sys_log_text(terminal, "Invalid mode: %s", param_arg);
                result = EXIT_INVALID_PARAM;
                continue;
            }
            motor_set_mode(motor, mode);
            sys_log_text(terminal, "%s mode -> %s", motor_name, param_arg);
        } else if (strcmp(cmd_arg, "param") == 0) {
            motor_update_param();
            sys_log_text(terminal, "PID params applied kp=%.2f ki=%.2f",
                         motor_speed_pid_kp, motor_speed_pid_ki);
        } else {
            sys_log_text(terminal, "Unknown command: %s", cmd_arg);
            result = EXIT_NOT_SUPPORTED;
        }
    }

    if (!processed) {
        return CMD_EXEC_CTX(EXIT_INVALID_PARAM, "empty_mask");
    }
    if (result == EXIT_OK && strcmp(cmd_arg, "status") == 0) {
        return CMD_EXEC_CTX(EXIT_OK, "status_printed");
    }
    return CMD_EXEC_CODE(result);
}
