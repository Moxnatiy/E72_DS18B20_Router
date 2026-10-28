/*
 * dbg_log.h
 *
 * Debug log over UART0 (DIO13 TX / DIO12 RX, 115200 8N1), written directly with the
 * UART2 driver. On the egony stick this is the CH340 port that is also used by the
 * ROM bootloader.
 *
 * DBG_LOG() blocks until the line is queued: call it from task context only, never
 * from a HWI/SWI or a ClockP callback. Build with -DE72_DEBUG_LOG=0 to compile it out.
 */
#ifndef DBG_LOG_H
#define DBG_LOG_H

#include <stdbool.h>
#include <stdint.h>

#ifndef E72_DEBUG_LOG
#define E72_DEBUG_LOG 1
#endif

#if E72_DEBUG_LOG

/* Returns false if the UART could not be opened. */
bool dbgLog_init(void);
void dbgLog_printf(const char *fmt, ...);
uint32_t dbgLog_ms(void);
const char *dbgLog_devStateName(uint8_t state);

#define DBG_LOG(fmt, ...) dbgLog_printf("[%7lu] " fmt "\r\n", (unsigned long)dbgLog_ms(), ##__VA_ARGS__)

#else

#define dbgLog_init()               (true)
#define dbgLog_devStateName(state)  ""
#define DBG_LOG(fmt, ...)           do { } while (0)

#endif /* E72_DEBUG_LOG */

#endif /* DBG_LOG_H */
