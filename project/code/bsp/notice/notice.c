/**
 * @file notice.c
 * @brief A14 声光：电平 + 非阻塞 beep
 */
#include "bsp/notice/notice.h"

#include "service/sys/sys_log.h"
#include "service/sys/sys_time.h"
#include "zf_driver_gpio.h"

static bool s_inited;
static bool s_beep_active;
static uint32_t s_beep_until_ms;

exit_code_t notice_init(void)
{
    if (s_inited) {
        return EXIT_ALREADY_INITIALIZED;
    }

    gpio_init(NOTICE_PIN, GPO, GPIO_LOW, GPO_PUSH_PULL);
    s_beep_active = false;
    s_inited = true;
    sys_log_text(info, "notice: pin A14 ready");
    return EXIT_OK;
}

void notice_set(bool on)
{
    if (!s_inited) {
        return;
    }
    gpio_set_level(NOTICE_PIN, on ? GPIO_HIGH : GPIO_LOW);
    if (!on) {
        s_beep_active = false;
    }
}

void notice_on(void)
{
    notice_set(true);
}

void notice_off(void)
{
    notice_set(false);
}

void notice_beep(uint16_t duration_ms)
{
    if (!s_inited) {
        return;
    }
    if (duration_ms == 0u) {
        notice_off();
        return;
    }
    notice_on();
    s_beep_active = true;
    s_beep_until_ms = sys_time_get_ms() + (uint32_t)duration_ms;
}

void notice_update(void)
{
    if (!s_inited || !s_beep_active) {
        return;
    }
    if ((int32_t)(sys_time_get_ms() - s_beep_until_ms) >= 0) {
        notice_off();
    }
}

bool notice_busy(void)
{
    return s_beep_active;
}
