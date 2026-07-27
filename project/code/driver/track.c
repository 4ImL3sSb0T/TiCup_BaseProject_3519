/**
 * @file track.c
 * @brief 光电循迹驱动层实现
 */
#include "driver/track.h"

#include "service/sys/sys_log.h"

#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
#include "zf_device_gs08ra.h"
#endif

/* -------------------------------------------------------------------------- */
/* 状态                                                                         */
/* -------------------------------------------------------------------------- */
static bool s_inited;
static uint8_t s_polarity = TRACK_ACTIVE_LEVEL_DEF; /* 见 track_set_polarity */
static uint8_t s_mask;
static uint8_t s_on_count;
static float s_error;
static float s_last_error;
static bool s_lost = true;

#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_DIGITAL5)
static const gpio_pin_enum s_pins[TRACK_CH_NUM] = {
    TRACK_PIN_0,
    TRACK_PIN_1,
    TRACK_PIN_2,
    TRACK_PIN_3,
    TRACK_PIN_4,
};
/* 整数权重 ×1，归一化除以 |w|_max=2 */
static const int8_t s_weights[TRACK_CH_NUM] = { -10, -3, 0, 3, 10 };
static const float s_w_max = 2.0f;
#else
/* 八路：权重 ×2 存整数，实际为 -3.5..+3.5，归一化除以 3.5 */
static const int8_t s_weights[TRACK_CH_NUM] = {
    -7, -5, -3, -1, 1, 3, 5, 7
};
static const float s_w_max = 7.0f; /* 对应 |weight|/1，与 sum 同尺度 */
#endif

/* -------------------------------------------------------------------------- */
/* 工具                                                                         */
/* -------------------------------------------------------------------------- */
static void track_compute_from_bits(const uint8_t *on_bits)
{
    int32_t sum_w = 0;
    uint8_t n_on = 0;
    uint8_t mask = 0;
    uint8_t i;

    for (i = 0; i < TRACK_CH_NUM; i++) {
        if (on_bits[i]) {
            n_on++;
            mask = (uint8_t)(mask | (1u << i));
            sum_w += s_weights[i];
        }
    }

    s_mask = mask;
    s_on_count = n_on;

    if (n_on == 0u) {
        s_lost = true;
        /* 保持 s_error = s_last_error，供丢线回转 */
        return;
    }

    s_lost = false;
    s_error = ((float)sum_w / (float)n_on) / s_w_max;
    s_last_error = s_error;
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                     */
/* -------------------------------------------------------------------------- */
const char *track_backend_name(void)
{
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_DIGITAL5)
    return "digital5";
#else
    return "gs08ra8";
#endif
}

exit_code_t track_init(void)
{
    if (s_inited) {
        return EXIT_ALREADY_INITIALIZED;
    }

#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_DIGITAL5)
    {
        uint8_t i;
        for (i = 0; i < TRACK_CH_NUM; i++) {
            /* 上拉输入；默认高=背景、低=赛道 */
            gpio_init(s_pins[i], GPI, GPIO_HIGH, GPI_PULL_UP);
        }
    }
#else
    gs08ra_init();
#endif

    s_polarity = TRACK_ACTIVE_LEVEL_DEF;
    s_mask = 0;
    s_on_count = 0;
    s_error = 0.0f;
    s_last_error = 0.0f;
    s_lost = true;
    s_inited = true;

    sys_log_text(info, "Track: init backend=%s ch=%u pol=%u",
                 track_backend_name(), (unsigned)TRACK_CH_NUM,
                 (unsigned)s_polarity);
    return EXIT_OK;
}

void track_scan(void)
{
    uint8_t on_bits[TRACK_CH_NUM];
    uint8_t i;

    if (!s_inited) {
        return;
    }

#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_DIGITAL5)
    for (i = 0; i < TRACK_CH_NUM; i++) {
        uint8_t level = gpio_get_level(s_pins[i]);
        on_bits[i] = (level == s_polarity) ? 1u : 0u;
    }
#else
    gs08ra_scan_read();
    /*
     * 库：bin_val 1=白 0=黑
     * polarity 0：黑为赛道 → on_line = (bin==0)
     * polarity 1：白为赛道 → on_line = (bin==1)
     */
    for (i = 0; i < TRACK_CH_NUM; i++) {
        uint8_t is_black = (gs08ra_bin_val[i] == 0u) ? 1u : 0u;
        if (s_polarity == 0u) {
            on_bits[i] = is_black;
        } else {
            on_bits[i] = (uint8_t)(1u - is_black);
        }
    }
#endif

    track_compute_from_bits(on_bits);
}

uint8_t track_get_mask(void)
{
    return s_mask;
}

float track_get_error(void)
{
    return s_error;
}

uint8_t track_get_on_count(void)
{
    return s_on_count;
}

bool track_is_lost(void)
{
    return s_lost;
}

void track_set_polarity(uint8_t active_level)
{
    s_polarity = (active_level != 0u) ? 1u : 0u;
}

uint8_t track_get_polarity(void)
{
    return s_polarity;
}

exit_code_t track_cal_set_max(void)
{
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
    if (!s_inited) {
        return EXIT_NOT_INITIALIZED;
    }
    gs08ra_scan_read();
    gs08ra_set_max();
    return EXIT_OK;
#else
    return EXIT_NOT_SUPPORTED;
#endif
}

exit_code_t track_cal_set_min(void)
{
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
    if (!s_inited) {
        return EXIT_NOT_INITIALIZED;
    }
    gs08ra_scan_read();
    gs08ra_set_min();
    return EXIT_OK;
#else
    return EXIT_NOT_SUPPORTED;
#endif
}

exit_code_t track_set_gs_threshold(uint8_t threshold)
{
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
    gs08ra_set_threshold(threshold);
    return EXIT_OK;
#else
    (void)threshold;
    return EXIT_NOT_SUPPORTED;
#endif
}

uint8_t track_get_gs_threshold(void)
{
#if (TRACK_SENSOR_BACKEND == TRACK_BACKEND_GS08RA8)
    return gs08ra_threshold;
#else
    return 0u;
#endif
}
