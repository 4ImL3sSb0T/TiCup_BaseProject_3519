#ifndef __CMD_SERVICE_H_
#define __CMD_SERVICE_H_

#include "common/event/event.h"

#ifdef __cplusplus
extern "C" {
#endif

void cmd_service_init(void);
void cmd_service_task(const event_t *event, void *user_data);

#ifdef __cplusplus
}
#endif

#endif // !__CMD_SERVICE_H_
