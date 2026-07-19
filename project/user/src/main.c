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
#include "service/imu/imu.h"

/*
 * 硬件资源划分 —— 权威文档：docs/hardware.md（改脚前必读并回写）
 *   - 电机/编码器/底盘：暂关闭（B7 让给无线串口；WiFi SPI 暂不可用）
 *   - 1ms 节拍: PIT_TIM_G12
 *   - 调试串口: UART0 (A10/A11)，兼命令入口
 *   - 核心板 LED: A14
 *   - 命令/日志: 无线转串口 UART1（B6 TX / B7 RX / B2 RTS）+ Debug UART0
 *   - IMU: SPI1（B23/B22/B21 + CS B19）
 *
 * 多速率任务:
 *   - 1ms  soft: imu_update
 *   - 20ms soft: 命令服务
 *   - 500ms soft: 核心板 LED 心跳（A14）
 */

#define SYS_TICK_PIT            (PIT_TIM_G12)
#define CMD_POLL_INTERVAL_MS    (20U)
#define IMU_UPDATE_MS          (1U)
#define LED_BLINK_INTERVAL_MS   (500U)
#define LED_PIN                 (A14)

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

/* 核心板蓝色 LED 心跳（E01_gpio_demo: A14 翻转） */
static void led_blink_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    gpio_toggle_level(LED_PIN);
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
     *   clock/debug/dwt → log → event → soft_timer → 业务
     *
     * 关键：
     *   - event_init() 内会调 sys_log_text；log 未 init 时默认 SYS_LOG_WIRELESS，
     *     会写未初始化的 wireless_uart。
     *   - soft_timer 创建/启动依赖 event（subscribe/publish）。
     *   - cmd_service 假定无线串口已由 sys_log_init 初始化。
     */
    /* 1. 基础时钟 / 调试串口 / 周期计数（log 失败回退 UART0 依赖 debug_init） */
    clock_init(SYSTEM_CLOCK_80M);
    debug_init();
    dwt_init();

    /* 2. 日志最先就绪：无线转串口（B6/B7）；失败回退 UART0 */
    sys_log_init(SYS_LOG_UART);
    sys_log_text(info, "==== BaseProject_3519 boot ====");

    /* 3. 事件系统（init 路径会打日志） */
    event_init();

    /* 4. 软件定时器（依赖 event） */
    soft_timer_init();

    /* 5. 参数系统（Flash 持久化暂未启用） */
    if (param_init() != EXIT_OK) {
        sys_log_text(error, "param_init failed");
    } else {
        sys_log_text(info, "param_init ok");
    }

    /* 6. 命令服务（无线串口 + Debug UART0；无线由 log 已 init） */
    cmd_service_init();
    sys_log_text(info, "cmd_service_init ok (wireless B6/B7 + debug UART0)");

    /* 7. 核心板 LED（A14） */
    gpio_init(LED_PIN, GPO, 0, GPO_PUSH_PULL);
    sys_log_text(info, "LED heartbeat on A14");

    /*
     * 电机 / 编码器 / 底盘 —— 暂关闭
     * 原因：右编码器 CH1 占用 B7，与无线串口 RX 冲突；WiFi SPI 当前不可用。
     * 恢复时：改编码器脚或改通信脚，并同步 docs/hardware.md。
     */
    sys_log_text(info, "motor/encoder/chassis: DISABLED (B7 -> wireless RX)");

    /* 8. IMU（6 轴，无磁力计；init 路径会打日志） */
    {
        exit_code_t imu_ret = imu_init(imu_mode_no_mag);
        if (imu_ret != EXIT_OK) {
            sys_log_text(error, "imu_init failed");
        } else {
            sys_log_text(info, "imu_init ok (no_mag)");
        }
    }

    /* 9. 1ms 系统节拍（驱动 soft_timer 的 sys_time_ms） */
    pit_ms_init(SYS_TICK_PIT, 1, pit_1ms_callback, NULL);
    sys_log_text(info, "sys tick 1ms on PIT_G12");

    /* 10. 周期任务（依赖 soft_timer + event） */
    (void)app_start_timer(CMD_POLL_INTERVAL_MS, cmd_service_task, "cmd");
    (void)app_start_timer(IMU_UPDATE_MS, imu_task, "imu");
    (void)app_start_timer(LED_BLINK_INTERVAL_MS, led_blink_task, "led");

    /* 11. 开全局中断 */
    interrupt_global_enable(0);

    sys_log_text(info, "init done. LED 500ms. try: help (via wireless or UART0)");
}

int main(void)
{
    app_init();

    while (true) {
        soft_timer_process();
        event_process_async();
    }
}
