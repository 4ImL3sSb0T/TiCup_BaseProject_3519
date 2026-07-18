/*********************************************************************************************************************
* MSPM0G3519 Opensource Library 即（MSPM0G3519 开源库）是一个基于官方 SDK 接口的第三方开源库
* Copyright (c) 2022 SEEKFREE 逐飞科技
*
* 文件名称          main
* 公司名称          成都逐飞科技有限公司
* 版本信息          查看 libraries/doc 文件夹内 version 文件 版本说明
* 开发环境          MDK 5.38
* 适用平台          MSPM0G3519
*
* 修改记录
* 日期              作者                备注
* 2026-07-18       TI_Cup              从 BaseProject(MSPM0G3507) 移植
********************************************************************************************************************/

#include "zf_common_headfile.h"

#include "common/event/event.h"
#include "common/event/soft_timer.h"
#include "service/sys/sys_log.h"
#include "service/sys/sys_time.h"
#include "service/com/param.h"
#include "service/com/cmd_service.h"
#include "service/motion/motor.h"
#include "service/motion/encoder.h"
#include "service/motion/chassis.h"
#include "service/imu/imu.h"

/*
 * 硬件资源划分（避免冲突）:
 *   - 电机 PWM: TIM_A0 (A0/B12) + DIR GPIO (A1/B13)
 *   - 编码器:   TIM_G7 / TIM_G6
 *   - 1ms 节拍: PIT_TIM_G12  (不可用 A0/G6/G7；3519 另有 G9/G14 可用)
 *   - 调试串口: UART0 (A10/A11)
 *   - 命令串口: UART3 (A14/A13)  // 注意 A14 也是核心板 LED，尽量保留
 *   - 3519 无 UART2；同引脚场景可用 UART7 兼容
 *   - 日志默认: 调试串口 (SYS_LOG_UART)；有 WiFi SPI 模块可改 SYS_LOG_WIFI
 *
 * 多速率任务:
 *   - 1ms  soft: imu_update（匹配 Madgwick sampleFreq=1000）
 *   - 2ms  soft: encoder + motor
 *   - 10ms soft: chassis 外环
 *   - 20ms soft: 命令服务
 */

#define SYS_TICK_PIT            (PIT_TIM_G12)
#define CMD_POLL_INTERVAL_MS    (20U)
#define IMU_UPDATE_MS          (1U)
#define MOTION_UPDATE_MS       (2U)
#define CHASSIS_UPDATE_MS      (10U)

/* -------------------------------------------------------------------------- */
/* 1ms 硬件定时：仅推进系统时间（不跑重逻辑）                                    */
/* -------------------------------------------------------------------------- */
static void pit_1ms_callback(uint32 event, void *ptr)
{
    (void)event;
    (void)ptr;
    sys_time_ms++;
}

/* -------------------------------------------------------------------------- */
/* 周期任务回调                                                                */
/* -------------------------------------------------------------------------- */
static void imu_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    imu_update();
}

static void motion_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    encoder_update();
    motor_update();
}

static void chassis_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    chassis_update();
}

static soft_timer_id_t app_start_timer(uint32_t period_ms,
                                       void (*cb)(const event_t *, void *),
                                       const char *name)
{
    soft_timer_id_t id;

    id = soft_timer_create_simple(period_ms, TIMER_MODE_PERIODIC, cb,
                                  EVENT_DISPATCH_SYNC);
    if (id == 0) {
        sys_log_text(error, "%s soft_timer create failed", name);
        return 0;
    }
    soft_timer_start(id);
    soft_timer_set_name(id, name);
    return id;
}

static void app_init(void)
{
    /* 1. 基础时钟 / 调试串口（逐飞库要求保留） */
    clock_init(SYSTEM_CLOCK_80M);
    debug_init();
    dwt_init();

    /* 2. 事件 + 软件定时器 */
    event_init();
    soft_timer_init();

    /* 3. 日志（默认 UART；WiFi 模块就绪后可改为 SYS_LOG_WIFI） */
    sys_log_init(SYS_LOG_WIFI);
    sys_log_text(info, "==== BaseProject_3519 boot ====");

    /* 4. 参数系统（Flash 持久化暂未启用） */
    if (param_init() != EXIT_OK) {
        sys_log_text(error, "param_init failed");
    } else {
        sys_log_text(info, "param_init ok");
    }

    /* 5. 命令服务（WiFi / Debug UART / UART3） */
    cmd_service_init();
    sys_log_text(info, "cmd_service_init ok (UART3 A14/A13, debug UART0)");

    /* 6. 双电机 + 双编码器（DRV8701） */
    if (motor_init() != EXIT_OK) {
        sys_log_text(error, "motor_init failed");
    } else {
        sys_log_text(info, "motor_init ok");
    }

    /* 7. IMU（6 轴，无磁力计） */
    {
        exit_code_t imu_ret = imu_init(imu_mode_no_mag);
        if (imu_ret != EXIT_OK) {
            sys_log_text(error, "imu_init failed");
        } else {
            sys_log_text(info, "imu_init ok (no_mag)");
        }

        /* 8. 底盘服务（依赖 motor；IMU 可选） */
        if (chassis_init() != EXIT_OK) {
            sys_log_text(error, "chassis_init failed");
        } else {
            sys_log_text(info, "chassis_init ok");
            chassis_set_imu_ready(imu_ret == EXIT_OK);
        }
    }

    /* 9. 1ms 系统节拍（PIT_G12，避开电机/编码器定时器） */
    pit_ms_init(SYS_TICK_PIT, 1, pit_1ms_callback, NULL);
    sys_log_text(info, "sys tick 1ms on PIT_G12");

    /* 10. 周期任务 */
    (void)app_start_timer(CMD_POLL_INTERVAL_MS, cmd_service_task, "cmd");
    (void)app_start_timer(IMU_UPDATE_MS, imu_task, "imu");
    (void)app_start_timer(MOTION_UPDATE_MS, motion_task, "motion");
    (void)app_start_timer(CHASSIS_UPDATE_MS, chassis_task, "chassis");

    /* 11. 开全局中断 */
    interrupt_global_enable(0);

    sys_log_text(info, "init done. try: help / chassis status / motor 0x3 status");
}

int main(void)
{
    app_init();

    while (true) {
        /* 软件定时器扫描 + 异步事件出队 */
        soft_timer_process();
        event_process_async();
    }
}
