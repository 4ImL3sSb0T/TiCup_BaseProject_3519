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
#include "service/fs/fs_service.h"
#include "service/com/param.h"
#include "service/com/cmd_service.h"
#include "service/motion/motor.h"
#include "service/motion/encoder.h"
#include "service/motion/chassis.h"
#include "driver/track.h"
#include "service/imu/imu.h"
#include "service/gui/gui_app.h"
#include "app/mission.h"
#include "app/track_app.h"
#include "bsp/notice/notice.h"

/*
 * 硬件资源划分 —— 权威文档：docs/hardware.md（改脚前必读并回写）
 *   - 左电机 M4: PWM B10(TIM_G0 CH0) + DIR B11
 *   - 右电机 M2: PWM B12(TIM_A0 CH2) + DIR B13
 *   - 编码器: TIM_G8 / TIM_G9 正交（A26/A27、B7/B9）
 *   - 1ms 节拍: PIT_TIM_G12
 *   - 调试串口 + 命令/日志: UART0 (A10/A11) —— 当前主通道
 *   - 无线转串口 UART1（B6/B7/B2）: 暂关闭（B7 给右编码器）
 *   - 核心板 LED: A14
 *   - IMU: SPI1（B23/B22/B21 + CS B19）
 *   - GUI: IPS200 SPI0（A12/A9/A7/A15/A8/A13）+ 按键 A30/A31/B0/B1
 *
 * 多速率任务:
 *   - 1ms  soft: imu_update
 *   - 2ms  soft: encoder + motor
 *   - 5ms  soft: GUI 按键扫描（gui_app）
 *   - 10ms soft: chassis 外环
 *   - 10ms soft: mission 任务状态机
 *   - 10ms soft: track_app 循迹调试
 *   - 20ms soft: 命令服务
 *   - 100ms soft: GUI 刷新（gui_app）
 *   - A14: mission 声光（LED+蜂鸣器），非心跳
 */

#define SYS_TICK_PIT            (PIT_TIM_G12)
#define CMD_POLL_INTERVAL_MS    (20U)
#define IMU_UPDATE_MS          (1U)
#define MOTION_UPDATE_MS       (2U)
#define CHASSIS_UPDATE_MS      (10U)
#define MISSION_UPDATE_MS      (10U)
#define TRACK_APP_UPDATE_MS    (10U)

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

static void mission_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    mission_update();
}

