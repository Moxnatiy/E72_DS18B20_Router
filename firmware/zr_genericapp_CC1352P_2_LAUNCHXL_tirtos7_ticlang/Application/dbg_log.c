/*
 * dbg_log.c
 *
 * Debug log over UART0, see dbg_log.h.
 */
#include "dbg_log.h"

#if E72_DEBUG_LOG

#include <stdarg.h>
#include <stdio.h>

#include <ti/drivers/UART2.h>
#include <ti/drivers/dpl/ClockP.h>

#include "ti_drivers_config.h"

#define DBG_LOG_LINE_MAX 160

static UART2_Handle dbgUart = NULL;
static char dbgLine[DBG_LOG_LINE_MAX];

bool dbgLog_init(void)
{
    UART2_Params params;

    UART2_Params_init(&params);
    params.baudRate = 115200;
    params.writeMode = UART2_Mode_BLOCKING;
    params.readMode = UART2_Mode_BLOCKING;

    dbgUart = UART2_open(CONFIG_DISPLAY_UART, &params);
    return (dbgUart != NULL);
}

void dbgLog_printf(const char *fmt, ...)
{
    va_list args;
    int len;

    if (dbgUart == NULL)
    {
        return;
    }

    va_start(args, fmt);
    len = vsnprintf(dbgLine, sizeof(dbgLine), fmt, args);
    va_end(args);

    if (len > 0)
    {
        if (len >= (int)sizeof(dbgLine))
        {
            len = sizeof(dbgLine) - 1;
        }
        UART2_write(dbgUart, dbgLine, (size_t)len, NULL);
    }
}

uint32_t dbgLog_ms(void)
{
    return (uint32_t)(((uint64_t)ClockP_getSystemTicks() * ClockP_getSystemTickPeriod()) / 1000);
}

const char *dbgLog_devStateName(uint8_t state)
{
    /* Order follows zstack_DevState in zstack.h */
    static const char *const names[] = {
        "HOLD", "INIT", "NWK_DISC", "NWK_JOINING", "NWK_REJOIN_SEC_CURR_CH",
        "END_DEVICE_UNAUTH", "END_DEVICE", "ROUTER", "COORD_STARTING", "ZB_COORD",
        "NWK_ORPHAN", "NWK_KA", "NWK_BACKOFF", "NWK_REJOIN_SEC_ALL_CH",
        "NWK_TC_REJOIN_CURR_CH", "NWK_TC_REJOIN_ALL_CH"
    };

    return (state < sizeof(names) / sizeof(names[0])) ? names[state] : "?";
}

#endif /* E72_DEBUG_LOG */
