/**
 ********************************************************************
 * @file    sensor_simulation.h
 * @brief   Simulated sensor data transmitter header
 *********************************************************************
 */
#ifndef SENSOR_SIMULATION_H
#define SENSOR_SIMULATION_H

#include "dji_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

T_DjiReturnCode DjiTest_SensorSimStartService(void);
T_DjiReturnCode DjiTest_SensorSimStopService(void);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_SIMULATION_H

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
