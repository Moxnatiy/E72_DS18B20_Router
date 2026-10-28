/*
 * ds18b20.c
 *
 * Minimal 1-Wire master (bit-banged GPIO) and DS18B20 driver, see ds18b20.h.
 * Timing follows Maxim AN126 "standard speed"; ROM search follows AN187.
 */
#include "ds18b20.h"

#include <string.h>

#include <ti/drivers/dpl/HwiP.h>
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/gpio.h)
#include DeviceFamily_constructPath(driverlib/cpu.h)

#define OW_CMD_SEARCH_ROM   0xF0
#define OW_CMD_MATCH_ROM    0x55
#define OW_CMD_SKIP_ROM     0xCC
#define DS_CMD_CONVERT_T    0x44
#define DS_CMD_READ_SCRATCH 0xBE
#define DS_CMD_WRITE_SCRATCH 0x4E
#define DS_CONFIG_12BIT     0x7F    /* R1 = R0 = 1: 12-bit, 0.0625 degC, 750 ms */

/* CPUdelay() burns 3 cycles per loop: 16 loops per us at 48 MHz */
#define DELAY_US(us)        CPUdelay((us) * 16)

static uint8_t owDio = 0xFF;

/* Drive the line low (output enable with DOUT = 0) or release it to the pull-up. */
static inline void owLow(void)     { GPIO_setOutputEnableDio(owDio, GPIO_OUTPUT_ENABLE); }
static inline void owRelease(void) { GPIO_setOutputEnableDio(owDio, GPIO_OUTPUT_DISABLE); }
static inline uint32_t owRead(void) { return GPIO_readDio(owDio); }

static bool owReset(void)
{
    uintptr_t key;
    bool presence;

    owLow();
    DELAY_US(480);

    key = HwiP_disable();
    owRelease();
    DELAY_US(70);
    presence = (owRead() == 0);
    HwiP_restore(key);

    DELAY_US(410);
    return presence;
}

static void owWriteBit(uint8_t bit)
{
    uintptr_t key = HwiP_disable();

    owLow();
    if (bit)
    {
        DELAY_US(6);
        owRelease();
        DELAY_US(64);
    }
    else
    {
        DELAY_US(60);
        owRelease();
        DELAY_US(10);
    }
    HwiP_restore(key);
}

static uint8_t owReadBit(void)
{
    uintptr_t key = HwiP_disable();
    uint8_t bit;

    owLow();
    DELAY_US(6);
    owRelease();
    DELAY_US(9);
    bit = (owRead() != 0);
    HwiP_restore(key);

    DELAY_US(55);
    return bit;
}

static void owWriteByte(uint8_t value)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        owWriteBit(value & 0x01);
        value >>= 1;
    }
}

static uint8_t owReadByte(void)
{
    uint8_t value = 0;

    for (uint8_t i = 0; i < 8; i++)
    {
        value |= (uint8_t)(owReadBit() << i);
    }
    return value;
}

/* Dallas/Maxim CRC8, polynomial x^8 + x^5 + x^4 + 1 */
static uint8_t owCrc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;

    while (len--)
    {
        uint8_t in = *data++;
        for (uint8_t i = 0; i < 8; i++)
        {
            uint8_t mix = (crc ^ in) & 0x01;
            crc >>= 1;
            if (mix)
            {
                crc ^= 0x8C;
            }
            in >>= 1;
        }
    }
    return crc;
}

void ds18b20_init(uint8_t dio)
{
    owDio = dio;
    /* DOUT stays 0: the pin only ever drives low, the pull-up gives the high level */
    GPIO_clearDio(owDio);
    owRelease();
}

