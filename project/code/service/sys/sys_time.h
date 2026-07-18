#ifndef __SYS_TIME_H
#define __SYS_TIME_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t sys_time_ms;

uint32_t sys_time_get_ms(void);

// DWT 周期计数器（Cortex-M0+ 不支持，提供空实现）
void dwt_init(void);

// DWT 宏在 Cortex-M0+ 上替换为返回 0（基于 sys_time_ms 的微秒近似）
#define DWT_GET_CYCLES()   (sys_time_ms * 80000UL)  // 80MHz → 1ms = 80000 cycles
#define DWT_RESET()        ((void)0)
#define DWT_START()        ((void)0)
#define DWT_STOP()         ((void)0)

#ifdef __cplusplus
}
#endif

#endif // !__SYS_TIME_H
