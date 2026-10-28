/*
 * e72_temp.c
 *
 * DS18B20 sensors exposed as Zigbee Temperature Measurement endpoints, see e72_temp.h.
 */
#include "e72_temp.h"

#include <string.h>

#include "zcl.h"
#include "zcl_general.h"
#include "zcl_ha.h"
#include "zcl_ms.h"
#include "zcl_port.h"
#include "zstackapi.h"
#include "util_timer.h"

#include <ti/sysbios/knl/Semaphore.h>

#include "ti_drivers_config.h"
#include "ds18b20.h"
#include "dbg_log.h"
#include "e72_led.h"

#define E72TEMP_MIN_VALUE       (-5500)   /* DS18B20 range, 0.01 degC */
#define E72TEMP_MAX_VALUE       12500
#define E72TEMP_POWER_ON_VALUE  8500      /* scratchpad default before first conversion */
#define E72TEMP_REPORT_MAX_MS   60000     /* report at least once a minute */
#define E72TEMP_REPORT_CHANGE   10        /* ... or on a change of 0.1 degC */
#define E72TEMP_COORD_ENDPOINT  1         /* Zigbee2MQTT/ZHA coordinator endpoint */

#define E72TEMP_NUM_ATTRS       4

typedef struct
{
    uint8_t rom[DS18B20_ROM_LEN];
    int16_t measuredValue;
    int16_t reportedValue;
    uint32_t reportedMs;
    bool reported;
    uint8_t errors;
    endPointDesc_t epDesc;
    SimpleDescriptionFormat_t simpleDesc;
    zclAttrRec_t attrs[E72TEMP_NUM_ATTRS];
} e72TempSensor_t;

static const cId_t e72Temp_inClusters[] = { ZCL_CLUSTER_ID_MS_TEMPERATURE_MEASUREMENT };
static const int16_t e72Temp_minValue = E72TEMP_MIN_VALUE;
static const int16_t e72Temp_maxValue = E72TEMP_MAX_VALUE;
static const uint16_t e72Temp_clusterRevision = 0x0001;

static e72TempSensor_t sensors[E72TEMP_MAX_SENSORS];
static uint8_t sensorCount;

static uint8_t appTaskId;
static bool online;
static uint8_t reportSeqNum;
static uint32_t *pAppEvents;
static Semaphore_Handle appSem;

static ClockP_Struct convertClkStruct;
static ClockP_Struct readClkStruct;
static ClockP_Handle readClkHandle;

static void e72Temp_convertClockCb(UArg a0)
{
    (void)a0;
    *pAppEvents |= E72TEMP_CONVERT_EVT;
    Semaphore_post(appSem);
}

static void e72Temp_readClockCb(UArg a0)
{
    (void)a0;
    *pAppEvents |= E72TEMP_READ_EVT;
    Semaphore_post(appSem);
}

static void e72Temp_setAttr(zclAttrRec_t *rec, uint16_t attrId, uint8_t dataType,
                            uint8_t access, void *data)
{
    rec->clusterID = ZCL_CLUSTER_ID_MS_TEMPERATURE_MEASUREMENT;
    rec->attr.attrId = attrId;
    rec->attr.dataType = dataType;
    rec->attr.accessControl = access;
    rec->attr.dataPtr = data;
}

