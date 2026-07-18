#ifndef __MJC_INPUT_BUTTON_H
#define __MJC_INPUT_BUTTON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize multi-button driven input for MujicaUI.
// Returns 1 on success, 0 on failure.
uint8_t mjc_buttons_init(void);

// Call every 5ms (consistent with TICKS_INTERVAL) to poll buttons.
void mjc_buttons_tick_5ms(void);

#ifdef __cplusplus
}
#endif

#endif // !__MJC_INPUT_BUTTON_H
