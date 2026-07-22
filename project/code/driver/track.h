/**
 * @file track.h
 * @brief 光电循迹驱动层：采样 / 极性归一 / 位掩码 / 加权偏差
 *
 * 仅负责传感器，不含 PID 与底盘控制（见 app/track_app.* / app/mission 弧段）。
 *
 * 后端（编译期 TRACK_SENSOR_BACKEND）：
 *   TRACK_BACKEND_DIGITAL5 — 五路独立数字 GPIO
 *   TRACK_BACKEND_GS08RA8  — 逐飞 GS08RA 八路模拟灰度
 *
 * 统一语义：归一化后 on_line=1 表示压在赛道上。
 * 默认极性：数字管脚低电平为赛道；GS08 黑线(bin=0)为赛道（可通过 polarity 翻转）。
 */
#ifndef __TRACK_H__
#define __TRACK_H__

#include "common/tools/common_def.h"
#include "zf_driver_gpio.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 编译期配置                                                                   */
/* -------------------------------------------------------------------------- */
#define TRACK_BACKEND_DIGITAL5  0
#define TRACK_BACKEND_GS08RA8   1

#ifndef TRACK_SENSOR_BACKEND
#define TRACK_SENSOR_BACKEND    TRACK_BACKEND_DIGITAL5
#endif

/** 1=允许 main 调用 track_init；0=仅编进工程、不定脚时不初始化 */
#ifndef TRACK_ENABLE
#define TRACK_ENABLE            0
#endif

/** 默认：该电平视为赛道（数字 GPIO 直接比较；GS08 见 track.c 映射） */
#define TRACK_ACTIVE_LEVEL_DEF  0

#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_DIGITAL5)
#define TRACK_CH_NUM            5
#elif (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
#define TRACK_CH_NUM            8
#else
#error "TRACK_SENSOR_BACKEND must be TRACK_BACKEND_DIGITAL5 or TRACK_BACKEND_GS08RA8"
#endif

/* -------------------------------------------------------------------------- */
/* 五路数字 GPIO：左 → 右 CH0..CH4（error 负=偏左）                              */
/* 已启用占用见 docs/hardware.md；勿占用 B21–B23（IMU SPI1）                    */
/* -------------------------------------------------------------------------- */
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_DIGITAL5)
#ifndef TRACK_PIN_0
#define TRACK_PIN_0             (A0)    /* 最左 */
#endif
#ifndef TRACK_PIN_1
#define TRACK_PIN_1             (A1)
#endif
#ifndef TRACK_PIN_2
#define TRACK_PIN_2             (B14)   /* 中 */
#endif
#ifndef TRACK_PIN_3
#define TRACK_PIN_3             (B8)
#endif
#ifndef TRACK_PIN_4
#define TRACK_PIN_4             (B18)   /* 最右 */
#endif
#endif

/* -------------------------------------------------------------------------- */
/* API                                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief 初始化传感器硬件（GPIO 或 GS08RA），不启动循迹控制
 */
exit_code_t track_init(void);

/**
 * @brief 扫描全部通道，更新 mask / error / on_count
 * @note  控制层 track_follow_update 会调用；也可单独轮询调试
 */
void track_scan(void);

/** bit i = 1 表示该通道 on_line */
uint8_t track_get_mask(void);

/**
 * @brief 加权横向偏差，约归一化到 [-1, 1]
 * @note  负=线偏左（视通道 0 为最左），正=偏右；丢线时保持 last 有效值
 */
float track_get_error(void);

uint8_t track_get_on_count(void);

/** 当前帧无通道压线 */
bool track_is_lost(void);

/**
 * @brief 设置赛道有效电平（数字）/ 极性（GS08 是否翻转黑线判定）
 * @param active_level 0 或 1；对 GS08：0=黑为赛道（默认），1=白为赛道
 */
void track_set_polarity(uint8_t active_level);
uint8_t track_get_polarity(void);

/** 编译期通道数 */
static inline uint8_t track_channel_count(void)
{
    return (uint8_t)TRACK_CH_NUM;
}

/** 后端名，便于 status 打印 */
const char *track_backend_name(void);

/**
 * @brief GS08 专用：把当前 raw 记为白/黑标定；非 GS08 返回 EXIT_NOT_SUPPORTED
 */
exit_code_t track_cal_set_max(void);
exit_code_t track_cal_set_min(void);

/**
 * @brief GS08 二值化阈值（0~100 归一化域）；非 GS08 返回 EXIT_NOT_SUPPORTED
 */
exit_code_t track_set_gs_threshold(uint8_t threshold);
uint8_t track_get_gs_threshold(void);

#ifdef __cplusplus
}
#endif

#endif /* __TRACK_H__ */
