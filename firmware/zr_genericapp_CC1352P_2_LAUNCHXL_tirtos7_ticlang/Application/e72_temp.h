/*
 * e72_temp.h
 *
 * DS18B20 sensors exposed as Zigbee Temperature Measurement (0x0402) endpoints.
 * One endpoint per sensor found on the 1-Wire bus at boot, starting at
 * E72TEMP_FIRST_ENDPOINT, ordered by ROM code so the mapping stays stable.
 */
#ifndef E72_TEMP_H
#define E72_TEMP_H

#include <stdbool.h>
#include <stdint.h>

#define E72TEMP_MAX_SENSORS     16      /* endpoints 10..25 */
#define E72TEMP_FIRST_ENDPOINT  10
#define E72TEMP_PERIOD_MS       10000   /* measurement period */

/* App events used by this module (free bits of zcl_genericapp.h) */
#define E72TEMP_CONVERT_EVT     0x0008  /* GENERICAPP_EVT_1 */
#define E72TEMP_READ_EVT        0x0010  /* GENERICAPP_EVT_2 */

/*
 * Scan the bus and register one endpoint per sensor.
 * Call from the app task before BDB commissioning starts.
 */
void e72Temp_init(uint8_t taskId, uint32_t *pEvents, void *semHandle);

/* Reports are only sent while the router is on a network. */
void e72Temp_setOnline(bool isOnline);

/* Handle E72TEMP_CONVERT_EVT / E72TEMP_READ_EVT; clears the handled bits. */
void e72Temp_processEvents(uint32_t *pEvents);

#endif /* E72_TEMP_H */
