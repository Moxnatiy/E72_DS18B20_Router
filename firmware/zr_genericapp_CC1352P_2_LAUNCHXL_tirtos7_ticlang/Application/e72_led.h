/*
 * e72_led.h
 *
 * egony stick LEDs:
 *   LED1 (DIO8, green) network: fast blink = searching/joining,
 *                               slow blink = not on a network (waiting to retry),
 *                               off + one flash per report sent = joined as router
 *   LED2 (DIO7) activity: short flash on every DS18B20 read cycle
 */
#ifndef E72_LED_H
#define E72_LED_H

#include <stdint.h>

void e72Led_init(void);

/* state is a zstack_DevState value */
void e72Led_netState(uint8_t state);

void e72Led_activity(void);

/* One flash on the network LED: a temperature report was sent */
void e72Led_reportSent(void);

/* Activity LED on steadily: debug UART failed to open */
void e72Led_error(void);

#endif /* E72_LED_H */
