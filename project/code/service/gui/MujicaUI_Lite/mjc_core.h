#ifndef __MJC_CORE_H
#define __MJC_CORE_H

#include "mjc_define.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t mjc_init(mjc_page_t* root_page);
uint8_t mjc_add_submenu(mjc_page_t* parent_page, mjc_item_t* parent_item, mjc_page_t* submenu_page);
uint8_t mjc_update(void);

/* Accessors for current navigation state */
mjc_page_t* mjc_get_current_page(void);
uint8_t mjc_set_current_page(mjc_page_t* page);
mjc_item_t* mjc_get_selected_item(void);

uint8_t mjc_item_execute(mjc_item_t* item, mjc_event_type_t event);

uint8_t mjc_page_up(void);
uint8_t mjc_page_down(void);
uint8_t mjc_page_back(void);

/* ---- NUMBER edit session (buffered; MAIN commits, AUX cancels) ---- */
uint8_t mjc_is_edit_mode(void);
mjc_item_t* mjc_edit_get_item(void);
float mjc_edit_get_value(void);
float mjc_edit_get_step(void);

/** Enter edit on current selected NUMBER item. Returns 1 on success. */
uint8_t mjc_edit_begin(void);
/** Write buffer to value_ptr, fire MJC_EVENT_CHANGE, leave edit. */
uint8_t mjc_edit_commit(void);
/** Discard buffer (value_ptr unchanged), leave edit. */
uint8_t mjc_edit_cancel(void);
/** dir = +1 / -1: adjust edit buffer by session step, clamp min/max. */
uint8_t mjc_edit_adjust(int8_t dir);
/** dir = +1 step*10, -1 step/10 (session step only). */
uint8_t mjc_edit_scale_step(int8_t dir);

#ifdef __cplusplus
}
#endif

#endif /* !__MJC_CORE_H */
