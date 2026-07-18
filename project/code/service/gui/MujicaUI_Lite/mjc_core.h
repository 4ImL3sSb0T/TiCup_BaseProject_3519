#ifndef __MJC_CORE_H
#define __MJC_CORE_H

#include "mjc_define.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t mjc_init(mjc_page_t* root_page);
uint8_t mjc_add_submenu(mjc_page_t* parent_page, mjc_item_t* parent_item, mjc_page_t* submenu_page);
uint8_t mjc_update(void);

// Accessors for current navigation state
mjc_page_t* mjc_get_current_page(void);
uint8_t mjc_set_current_page(mjc_page_t* page);
mjc_item_t* mjc_get_selected_item(void);

uint8_t mjc_item_execute(mjc_item_t* item, mjc_event_type_t event);

uint8_t mjc_page_up(void);
uint8_t mjc_page_down(void);
uint8_t mjc_page_back(void);

#ifdef __cplusplus
}
#endif

#endif // !__MJC_CORE_H
