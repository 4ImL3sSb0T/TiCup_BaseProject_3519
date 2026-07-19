#ifndef __MJC_INPUT_BUTTON_H
#define __MJC_INPUT_BUTTON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize multi-button driven input for MujicaUI.
 * @return 1 on success, 0 on failure.
 */
uint8_t mjc_buttons_init(void);

/** Call every 5ms (TICKS_INTERVAL) to poll buttons. */
void mjc_buttons_tick_5ms(void);

/**
 * @brief Enable/disable default menu navigation handler.
 * When disabled, multi_button still publishes BUTTON_EVENT_* for other subscribers.
 */
void mjc_input_set_enabled(uint8_t enabled);
uint8_t mjc_input_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* !__MJC_INPUT_BUTTON_H */
