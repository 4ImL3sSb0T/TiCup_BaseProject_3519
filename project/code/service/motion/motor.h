/**
 * @file motor.h
 * @brief 电机控制模块（双电机，DRV8701：DIR + PWM）
 *
 * 电机：MG310 霍尔编码器版（减速比 1:20.409，额定 7.4 V，可用 7~13 V）
 * 供电：约 12 V；PWM 限幅按等效额定电压折算（7.4/12）
 * 速度单位：编码器脉冲 / 2ms 控制周期（与 encoder_update + motor_update 一致）
 * 引脚参考：E3_04_drv8701e_double_motor_contro_demo（MSPM0G3519 / 逐飞主板）
 * 命令 mask：bit0=左, bit1=右（0x1/0x2/0x3）
 *
 * 注意：不可长时间堵转（堵转电流 ≤2 A）。
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

/* --------------------------------------------------------------------------
 * MG310 本体 / 编码器（霍尔 13 线 AB 相，TIM 正交 ×4，计数在电机轴）
 *   counts/输出轴一圈 ≈ 13 × 4 × 20.409 ≈ 1061
 *   空载 ~500 RPM → 约 17.7 counts/2ms；额定 ~400 RPM → 约 14.2
 * 若实车手转一圈 position 与 1061 差很多，以实测改 LINES / GEAR 后重算限幅。
 * -------------------------------------------------------------------------- */
#define MOTOR_GEAR_RATIO                    (20.409f)
#define MOTOR_ENC_LINES                     (13)
#define MOTOR_ENC_QUAD                      (4)
#define MOTOR_COUNTS_PER_OUT_REV \
    (MOTOR_ENC_LINES * MOTOR_ENC_QUAD * MOTOR_GEAR_RATIO)

/** 12 V 供电下等效额定 7.4 V：duty ≈ 7.4/12 * PWM_DUTY_MAX(10000) ≈ 6167 */
#define MOTOR_SUPPLY_VOLT_V                 (12.0f)
#define MOTOR_RATED_VOLT_V                  (7.4f)

/**
 * 速度目标上限（counts/2ms），略高于空载估算 17.7
 * 开环/速度环 set 建议先 5~12，再往上摸
 */
#define MOTOR_MAX_TARGET_SPEED              (20.0f)

/**
 * PWM 占空比上限（相对 PWM_DUTY_MAX=10000）
 * 约 72% × 12 V ≈ 8.6 V（略高于 7.4 V 额定，便于短时加速）
 * 勿长期满占空堵转（堵转电流大）
 */
#define MOTOR_MAX_DUTY                      (9500)

#define MOTOR_DEADZONE_CALIBRATE_TIME_MS    500
#define MOTOR_DEADZONE_CALIBRATE_STEP       1
/** 速度环近零/误差死区（counts/2ms）；过大会导致稳态欠调、提前冻住 PID */
#define MOTOR_DEADSPEED_THRESHOLD_DEFAULT   (0.4f)

/** PWM 频率，与逐飞 DRV8701 例程一致 */
#define MOTOR_PWM_FREQ_HZ                   17000U

/* --------------------------------------------------------------------------
 * DRV8701 引脚（3507 主板「有刷电机」座，灰排线）
 *   座上 4 组，每组 1×PWM + 1×DIR（见 docs/motherboard_3507_pinout.md）：
 *     M1=B8/B9  M2=B12/B13  M3=A27/A26  M4=B11/B10
 *   本工程：
 *     左 — M4 同组：B10 PWM + B11 DIR
 *           （B10 无 TIM_A0 复用，用 TIM_G0 CH0；B8/B9 中 B9 给右编码器）
 *     右 — M2 同组：B12 PWM + B13 DIR（TIM_A0 CH2）
 *   避开：B8 空闲；B9 / A26 / A27（正交编码器）
 * -------------------------------------------------------------------------- */
#define MOTOR_LEFT_PWM                      (PWM_TIM_G0_CH0_B10)
#define MOTOR_LEFT_DIR                      (B11)

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

/** 将 motor_kp/ki 写入速度环，并同步积分限（set/load 后调用） */
void motor_apply_param(void);

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
