/*
 * e72_led.c
 *
 * egony stick LEDs, see e72_led.h.
 */
#include "e72_led.h"

#include <ti/drivers/apps/LED.h>

#include "zstack.h"
#include "ti_drivers_config.h"

#define E72LED_SEARCH_BLINK_MS  250
#define E72LED_IDLE_BLINK_MS    1000
#define E72LED_REPORT_MS        200   /* LED driver toggles every period: 200 ms on */
#define E72LED_ACTIVITY_MS      200   /* LED driver toggles every period: 200 ms on */

static LED_Handle ledNet;
static LED_Handle ledAct;
static bool netJoined;

void e72Led_init(void)
{
    LED_Params params;

    LED_init();
    LED_Params_init(&params);
    ledNet = LED_open(CONFIG_LED_NET, &params);
    ledAct = LED_open(CONFIG_LED_ACT, &params);

    /* Searching until the stack reports otherwise */
    e72Led_netState(zstack_DevState_NWK_DISC);
}

void e72Led_netState(uint8_t state)
{
    if (ledNet == NULL)
    {
        return;
    }

    LED_stopBlinking(ledNet);
    netJoined = (state == zstack_DevState_DEV_ROUTER);

    switch (state)
    {
        case zstack_DevState_DEV_ROUTER:
            /* Joined: dark, one flash per report sent (e72Led_reportSent) */
            LED_setOff(ledNet);
            break;

        case zstack_DevState_HOLD:
            /* Not on a network, waiting for the next steering retry */
            LED_startBlinking(ledNet, E72LED_IDLE_BLINK_MS, LED_BLINK_FOREVER);
            break;

        default:
            LED_startBlinking(ledNet, E72LED_SEARCH_BLINK_MS, LED_BLINK_FOREVER);
            break;
    }
}

void e72Led_reportSent(void)
{
    if (ledNet != NULL && netJoined)
    {
        LED_startBlinking(ledNet, E72LED_REPORT_MS, 1);
    }
}

void e72Led_error(void)
{
    if (ledAct != NULL)
    {
        LED_setOn(ledAct, LED_BRIGHTNESS_MAX);
    }
}

void e72Led_activity(void)
{
    if (ledAct != NULL)
    {
        LED_startBlinking(ledAct, E72LED_ACTIVITY_MS, 1);
    }
}
