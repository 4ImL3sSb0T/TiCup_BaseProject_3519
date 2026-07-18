#ifndef __CMD_SERVICE_H_
#define __CMD_SERVICE_H_

#include "common/event/event.h"
#include "zf_driver_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

void cmd_service_init(void);
void cmd_service_task(const event_t *event, void *user_data);
void cmd_service_uart_rx_irq_handler(uart_index_enum uart_n);

#ifdef __cplusplus
}
#endif

#endif // !__CMD_SERVICE_H_
