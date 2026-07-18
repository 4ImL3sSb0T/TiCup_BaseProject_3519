/**
 * @file encoder.h
 * @brief 编码器模块头文件（双编码器，MSPM0G3519 / 逐飞主板）
 *
 * 引脚参考官方例程：
 *   Motherboard_Demo/E2_encoder/E2_02_encoder_dir_demo
 */
#ifndef __MOTOR_ENCODER_H__
#define __MOTOR_ENCODER_H__

#include "common/filter/moving_average_filter.h"
#include "zf_driver_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * 编码器硬件配置 —— 与逐飞 E2_02_encoder_dir_demo 一致
 *   ENCODER1: TIM_G7, LSB=A26, DIR=B27  → 左轮
 *   ENCODER2: TIM_G6, LSB=B10, DIR=B11  → 右轮
 * -------------------------------------------------------------------------- */
#define ENCODER_LEFT_TIMER              (TIM_G7)
#define ENCODER_LEFT_LSB                (TIMG7_ENCODER1_CH1_A26)
#define ENCODER_LEFT_DIR                (B27)

#define ENCODER_RIGHT_TIMER             (TIM_G6)
#define ENCODER_RIGHT_LSB               (TIMG6_ENCODER1_CH1_B10)
#define ENCODER_RIGHT_DIR               (B11)

#define ENCODER_FILTER_WINDOW_SIZE      5

/**
 * @brief 电机编码器数据结构体
 */
typedef struct {
    timer_index_enum encoder_index;             ///< 编码器定时器索引
    encoder_channel1_enum channel1;             ///< 脉冲/A 相引脚
    encoder_channel2_enum channel2;             ///< 方向/B 相引脚
    moving_average_filter_trim_t filter;        ///< 去最值滑动平均滤波器
    filter_data_t filter_buffer[ENCODER_FILTER_WINDOW_SIZE];
    int16 raw_speed;                            ///< 原始速度（脉冲/采样周期）
    float filtered_speed;                       ///< 滤波后速度
    int64 position;                             ///< 累计位置（脉冲）
    uint8 polarity;                             ///< 1=取反方向
} motor_encoder_t;

extern motor_encoder_t encoder_left;
extern motor_encoder_t encoder_right;

void encoder_init(void);
void encoder_update(void);
void encoder_update_single(motor_encoder_t *encoder);
int32 encoder_get_position(motor_encoder_t *encoder);
void encoder_clear_position(motor_encoder_t *encoder);
int16 encoder_get_speed(motor_encoder_t *encoder);
float encoder_get_filtered_speed(motor_encoder_t *encoder);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_ENCODER_H__ */
