/**
 * @file motor.h
 * @brief 电机控制模块（双电机，DRV8701：DIR + PWM）
 *
 * 引脚参考：E3_04_drv8701e_double_motor_contro_demo（MSPM0G3519 / 逐飞主板）
 * 命令 mask：bit0=左, bit1=右（0x1/0x2/0x3）
 */
#ifndef __MOTOR_H
#define __MOTOR_H

#include "common/tools/common_def.h"
#include "common/pid/pid.h"
#include "encoder.h"
#include "zf_driver_pwm.h"
#include "zf_driver_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOTOR_MAX_TARGET_SPEED              250.0f
#define MOTOR_MAX_DUTY                      3500
#define MOTOR_DEADZONE_CALIBRATE_TIME_MS    500
#define MOTOR_DEADZONE_CALIBRATE_STEP       1
#define MOTOR_DEADSPEED_THRESHOLD_DEFAULT   2.0f

/** PWM 频率，与逐飞 DRV8701 例程一致 */
#define MOTOR_PWM_FREQ_HZ                   17000U

/* --------------------------------------------------------------------------
 * DRV8701 引脚（3507 主板「有刷电机」座，灰排线）
 *   座上可用：B8 B9 B12 B13 B10 B11 A26 A27
 *   本工程：左 B8 PWM + B10 DIR；右 B12 PWM + B13 DIR
 *   避开：B9/A26/A27（正交编码器）、B11 预留
 * -------------------------------------------------------------------------- */
#define MOTOR_LEFT_PWM                      (PWM_TIM_A0_CH0_B8)
#define MOTOR_LEFT_DIR                      (B10)

#define MOTOR_RIGHT_PWM                     (PWM_TIM_A0_CH2_B12)
#define MOTOR_RIGHT_DIR                     (B13)

typedef enum {
    MOTOR_MODE_SPEED = 0,
    MOTOR_MODE_POSITION,
    MOTOR_MODE_OPENLOOP
} motor_mode_t;

typedef struct {
    pwm_channel_enum pwm_channel;       ///< PWM 通道
    gpio_pin_enum dir_pin;              ///< 方向 GPIO
    motor_encoder_t *encoder;           ///< 对应编码器

    pid_controller_t speed_pid;
    pid_controller_t position_pid;

    float target_speed;
    float target_position;
    int32 target_pwm;

    motor_mode_t mode;
    int32 deadzone;
    float min_speed_threshold;
    int8 dir_reverse;
} motor_t;

extern motor_t motor_left;
extern motor_t motor_right;

exit_code_t motor_init(void);
exit_code_t motor_set_duty(motor_t *motor, int32 duty);
exit_code_t motor_stop_all(void);
exit_code_t motor_stop(motor_t *motor);

void motor_set_target_speed(motor_t *motor, float speed);
void motor_set_target_pwm(motor_t *motor, int32 pwm);
void motor_update(void);
void motor_update_single(motor_t *motor);

exit_code_t motor_calibrate_deadzone(void);
int32 motor_get_deadzone(motor_t *motor);

void motor_set_mode(motor_t *motor, motor_mode_t mode);
motor_mode_t motor_get_mode(motor_t *motor);
void motor_set_target_position(motor_t *motor, float position);
int32 motor_get_position(motor_t *motor);
void motor_clear_position(motor_t *motor);

cmd_exec_result_t motor_command_handler(i32 seq, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_H */
