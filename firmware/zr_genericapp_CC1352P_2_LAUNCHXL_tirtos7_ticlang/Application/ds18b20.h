/*
 * ds18b20.h
 *
 * Minimal 1-Wire master (bit-banged GPIO) and DS18B20 driver for the CC26x2.
 *
 * Wiring: DATA to the 1-Wire DIO with an external 4.7 kOhm pull-up to 3.3 V,
 * sensors powered from VDD (parasite power is not supported).
 *
 * Every function blocks for a few ms and must be called from task context.
 * Interrupts are disabled only inside a single bit slot (< 80 us).
 */
#ifndef DS18B20_H
#define DS18B20_H

#include <stdbool.h>
#include <stdint.h>

#define DS18B20_ROM_LEN         8
#define DS18B20_FAMILY_CODE     0x28
#define DS18B20_CONVERT_TIME_MS 750   /* 12-bit resolution */
#define DS18B20_INVALID_TEMP    ((int16_t)0x8000)   /* ZCL "invalid" for int16 */

/* Bind the driver to a DIO (configured as input by SysConfig; external 4.7 kOhm pull-up). */
void ds18b20_init(uint8_t dio);

/* Find up to maxCount DS18B20 sensors on the bus. Returns the number found. */
uint8_t ds18b20_search(uint8_t roms[][DS18B20_ROM_LEN], uint8_t maxCount);

/* Set 12-bit resolution (0.0625 degC) in the sensor RAM. Returns false on bus/CRC error. */
bool ds18b20_setMaxResolution(const uint8_t rom[DS18B20_ROM_LEN]);

/* Start a temperature conversion on every sensor on the bus (SKIP ROM + CONVERT T). */
bool ds18b20_startConversionAll(void);

/* Read the last conversion of one sensor, in 0.01 degC. Returns false on CRC/bus error. */
bool ds18b20_readTemperature(const uint8_t rom[DS18B20_ROM_LEN], int16_t *centiDegC);

#endif /* DS18B20_H */
