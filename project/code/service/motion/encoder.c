/**
 * @file encoder.c
 * @brief 编码器模块实现（双编码器）
 *
 * 硬件：方向编码器模式 encoder_dir_init
 * 引脚：E2_02_encoder_dir_demo（TIM_G7 / TIM_G6）
 */
#include "encoder.h"
#include "common/filter/moving_average_filter.h"
#include "service/sys/sys_log.h"

motor_encoder_t encoder_left = {
    .encoder_index = ENCODER_LEFT_TIMER,
    .channel1 = ENCODER_LEFT_LSB,
    .channel2 = ENCODER_LEFT_DIR,
    .polarity = 0
};

motor_encoder_t encoder_right = {
    .encoder_index = ENCODER_RIGHT_TIMER,
    .channel1 = ENCODER_RIGHT_LSB,
    .channel2 = ENCODER_RIGHT_DIR,
    .polarity = 1
};

static motor_encoder_t *const encoder_list[] = {
    &encoder_left,
    &encoder_right,
};

#define ENCODER_COUNT  (sizeof(encoder_list) / sizeof(encoder_list[0]))

static void encoder_init_single(motor_encoder_t *encoder)
{
    encoder_dir_init(encoder->encoder_index, encoder->channel1, encoder->channel2);
    moving_average_filter_trim_init(&encoder->filter, encoder->filter_buffer,
                                    ENCODER_FILTER_WINDOW_SIZE, 1);
    encoder->raw_speed = 0;
    encoder->filtered_speed = 0.0f;
    encoder->position = 0;
}

void encoder_init(void)
{
    uint8 i;

    sys_log_text(info, "Encoder: init dual dir-encoder (G7/A26+B27, G6/B10+B11)...");
    for (i = 0; i < ENCODER_COUNT; i++) {
        encoder_init_single(encoder_list[i]);
    }
    sys_log_text(info, "Encoder: ready (count=%u)", (unsigned)ENCODER_COUNT);
}

static void encoder_update_single_internal(motor_encoder_t *encoder)
{
    int16 count;

    if (encoder == NULL) {
        return;
    }

    count = encoder_get_count(encoder->encoder_index);
    encoder->raw_speed = encoder->polarity ? (int16)(-count) : count;
    encoder->position += encoder->raw_speed;
    encoder_clear_count(encoder->encoder_index);

    encoder->filtered_speed = moving_average_filter_trim_update(
        &encoder->filter, (filter_data_t)encoder->raw_speed);
}

void encoder_update_single(motor_encoder_t *encoder)
{
    encoder_update_single_internal(encoder);
}

void encoder_update(void)
{
    uint8 i;
    for (i = 0; i < ENCODER_COUNT; i++) {
        encoder_update_single_internal(encoder_list[i]);
    }
}

int32 encoder_get_position(motor_encoder_t *encoder)
{
    if (encoder != NULL) {
        return (int32)encoder->position;
    }
    return 0;
}

void encoder_clear_position(motor_encoder_t *encoder)
{
    if (encoder != NULL) {
        encoder->position = 0;
    }
}

int16 encoder_get_speed(motor_encoder_t *encoder)
{
    if (encoder != NULL) {
        return encoder->raw_speed;
    }
    return 0;
}

float encoder_get_filtered_speed(motor_encoder_t *encoder)
{
    if (encoder != NULL) {
        return encoder->filtered_speed;
    }
    return 0.0f;
}
