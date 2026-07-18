#include "sys_log.h"
#include "zf_device_wifi_spi.h"
#include "zf_device_wireless_uart.h"
#include "zf_common_debug.h"
#include "zf_driver_uart.h"
#include "zf_driver_delay.h"
#include <stdarg.h>
#include <stdio.h>

sys_log_type_e current_log_type = SYS_LOG_WIRELESS;

void sys_log_init(sys_log_type_e log_type)
{
    current_log_type = log_type;
    if (current_log_type != SYS_LOG_WIFI
        && current_log_type != SYS_LOG_UART
        && current_log_type != SYS_LOG_WIRELESS) {
        current_log_type = SYS_LOG_WIRELESS;
    }

    if (current_log_type == SYS_LOG_WIFI) {
        uint8_t try_count = 0;
        while (wifi_spi_init("TX5PRO", "0d000721")) {
            printf("\r\n connect wifi failed. \r\n");
            try_count++;
            if (try_count >= 5) {
                current_log_type = SYS_LOG_UART;
                printf("\r\n wifi init failed after 5 tries. \r\n");
                printf("System log redirecting to UART.\r\n");
                break;
            }
            system_delay_ms(100);
        }
        if (current_log_type == SYS_LOG_WIFI) {
            wifi_spi_socket_connect(
                "TCP",
                WIFI_SPI_TARGET_IP,
                WIFI_SPI_TARGET_PORT,
                WIFI_SPI_LOCAL_PORT);
            sys_log_text(info, "module version:%s", wifi_spi_version);
            sys_log_text(info, "module mac    :%s", wifi_spi_mac_addr);
            sys_log_text(info, "module ip     :%s", wifi_spi_ip_addr_port);
            sys_log_text(info, "wifi debug init success.");
        }
    } else if (current_log_type == SYS_LOG_WIRELESS) {
        /* 失败则回退 Debug UART，保证仍能看到启动日志 */
        if (wireless_uart_init() != 0) {
            current_log_type = SYS_LOG_UART;
            printf("\r\n wireless_uart init failed, log -> UART0.\r\n");
            sys_log_text(info, "UART log initialized (wireless fallback).");
        } else {
            sys_log_text(info, "wireless uart log init ok (UART1 B6/B7, RTS=B2).");
        }
    } else {
        sys_log_text(info, "UART log initialized.");
    }
}

void sys_log_send_data(const uint8_t *data, uint32_t len)
{
    switch (current_log_type) {
    case SYS_LOG_WIFI:
        wifi_spi_send_buffer(data, len);
        break;
    case SYS_LOG_WIRELESS:
        wireless_uart_send_buffer(data, len);
        break;
    case SYS_LOG_UART:
        uart_write_buffer(DEBUG_UART_INDEX, data, len);
        break;
    default:
        break;
    }
}

void sys_log_printf(const char *fmt, ...)
{
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    switch (current_log_type) {
    case SYS_LOG_WIFI:
        wifi_spi_send_string(buffer);
        break;
    case SYS_LOG_WIRELESS:
        wireless_uart_send_string(buffer);
        break;
    case SYS_LOG_UART:
        uart_write_string(DEBUG_UART_INDEX, buffer);
        break;
    default:
        break;
    }
}
