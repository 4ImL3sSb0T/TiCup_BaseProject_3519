/**
 * @file notice.h
 * @brief 声光提示：LED + 蜂鸣器共用 GPIO A14
 */
#ifndef __NOTICE_H__
#define __NOTICE_H__

#include "common/tools/common_def.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NOTICE_PIN
#define NOTICE_PIN  (A14)
#endif

exit_code_t notice_init(void);

void notice_set(bool on);
void notice_on(void);
void notice_off(void);

/**
 * @brief 非阻塞响/亮 duration_ms，需周期调用 notice_update() 自动关闭
 */
void notice_beep(uint16_t duration_ms);

/** 建议与 mission 同拍 10 ms；处理 beep 超时关断 */
void notice_update(void);

bool notice_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* __NOTICE_H__ */