uint8_t ds18b20_search(uint8_t roms[][DS18B20_ROM_LEN], uint8_t maxCount)
{
    uint8_t rom[DS18B20_ROM_LEN] = {0};
    int8_t lastDiscrepancy = -1;
    bool lastDevice = false;
    uint8_t found = 0;
    uint8_t passes = 0;

    /* Each pass finds one ROM code; the pass limit is a second guard against a noisy bus */
    while (!lastDevice && found < maxCount && passes++ < 2 * maxCount)
    {
        int8_t lastZero = -1;

        if (!owReset())
        {
            break;  /* no device on the bus */
        }
        owWriteByte(OW_CMD_SEARCH_ROM);

        for (uint8_t bitIndex = 0; bitIndex < 64; bitIndex++)
        {
            uint8_t idBit = owReadBit();
            uint8_t cmpBit = owReadBit();
            uint8_t *byte = &rom[bitIndex / 8];
            uint8_t mask = (uint8_t)(1 << (bitIndex % 8));
            uint8_t direction;

            if (idBit && cmpBit)
            {
                return found;  /* bus error, nobody answered this bit */
            }

            if (idBit != cmpBit)
            {
                direction = idBit;
            }
            else
            {
                /* Discrepancy: both 0 and 1 present at this bit position */
                if (bitIndex < lastDiscrepancy)
                {
                    direction = ((*byte & mask) != 0);
                }
                else
                {
                    direction = (bitIndex == lastDiscrepancy);
                }
                if (direction == 0)
                {
                    lastZero = (int8_t)bitIndex;
                }
            }

            if (direction)
            {
                *byte |= mask;
            }
            else
            {
                *byte &= (uint8_t)~mask;
            }
            owWriteBit(direction);
        }

        lastDiscrepancy = lastZero;
        lastDevice = (lastDiscrepancy < 0);

        if (owCrc8(rom, 7) != rom[7])
        {
            /* Noise, not a device (e.g. floating line with no sensor and no pull-up):
             * stop here, otherwise the search walks all 2^64 ROM codes */
            break;
        }
        if (rom[0] == DS18B20_FAMILY_CODE)
        {
            memcpy(roms[found++], rom, DS18B20_ROM_LEN);
        }
    }

    return found;
}

bool ds18b20_startConversionAll(void)
{
    if (!owReset())
    {
        return false;
    }
    owWriteByte(OW_CMD_SKIP_ROM);
    owWriteByte(DS_CMD_CONVERT_T);
    return true;
}

static bool owSelect(const uint8_t rom[DS18B20_ROM_LEN])
{
    if (!owReset())
    {
        return false;
    }
    owWriteByte(OW_CMD_MATCH_ROM);
    for (uint8_t i = 0; i < DS18B20_ROM_LEN; i++)
    {
        owWriteByte(rom[i]);
    }
    return true;
}

static bool dsReadScratchpad(const uint8_t rom[DS18B20_ROM_LEN], uint8_t scratch[9])
{
    if (!owSelect(rom))
    {
        return false;
    }
    owWriteByte(DS_CMD_READ_SCRATCH);
    for (uint8_t i = 0; i < 9; i++)
    {
        scratch[i] = owReadByte();
    }
    return (owCrc8(scratch, 8) == scratch[8]);
}

bool ds18b20_setMaxResolution(const uint8_t rom[DS18B20_ROM_LEN])
{
    uint8_t scratch[9];

    if (!dsReadScratchpad(rom, scratch))
    {
        return false;
    }
    if (scratch[4] == DS_CONFIG_12BIT)
    {
        return true;
    }

    /* Keep the alarm bytes TH/TL, only change the config register (RAM only, no EEPROM wear) */
    if (!owSelect(rom))
    {
        return false;
    }
    owWriteByte(DS_CMD_WRITE_SCRATCH);
    owWriteByte(scratch[2]);
    owWriteByte(scratch[3]);
    owWriteByte(DS_CONFIG_12BIT);

    return dsReadScratchpad(rom, scratch) && scratch[4] == DS_CONFIG_12BIT;
}

bool ds18b20_readTemperature(const uint8_t rom[DS18B20_ROM_LEN], int16_t *centiDegC)
{
    uint8_t scratch[9];

    /* One retry: a single corrupted bit on a long cable is common */
    if (!dsReadScratchpad(rom, scratch) && !dsReadScratchpad(rom, scratch))
    {
        return false;
    }

    /* 1/16 degC per LSB -> 0.01 degC */
    int16_t raw = (int16_t)((scratch[1] << 8) | scratch[0]);
    *centiDegC = (int16_t)(((int32_t)raw * 100) / 16);
    return true;
}