static void track_app_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    track_app_update();
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
    /*
     * 初始化顺序（按依赖）：
     *   clock/debug/dwt → log → event → soft_timer → param
     *   → motor(+encoder)【尽早，fs/GUI 之前把 PWM 置 0】
     *   → fs → cmd → LED → imu → chassis → param_load
     *   → imu_calibrate(use_flash) → GUI → 节拍
     *
     * 注意：
     *   - event_init() 内会调 sys_log_text；log 须先 init。
     *   - soft_timer 创建/启动依赖 event。
     *   - motor 仅依赖 clock + log + param_init（param_add）；不依赖 fs。
     *   - fs 可能较慢（mount/format），故放在 motor 之后，缩短电机脚悬空时间。
     *   - param_load 须在全部 param_add（motor/chassis/imu）且 fs 就绪之后。
     *   - imu_calibrate(true) 依赖 param_load 已恢复 imu_calib_valid / 零偏。
     *   - 当前 SYS_LOG_UART：不 init 无线，B7 留给右编码器。
     *   - GUI 占用 SPI0 屏脚；WiFi SPI 仍暂关。
     */
    /* 1. 基础时钟 / 调试串口 / 周期计数 */
    clock_init(SYSTEM_CLOCK_80M);
    debug_init();
    dwt_init();

    /* 2. 日志：Debug UART0（无线暂关） */
    sys_log_init(SYS_LOG_UART);
    sys_log_text(info, "==== BaseProject_3519 boot ====");

    /* 3. 事件系统 */
    event_init();

    /* 4. 软件定时器 */
    soft_timer_init();

    /* 5. 参数注册表（不 load；业务 param_add 完成后再 load） */
    if (param_init() != EXIT_OK) {
        sys_log_text(error, "param_init failed");
    } else {
        sys_log_text(info, "param_init ok");
    }

    /* 6. 双电机 + 双编码器（尽早：fs/IMU/GUI 之前把 PWM 强制 0） */
    if (motor_init() != EXIT_OK) {
        sys_log_text(error, "motor_init failed");
    } else {
        sys_log_text(info, "motor_init ok");
    }

    /* 7. LittleFS（DATA Flash）— 可能较慢；须在 param_load 之前 */
    if (fs_init() != EXIT_OK) {
        sys_log_text(error, "fs_init failed");
    } else {
        sys_log_text(info, "fs_init ok");
    }

    /* 8. 命令服务（仅 Debug UART0；无线暂关） */
    cmd_service_init();
    sys_log_text(info, "cmd_service_init ok (debug UART0 only, wireless off)");

    /* 9. 声光 notice（A14）；mission_init 内也会 notice_init，此处尽早置低 */
    if (notice_init() != EXIT_OK) {
        sys_log_text(warning, "notice_init skip/fail");
    }

    /* 10. IMU（6 轴，无磁力计）— 仅 init + 注册参数，校准放到 param_load 之后 */
    exit_code_t imu_ret = EXIT_FAIL;
    {
        imu_ret = imu_init(imu_mode_no_mag);
        if (imu_ret != EXIT_OK) {
            sys_log_text(error, "imu_init failed");
        } else {
            sys_log_text(info, "imu_init ok (no_mag)");
        }

        /* 11. 底盘（依赖 motor；IMU 可选闭环） */
        if (chassis_init() != EXIT_OK) {
            sys_log_text(error, "chassis_init failed");
        } else {
            sys_log_text(info, "chassis_init ok");
            chassis_set_imu_ready(imu_ret == EXIT_OK);
        }

        /* 11b. mission（内部 track_init；依赖 chassis + imu） */
        if (mission_init() != EXIT_OK) {
            sys_log_text(error, "mission_init failed");
        } else {
            sys_log_text(info, "mission_init ok");
        }

        /* 11c. 循迹调试 app（与 mission 共用 track 驱动；互斥运行） */
        if (track_app_init() != EXIT_OK) {
            sys_log_text(error, "track_app_init failed");
        } else {
            sys_log_text(info, "track_app_init ok");
        }
    }

    /* 12. 全部 param_add 完成后从 LFS /param.txt 恢复
     *     param_load 成功时内部会 motor_apply_param()；失败则仍用 motor_init 默认 PID */
    if (param_load("boot") != 0) {
        sys_log_text(warning, "param_load boot failed (using defaults)");
        motor_apply_param(); /* 仍把当前 RAM 注册表（默认）刷进 PID */
    }

    /* 12b. IMU 校准：优先用 Flash；无有效参数则现场校准并写入 */
    if (imu_ret == EXIT_OK) {
        imu_calibrate(false);
    }

    /* 13. GUI：IPS200 + 板载按键（自建 5ms/100ms soft_timer） */
    if (gui_app_init() != EXIT_OK) {
        sys_log_text(error, "gui_app_init failed");
    } else {
        sys_log_text(info, "gui_app_init ok");
    }

    /* 14. 1ms 系统节拍 */
    pit_ms_init(SYS_TICK_PIT, 1, pit_1ms_callback, NULL);
    sys_log_text(info, "sys tick 1ms on PIT_G12");

    /* 15. 周期任务 */
    (void)app_start_timer(CMD_POLL_INTERVAL_MS, cmd_service_task, "cmd");
    (void)app_start_timer(IMU_UPDATE_MS, imu_task, "imu");
    (void)app_start_timer(MOTION_UPDATE_MS, motion_task, "motion");
    (void)app_start_timer(CHASSIS_UPDATE_MS, chassis_task, "chassis");
    // (void)app_start_timer(MISSION_UPDATE_MS, mission_task, "mission");
    (void)app_start_timer(TRACK_APP_UPDATE_MS, track_app_task, "track_app");

    /* 16. 开全局中断 */
    interrupt_global_enable(0);

    sys_log_text(info,
                 "init done. try: help / track start|status / mission start 1"
                 " (UART0); GUI keys A30/A31/B0/B1");
}

int main(void)
{
    app_init();

    while (true) {
        soft_timer_process();
        event_process_async();
    }
}
