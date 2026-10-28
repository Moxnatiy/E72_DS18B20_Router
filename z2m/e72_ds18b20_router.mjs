// Zigbee2MQTT external converter for the E72-2G4M20S1E router with DS18B20 sensors.
// Copy to <z2m data>/external_converters/ and restart Zigbee2MQTT.
//
// Endpoint 8 is the router itself (Basic + TX power attribute 0x1337),
// endpoints 10..25 are DS18B20 sensors (Temperature Measurement 0x0402), one per sensor
// found on the 1-Wire bus at boot, ordered by ROM code.
import * as m from "zigbee-herdsman-converters/lib/modernExtend";

const FIRST_TEMP_ENDPOINT = 10;
const MAX_SENSORS = 16;

const endpoints = {router: 8};
for (let i = 0; i < MAX_SENSORS; i++) {
    endpoints[`t${i + 1}`] = FIRST_TEMP_ENDPOINT + i;
}

export default {
    zigbeeModel: ["E72.DS18B20.Router"],
    model: "E72.DS18B20.Router",
    vendor: "DIYZ",
    description: "CC2652P (Ebyte E72) Zigbee router with DS18B20 sensors",
    extend: [
        m.deviceEndpoints({endpoints}),
        // The firmware pushes reports itself (every 60 s or on a 0.1 degC change),
        // so Z2M must not try to configure reporting on this attribute.
        m.temperature({
            endpointNames: Object.keys(endpoints).filter((e) => e !== "router"),
            reporting: false,
            precision: 2,
        }),
        m.numeric({
            name: "transmit_power",
            cluster: "genBasic",
            attribute: {ID: 0x1337, type: 0x28},
            endpointName: "router",
            unit: "dBm",
            valueMin: -20,
            valueMax: 20,
            description: "Radio TX power, stored in NV",
            access: "ALL",
        }),
    ],
    ota: false,
};
