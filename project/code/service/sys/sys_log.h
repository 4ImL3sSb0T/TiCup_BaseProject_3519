#ifndef _WIFI_DEBUG_H_
#define _WIFI_DEBUG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYS_LOG_WIFI,
    SYS_LOG_UART,
} sys_log_type_e;

void sys_log_init(sys_log_type_e log_type);
void sys_log_send_data(const uint8_t *data, uint32_t len);
void sys_log_printf(const char *fmt, ...);

// 方便使用的文本日志宏
#define sys_log_text(window, fmt, args...) sys_log_printf("{" #window "}" fmt "\n", ##args)
#define sys_log_stamp(window, fmt, ...) sys_log_printf("<%u>{" #window "}" fmt "\n", sys_time_get_ms(), __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
