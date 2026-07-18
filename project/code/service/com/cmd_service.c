#include "cmd_service.h"
#include "common/tools/common_def.h"
#include "zf_common_fifo.h"
#include "zf_common_interrupt.h"
#include "zf_device_wifi_spi.h"
#include "parse.h"
#include "cmd_ack.h"
#include "service/sys/sys_log.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CMD_SERVICE_RX_BUFFER_SIZE           256
#define CMD_SERVICE_FIFO_BUFFER_SIZE_WIFI    256
#define CMD_SERVICE_FIFO_BUFFER_SIZE_DEBUG   256
#define CMD_SERVICE_FIFO_BUFFER_SIZE_UART3   2048

/*
 * 命令串口：MSPM0G3519 UART3（A14/A13）
 * 注意：3519 无 UART2；若需兼容原 UART2 引脚请改用 UART7
 * A14 同时为核心板 LED，尽量避免冲突
 */
#define CMD_SERVICE_UART_BAUD                115200
#define CMD_SERVICE_UART_TX_PIN              UART3_TX_A14
#define CMD_SERVICE_UART_RX_PIN              UART3_RX_A13
#define CMD_SERVICE_UART_IRQn                UART3_INT_IRQn

// 日志等级定义
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
    CMD_SERVICE_SOURCE_WIFI = 0,
    CMD_SERVICE_SOURCE_DEBUG,
    CMD_SERVICE_SOURCE_UART3,
    CMD_SERVICE_SOURCE_COUNT
} cmd_service_source_t;

typedef struct
{
    fifo_struct fifo;
    u8 *buffer;
    u32 buffer_size;
    const char *name;
    IRQn_Type rx_irqn;
    bool irq_lock_required;
} cmd_service_rx_channel_t;

static u8 cmd_service_io_buffer[CMD_SERVICE_RX_BUFFER_SIZE];
static u8 cmd_service_line_buffer[CMD_SERVICE_RX_BUFFER_SIZE];
static u8 cmd_service_peek_buffer[CMD_SERVICE_RX_BUFFER_SIZE];

static u8 cmd_service_wifi_fifo_buffer[CMD_SERVICE_FIFO_BUFFER_SIZE_WIFI];
static u8 cmd_service_debug_fifo_buffer[CMD_SERVICE_FIFO_BUFFER_SIZE_DEBUG];
static u8 cmd_service_uart3_fifo_buffer[CMD_SERVICE_FIFO_BUFFER_SIZE_UART3];
static volatile u32 g_cmd_service_uart3_drop_bytes = 0;