static void e72Temp_registerEndpoint(e72TempSensor_t *s, uint8_t endpoint)
{
    s->measuredValue = DS18B20_INVALID_TEMP;
    s->reported = false;

    s->simpleDesc.EndPoint = endpoint;
    s->simpleDesc.AppProfId = ZCL_HA_PROFILE_ID;
    s->simpleDesc.AppDeviceId = ZCL_DEVICEID_TEMPERATURE_SENSOR;
    s->simpleDesc.AppDevVer = 0;
    s->simpleDesc.Reserved = 0;
    s->simpleDesc.AppNumInClusters = 1;
    s->simpleDesc.pAppInClusterList = (cId_t *)e72Temp_inClusters;
    s->simpleDesc.AppNumOutClusters = 0;
    s->simpleDesc.pAppOutClusterList = NULL;

    s->epDesc.endPoint = endpoint;
    s->epDesc.simpleDesc = &s->simpleDesc;
    zclport_registerEndpoint(appTaskId, &s->epDesc);

    e72Temp_setAttr(&s->attrs[0], ATTRID_TEMPERATURE_MEASUREMENT_MEASURED_VALUE, ZCL_DATATYPE_INT16,
                    ACCESS_CONTROL_READ | ACCESS_CONTROL_AUTH_READ, &s->measuredValue);
    e72Temp_setAttr(&s->attrs[1], ATTRID_TEMPERATURE_MEASUREMENT_MIN_MEASURED_VALUE, ZCL_DATATYPE_INT16,
                    ACCESS_CONTROL_READ | ACCESS_CONTROL_AUTH_READ, (void *)&e72Temp_minValue);
    e72Temp_setAttr(&s->attrs[2], ATTRID_TEMPERATURE_MEASUREMENT_MAX_MEASURED_VALUE, ZCL_DATATYPE_INT16,
                    ACCESS_CONTROL_READ | ACCESS_CONTROL_AUTH_READ, (void *)&e72Temp_maxValue);
    e72Temp_setAttr(&s->attrs[3], ATTRID_CLUSTER_REVISION, ZCL_DATATYPE_UINT16,
                    ACCESS_CONTROL_READ | ACCESS_CONTROL_AUTH_READ, (void *)&e72Temp_clusterRevision);
    zcl_registerAttrList(endpoint, E72TEMP_NUM_ATTRS, s->attrs);
}

/*
 * Reports are sent by the app (not BDB reporting) so the LED can show each one:
 * on a change of E72TEMP_REPORT_CHANGE or at least every E72TEMP_REPORT_MAX_MS,
 * straight to the coordinator.
 */
static void e72Temp_report(e72TempSensor_t *s)
{
    uint8_t buf[sizeof(zclReportCmd_t) + sizeof(zclReport_t)];
    zclReportCmd_t *cmd = (zclReportCmd_t *)buf;
    afAddrType_t dst;
    uint32_t now = dbgLog_ms();
    int16_t delta = (int16_t)(s->measuredValue - s->reportedValue);

    if (!online)
    {
        return;
    }
    if (s->reported && (delta < E72TEMP_REPORT_CHANGE && delta > -E72TEMP_REPORT_CHANGE) &&
        (now - s->reportedMs) < E72TEMP_REPORT_MAX_MS)
    {
        return;
    }

    dst.addrMode = (afAddrMode_t)Addr16Bit;
    dst.addr.shortAddr = 0x0000;
    dst.endPoint = E72TEMP_COORD_ENDPOINT;
    dst.panId = 0;

    cmd->numAttr = 1;
    cmd->attrList[0].attrID = ATTRID_TEMPERATURE_MEASUREMENT_MEASURED_VALUE;
    cmd->attrList[0].dataType = ZCL_DATATYPE_INT16;
    cmd->attrList[0].attrData = (uint8_t *)&s->measuredValue;

    if (zcl_SendReportCmd(s->epDesc.endPoint, &dst, ZCL_CLUSTER_ID_MS_TEMPERATURE_MEASUREMENT,
                          cmd, ZCL_FRAME_SERVER_CLIENT_DIR, TRUE, reportSeqNum++) == ZSuccess)
    {
        s->reported = true;
        s->reportedValue = s->measuredValue;
        s->reportedMs = now;
        DBG_LOG("ep %u: report sent", s->epDesc.endPoint);
        e72Led_reportSent();
    }
    else
    {
        DBG_LOG("ep %u: report send failed", s->epDesc.endPoint);
    }
}

void e72Temp_setOnline(bool isOnline)
{
    online = isOnline;
    if (online)
    {
        /* Send fresh values right after (re)joining */
        for (uint8_t i = 0; i < sensorCount; i++)
        {
            sensors[i].reported = false;
        }
    }
}

