#include "cmd_service.h"
#include "common/tools/common_def.h"
#include "zf_common_fifo.h"
#include "zf_device_wireless_uart.h"
#include "parse.h"
#include "cmd_ack.h"
#include "service/sys/sys_log.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CMD_SERVICE_RX_BUFFER_SIZE              256
#define CMD_SERVICE_FIFO_BUFFER_SIZE_WIRELESS   256
#define CMD_SERVICE_FIFO_BUFFER_SIZE_DEBUG      256

/*
 * 命令入口（当前启用）：
 *   - 无线转串口 UART1（B6 TX / B7 RX / B2 RTS）—— 与日志共用，由 sys_log_init 初始化
 *   - Debug UART0（A10/A11）
 *
 * WiFi SPI：暂不可用，不在此轮询。
 * UART3（A14/A13）：暂禁用。
 * 右编码器曾用 B7；当前底盘/电机/编码器关闭，B7 让给无线串口。
 */

#define CMD_SERVICE_LOG_NONE   0
#define CMD_SERVICE_LOG_ERROR  1
#define CMD_SERVICE_LOG_INFO   2
#define CMD_SERVICE_LOG_DEBUG  3

#define CMD_SERVICE_LOG_LEVEL  CMD_SERVICE_LOG_ERROR

#define CMD_LOG_ERROR(fmt, args...) \
    do { if (CMD_SERVICE_LOG_LEVEL >= CMD_SERVICE_LOG_ERROR) \
         sys_log_text(cmd, "ERROR: " fmt, ##args); } while(0)

#define CMD_LOG_INFO(fmt, args...) \
    do { if (CMD_SERVICE_LOG_LEVEL >= CMD_SERVICE_LOG_INFO) \
         sys_log_text(cmd, "INFO: " fmt, ##args); } while(0)

#define CMD_LOG_DEBUG(fmt, args...) \
    do { if (CMD_SERVICE_LOG_LEVEL >= CMD_SERVICE_LOG_DEBUG) \
         sys_log_text(cmd, "DEBUG: " fmt, ##args); } while(0)

typedef enum
{
    CMD_SERVICE_SOURCE_WIRELESS = 0,
    CMD_SERVICE_SOURCE_DEBUG,
    CMD_SERVICE_SOURCE_COUNT
} cmd_service_source_t;

typedef struct
{
    fifo_struct fifo;
    u8 *buffer;
    u32 buffer_size;
    const char *name;
} cmd_service_rx_channel_t;

static u8 cmd_service_io_buffer[CMD_SERVICE_RX_BUFFER_SIZE];
static u8 cmd_service_line_buffer[CMD_SERVICE_RX_BUFFER_SIZE];
static u8 cmd_service_peek_buffer[CMD_SERVICE_RX_BUFFER_SIZE];

static u8 cmd_service_wireless_fifo_buffer[CMD_SERVICE_FIFO_BUFFER_SIZE_WIRELESS];
static u8 cmd_service_debug_fifo_buffer[CMD_SERVICE_FIFO_BUFFER_SIZE_DEBUG];

static cmd_service_rx_channel_t cmd_service_channels[CMD_SERVICE_SOURCE_COUNT] = {
    {
        .buffer = cmd_service_wireless_fifo_buffer,
        .buffer_size = CMD_SERVICE_FIFO_BUFFER_SIZE_WIRELESS,
        .name = "wireless",
    },
    {
        .buffer = cmd_service_debug_fifo_buffer,
        .buffer_size = CMD_SERVICE_FIFO_BUFFER_SIZE_DEBUG,
        .name = "debug",
    },
};

static bool cmd_service_parse_seq_envelope(char *line, i32 *seq, char **payload)
{
    if (line == NULL || seq == NULL || payload == NULL) {
        return false;
    }

    *seq = -1;
    *payload = line;

    if (line[0] != '@') {
        return true;
    }

    char *space = strchr(line, ' ');
    if (space == NULL || space == (line + 1)) {
        return false;
    }

    *space = '\0';
    char *endptr = NULL;
    long parsed_seq = strtol(line + 1, &endptr, 10);
    if (endptr == NULL || *endptr != '\0' || parsed_seq < 0 || parsed_seq > INT32_MAX) {
        return false;
    }

    char *cmd = space + 1;
    while (*cmd == ' ') {
        cmd++;
    }
    if (*cmd == '\0') {
        return false;
    }

    *seq = (i32)parsed_seq;
    *payload = cmd;
    return true;
}

static bool cmd_service_fifo_push_byte(cmd_service_rx_channel_t *channel, u8 value)
{
    if (channel == NULL) {
        return false;
    }
    return (fifo_write_buffer(&channel->fifo, &value, 1) == FIFO_SUCCESS);
}

static u32 cmd_service_fifo_push_buffer(cmd_service_rx_channel_t *channel, const u8 *data, u32 len)
{
    if (channel == NULL || data == NULL || len == 0) {
        return 0;
    }

    u32 pushed = 0;
    for (u32 i = 0; i < len; i++) {
        if (cmd_service_fifo_push_byte(channel, data[i])) {
            pushed++;
        }
    }
    return pushed;
}

static bool cmd_service_find_newline(cmd_service_rx_channel_t *channel, u32 scan_limit, u32 *newline_pos)
{
    if (channel == NULL || newline_pos == NULL) {
        return false;
    }

    u32 peek_len = fifo_used(&channel->fifo);
    if (peek_len == 0) {
        return false;
    }
    if (peek_len > scan_limit) {
        peek_len = scan_limit;
    }

    if (fifo_read_buffer(&channel->fifo, cmd_service_peek_buffer, &peek_len, FIFO_READ_ONLY) != FIFO_SUCCESS) {
        return false;
    }

    for (u32 i = 0; i < peek_len; i++) {
        if (cmd_service_peek_buffer[i] == '\n') {
            *newline_pos = i;
            return true;
        }
    }
    return false;
}

static void cmd_service_process_channel(cmd_service_rx_channel_t *channel)
{
    if (channel == NULL) {
        return;
    }

    while (1) {
        u32 used = fifo_used(&channel->fifo);
        if (used == 0) {
            break;
        }

        if (used >= COMMAND_MAX_LEN) {
            bool has_newline = false;
            u32 check_len = (used > COMMAND_MAX_LEN) ? COMMAND_MAX_LEN : used;
            if (fifo_read_buffer(&channel->fifo, cmd_service_peek_buffer, &check_len, FIFO_READ_ONLY) == FIFO_SUCCESS) {
                for (u32 i = 0; i < check_len; i++) {
                    if (cmd_service_peek_buffer[i] == '\n') {
                        has_newline = true;
                        break;
                    }
                }
            }

            if (!has_newline) {
                CMD_LOG_ERROR("[%s] Command too long (%u bytes), clearing FIFO", channel->name, used);
                fifo_clear(&channel->fifo);
                break;
            }
        }

        u32 cmd_len = 0;
        if (!cmd_service_find_newline(channel, CMD_SERVICE_RX_BUFFER_SIZE, &cmd_len)) {
            break;
        }

        if (cmd_len == 0) {
            u8 dummy = 0;
            fifo_read_element(&channel->fifo, &dummy, FIFO_READ_AND_CLEAN);
            continue;
        }

        u32 read_len = cmd_len;
        if (fifo_read_buffer(&channel->fifo, cmd_service_line_buffer, &read_len, FIFO_READ_AND_CLEAN) != FIFO_SUCCESS) {
            break;
        }

        u8 newline_char = 0;
        fifo_read_element(&channel->fifo, &newline_char, FIFO_READ_AND_CLEAN);

        cmd_service_line_buffer[read_len] = '\0';
        if (read_len > 0 && cmd_service_line_buffer[read_len - 1] == '\r') {
            cmd_service_line_buffer[read_len - 1] = '\0';
        }

        i32 seq = -1;
        char *payload = NULL;
        if (!cmd_service_parse_seq_envelope((char *)cmd_service_line_buffer, &seq, &payload)) {
            cmd_exec_result_t envelope_err = CMD_EXEC_CTX(EXIT_INVALID_PARAM, "invalid_seq_envelope");
            cmd_ack_emit(-1, &envelope_err);
            CMD_LOG_ERROR("[%s] Invalid seq envelope: %s", channel->name, cmd_service_line_buffer);
            continue;
        }

        cmd_exec_result_t ret = parse_command(seq, payload);
        cmd_ack_emit(seq, &ret);
        if (ret.code != EXIT_OK && ret.code != EXIT_IN_PROGRESS) {
            CMD_LOG_ERROR("[%s] Parse failed: %d", channel->name, ret.code);
        }
    }
}

static void cmd_service_poll_sources(void)
{
    u32 wireless_bytes = wireless_uart_read_buffer(cmd_service_io_buffer, CMD_SERVICE_RX_BUFFER_SIZE);
    if (wireless_bytes > 0) {
        u32 pushed = cmd_service_fifo_push_buffer(
            &cmd_service_channels[CMD_SERVICE_SOURCE_WIRELESS],
            cmd_service_io_buffer,
            wireless_bytes);
        if (pushed != wireless_bytes) {
            CMD_LOG_ERROR("[wireless] FIFO full, dropped %u bytes", (wireless_bytes - pushed));
        }
    }

    u32 debug_bytes = debug_read_ring_buffer(cmd_service_io_buffer, CMD_SERVICE_RX_BUFFER_SIZE);
    if (debug_bytes > 0) {
        u32 pushed = cmd_service_fifo_push_buffer(
            &cmd_service_channels[CMD_SERVICE_SOURCE_DEBUG],
            cmd_service_io_buffer,
            debug_bytes);
        if (pushed != debug_bytes) {
            CMD_LOG_ERROR("[debug] FIFO full, dropped %u bytes", (debug_bytes - pushed));
        }
    }
}

void cmd_service_init(void)
{
    for (u32 i = 0; i < CMD_SERVICE_SOURCE_COUNT; i++) {
        fifo_init(&cmd_service_channels[i].fifo,
                  FIFO_DATA_8BIT,
                  cmd_service_channels[i].buffer,
                  cmd_service_channels[i].buffer_size);
    }
    /* 无线串口由 sys_log_init(SYS_LOG_WIRELESS) 初始化，此处只建接收 FIFO */
}

void cmd_service_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;

    cmd_service_poll_sources();

    for (u32 i = 0; i < CMD_SERVICE_SOURCE_COUNT; i++) {
        cmd_service_process_channel(&cmd_service_channels[i]);
    }
}
