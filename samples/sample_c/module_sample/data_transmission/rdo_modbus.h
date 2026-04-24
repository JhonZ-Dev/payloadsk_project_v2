/**
 ********************************************************************
 * @file    rdo_modbus.h
 * @brief   RDO Blue sensor Modbus RTU reader (native C, no external libs)
 *********************************************************************
 */
#ifndef RDO_MODBUS_H
#define RDO_MODBUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sensor data structure */
typedef struct {
    float temperature_c;
    float dissolved_oxygen_mg_l;
    float do_saturation_percent;
    float oxygen_partial_pressure_torr;
    uint64_t timestamp_ms;
    int valid; /* 1 if data was read successfully, 0 otherwise */
} T_RdoSensorData;

/**
 * @brief Open the serial port and configure for Modbus RTU (19200, 8E1).
 * @param portPath: e.g. "/dev/ttyACM0" or "/dev/serial/by-id/..."
 * @return 0 on success, -1 on failure
 */
int RdoModbus_Init(const char *portPath);

/**
 * @brief Close the serial port.
 */
void RdoModbus_DeInit(void);

/**
 * @brief Read all 4 sensor values from the RDO Blue sensor.
 * @param data: pointer to structure to fill
 * @return 0 on success, -1 on failure
 */
int RdoModbus_ReadSensorData(T_RdoSensorData *data);

#ifdef __cplusplus
}
#endif

#endif /* RDO_MODBUS_H */