void e72Temp_init(uint8_t taskId, uint32_t *pEvents, void *semHandle)
{
    uint8_t roms[E72TEMP_MAX_SENSORS][DS18B20_ROM_LEN];

    appTaskId = taskId;
    pAppEvents = pEvents;
    appSem = (Semaphore_Handle)semHandle;

    ds18b20_init(CONFIG_GPIO_ONEWIRE);
    sensorCount = ds18b20_search(roms, E72TEMP_MAX_SENSORS);
    DBG_LOG("1-Wire DIO%u: %u DS18B20 found", CONFIG_GPIO_ONEWIRE, sensorCount);

    /* Sort by ROM code so endpoint numbers do not depend on search order */
    for (uint8_t i = 1; i < sensorCount; i++)
    {
        for (uint8_t j = i; j > 0 && memcmp(roms[j - 1], roms[j], DS18B20_ROM_LEN) > 0; j--)
        {
            uint8_t tmp[DS18B20_ROM_LEN];
            memcpy(tmp, roms[j], DS18B20_ROM_LEN);
            memcpy(roms[j], roms[j - 1], DS18B20_ROM_LEN);
            memcpy(roms[j - 1], tmp, DS18B20_ROM_LEN);
        }
    }

    for (uint8_t i = 0; i < sensorCount; i++)
    {
        e72TempSensor_t *s = &sensors[i];
        const uint8_t *r = roms[i];

        memcpy(s->rom, r, DS18B20_ROM_LEN);
        e72Temp_registerEndpoint(s, (uint8_t)(E72TEMP_FIRST_ENDPOINT + i));
        DBG_LOG("  ep %u: %02x%02x%02x%02x%02x%02x%02x%02x, 12-bit %s", E72TEMP_FIRST_ENDPOINT + i,
                r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
                ds18b20_setMaxResolution(r) ? "ok" : "FAILED");
    }

    if (sensorCount == 0)
    {
        return;
    }

    UtilTimer_construct(&convertClkStruct, e72Temp_convertClockCb,
                        1000, E72TEMP_PERIOD_MS, true, 0);
    readClkHandle = UtilTimer_construct(&readClkStruct, e72Temp_readClockCb,
                                        DS18B20_CONVERT_TIME_MS + 50, 0, false, 0);
    (void)readClkHandle;
}

static void e72Temp_readAll(void)
{
    e72Led_activity();

    for (uint8_t i = 0; i < sensorCount; i++)
    {
        e72TempSensor_t *s = &sensors[i];
        uint8_t endpoint = s->epDesc.endPoint;
        int16_t value;

        if (!ds18b20_readTemperature(s->rom, &value))
        {
            if (++s->errors >= 3 && s->measuredValue != DS18B20_INVALID_TEMP)
            {
                s->measuredValue = DS18B20_INVALID_TEMP;
                DBG_LOG("ep %u: sensor lost", endpoint);
            }
            else
            {
                DBG_LOG("ep %u: read error %u", endpoint, s->errors);
                continue;
            }
        }
        else if (value == E72TEMP_POWER_ON_VALUE && s->measuredValue == DS18B20_INVALID_TEMP)
        {
            /* 85.00 degC right after power-up is the reset value, not a measurement */
            continue;
        }
        else
        {
            int16_t absValue = (value < 0) ? (int16_t)-value : value;
            s->errors = 0;
            s->measuredValue = value;
            DBG_LOG("ep %u: %s%d.%02d C", endpoint, (value < 0) ? "-" : "",
                    absValue / 100, absValue % 100);
        }

        e72Temp_report(s);
    }
}

void e72Temp_processEvents(uint32_t *pEvents)
{
    if (*pEvents & E72TEMP_CONVERT_EVT)
    {
        *pEvents &= ~E72TEMP_CONVERT_EVT;
        if (ds18b20_startConversionAll())
        {
            UtilTimer_start(&readClkStruct);
        }
        else
        {
            DBG_LOG("1-Wire: no presence pulse");
        }
    }

    if (*pEvents & E72TEMP_READ_EVT)
    {
        *pEvents &= ~E72TEMP_READ_EVT;
        e72Temp_readAll();
    }
}