static cmd_service_rx_channel_t cmd_service_channels[CMD_SERVICE_SOURCE_COUNT] = {
    {
        .buffer = cmd_service_wifi_fifo_buffer,
        .buffer_size = CMD_SERVICE_FIFO_BUFFER_SIZE_WIFI,
        .name = "wifi",
        .rx_irqn = UART0_INT_IRQn,    /* placeholder, irq_lock_required=false */
        .irq_lock_required = false
    },
    {
        .buffer = cmd_service_debug_fifo_buffer,
        .buffer_size = CMD_SERVICE_FIFO_BUFFER_SIZE_DEBUG,
        .name = "debug",
        .rx_irqn = UART0_INT_IRQn,    /* placeholder, irq_lock_required=false */
        .irq_lock_required = false
    },
    {
        .buffer = cmd_service_uart3_fifo_buffer,
        .buffer_size = CMD_SERVICE_FIFO_BUFFER_SIZE_UART3,
        .name = "uart3",
        .rx_irqn = CMD_SERVICE_UART_IRQn,
        .irq_lock_required = true
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

static bool cmd_service_channel_lock(cmd_service_rx_channel_t *channel)
{
    if (channel == NULL || !channel->irq_lock_required) {
        return false;
    }
    interrupt_disable(channel->rx_irqn);
    return true;
}

static void cmd_service_channel_unlock(cmd_service_rx_channel_t *channel, bool locked)
{
    if (channel == NULL || !locked) {
        return;
    }
    interrupt_enable(channel->rx_irqn);
}

static void cmd_service_process_channel(cmd_service_rx_channel_t *channel)
{
    if (channel == NULL) {
        return;
    }

    while (1) {
        bool locked = cmd_service_channel_lock(channel);
        u32 used = fifo_used(&channel->fifo);
        if (used == 0) {
            cmd_service_channel_unlock(channel, locked);
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
                cmd_service_channel_unlock(channel, locked);
                break;
            }
        }

        u32 cmd_len = 0;
        if (!cmd_service_find_newline(channel, CMD_SERVICE_RX_BUFFER_SIZE, &cmd_len)) {
            cmd_service_channel_unlock(channel, locked);
            break;
        }

        if (cmd_len == 0) {
            u8 dummy = 0;
            fifo_read_element(&channel->fifo, &dummy, FIFO_READ_AND_CLEAN);
            cmd_service_channel_unlock(channel, locked);
            continue;
        }

        u32 read_len = cmd_len;
        if (fifo_read_buffer(&channel->fifo, cmd_service_line_buffer, &read_len, FIFO_READ_AND_CLEAN) != FIFO_SUCCESS) {
            cmd_service_channel_unlock(channel, locked);
            break;
        }

        u8 newline_char = 0;
        fifo_read_element(&channel->fifo, &newline_char, FIFO_READ_AND_CLEAN);
        cmd_service_channel_unlock(channel, locked);

        cmd_service_line_buffer[read_len] = '\0';
        if (read_len > 0 && cmd_service_line_buffer[read_len - 1] == '\r') {
            cmd_service_line_buffer[read_len - 1] = '\0';
        }

        if (channel == &cmd_service_channels[CMD_SERVICE_SOURCE_UART3]) {
            sys_log_text(UART3, "%s", cmd_service_line_buffer);
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
    u32 wifi_bytes = wifi_spi_read_buffer(cmd_service_io_buffer, CMD_SERVICE_RX_BUFFER_SIZE);
    if (wifi_bytes > 0) {
        u32 pushed = cmd_service_fifo_push_buffer(&cmd_service_channels[CMD_SERVICE_SOURCE_WIFI], cmd_service_io_buffer, wifi_bytes);
        if (pushed != wifi_bytes) {
            CMD_LOG_ERROR("[wifi] FIFO full, dropped %u bytes", (wifi_bytes - pushed));
        }
    }

    u32 debug_bytes = debug_read_ring_buffer(cmd_service_io_buffer, CMD_SERVICE_RX_BUFFER_SIZE);
    if (debug_bytes > 0) {
        u32 pushed = cmd_service_fifo_push_buffer(&cmd_service_channels[CMD_SERVICE_SOURCE_DEBUG], cmd_service_io_buffer, debug_bytes);
        if (pushed != debug_bytes) {
            CMD_LOG_ERROR("[debug] FIFO full, dropped %u bytes", (debug_bytes - pushed));
        }
    }
}

/* UART3 RX 回调：适配 zf uart_set_callback 签名 */
static void cmd_service_uart3_callback(uint32 event, void *ptr)
{
    (void)ptr;
    if (event == UART_INTERRUPT_STATE_RX) {
        cmd_service_uart_rx_irq_handler(UART_3);
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

    uart_init(UART_3, CMD_SERVICE_UART_BAUD, CMD_SERVICE_UART_TX_PIN, CMD_SERVICE_UART_RX_PIN);
    uart_set_callback(UART_3, cmd_service_uart3_callback, NULL);
    uart_set_interrupt_config(UART_3, UART_INTERRUPT_CONFIG_RX_ENABLE);
}

void cmd_service_uart_rx_irq_handler(uart_index_enum uart_n)
{
    cmd_service_rx_channel_t *channel = NULL;
    if (uart_n == UART_3) {
        channel = &cmd_service_channels[CMD_SERVICE_SOURCE_UART3];
    } else {
        return;
    }

    u8 byte = 0;
    while (uart_query_byte(uart_n, &byte)) {
        if (!cmd_service_fifo_push_byte(channel, byte)) {
            g_cmd_service_uart3_drop_bytes++;
        }
    }
}

void cmd_service_task(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;

    interrupt_disable(CMD_SERVICE_UART_IRQn);
    u32 uart3_drop_bytes = g_cmd_service_uart3_drop_bytes;
    g_cmd_service_uart3_drop_bytes = 0;
    interrupt_enable(CMD_SERVICE_UART_IRQn);
    if (uart3_drop_bytes > 0) {
        sys_log_text(terminal,
                     "{cmd_service_evt} source=uart3 state=fifo_overflow dropped=%lu",
                     (unsigned long)uart3_drop_bytes);
    }

    cmd_service_poll_sources();

    for (u32 i = 0; i < CMD_SERVICE_SOURCE_COUNT; i++) {
        cmd_service_process_channel(&cmd_service_channels[i]);
    }
}

