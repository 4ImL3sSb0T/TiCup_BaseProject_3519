/**
 * @file chassis.h
 * @brief 差速底盘服务：v + ω 统一接口，可选 IMU 角速度/航向闭环
 *
 * 模式：
 *   IDLE      停车
 *   OPENLOOP  v,ω → 左右 PWM（经 motor 开环）
 *   SPEED     v,ω → 左右轮速（编码器速度环）
 *   YAW_RATE  SPEED + gyro.z 角速度内环
 *   HEADING   航向外环 → 角速度内环 → SPEED
 *
 * 周期建议：chassis_update 10ms；motor 2ms；imu 1ms
 */
#ifndef __CHASSIS_H__
#define __CHASSIS_H__

#include "common/tools/common_def.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 底盘控制周期 (s)，与 main 中 10ms soft_timer 一致 */
#define CHASSIS_DT_S                    (0.01f)

/**
 * 轮速与 motor 同单位：counts/2ms（MG310 霍尔空载约 18）
 * half_track / omega 仍为抽象量，靠 param 实车标定
 */
#define CHASSIS_HALF_TRACK_DEFAULT      (2.0f)
#define CHASSIS_MAX_V_DEFAULT           (18.0f)
#define CHASSIS_MAX_OMEGA_DEFAULT       (12.0f)

/** 开环：v/ω → duty；约 MOTOR_MAX_DUTY/CHASSIS_MAX_V ≈ 6200/18 */
#define CHASSIS_OL_V_SCALE_DEFAULT      (340.0f)
#define CHASSIS_OL_W_SCALE_DEFAULT      (250.0f)

/**
 * IMU 角速度(deg/s) → 运动学 ω（轮速单位）的桥接系数
 * wheel_omega = imu_omega_deg_s * chassis_omega_to_wheel
 */
#define CHASSIS_OMEGA_TO_WHEEL_DEFAULT  (0.15f)

/** IMU 航向/角速度符号：实车反向时改为 -1 */
#define CHASSIS_IMU_YAW_SIGN_DEFAULT    (1.0f)

typedef enum {
    CHASSIS_MODE_IDLE = 0,
    CHASSIS_MODE_OPENLOOP,
    CHASSIS_MODE_SPEED,
    CHASSIS_MODE_YAW_RATE,
    CHASSIS_MODE_HEADING
} chassis_mode_t;

exit_code_t chassis_init(void);
void chassis_update(void);

/** IMU 初始化成功后由 main 调用，允许进入 YAW_RATE / HEADING */
void chassis_set_imu_ready(bool ready);

void chassis_set_mode(chassis_mode_t mode);
chassis_mode_t chassis_get_mode(void);

void chassis_set_velocity(float v, float omega);
void chassis_set_heading(float yaw_deg);
void chassis_stop(void);

void chassis_get_wheel_target(float *left, float *right);
float chassis_get_target_v(void);
float chassis_get_target_omega(void);
float chassis_get_target_heading(void);

cmd_exec_result_t chassis_command_handler(i32 seq, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* __CHASSIS_H__ */
