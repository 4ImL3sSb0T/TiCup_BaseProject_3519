#include "sys_time.h"
#include <stdint.h>

volatile uint32_t sys_time_ms = 0;

uint32_t sys_time_get_ms(void) {
	return sys_time_ms;
}

// MSPM0G3519 (Cortex-M0+) does NOT have DWT cycle counter.
// DWT is only available on Cortex-M3/M4/M7.
// Provide empty stub for compatibility.
void dwt_init(void) {
	// NOP on Cortex-M0+
}