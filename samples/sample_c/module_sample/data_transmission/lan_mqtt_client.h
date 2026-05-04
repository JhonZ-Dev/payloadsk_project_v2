/**
 ********************************************************************
 * @file    lan_mqtt_client.h
 * @brief   Header file for LAN MQTT Client
 *          Sends sensor data directly to Mosquitto broker over local LAN.
 *          No internet connection required on Raspberry Pi.
 *
 * @copyright (c) 2021 DJI. All rights reserved.
 *********************************************************************
 */
#ifndef LAN_MQTT_CLIENT_H
#define LAN_MQTT_CLIENT_H

#include "dji_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LAN MQTT client and connect to broker.
 * @param brokerAddr MQTT broker IP address (e.g., "172.16.10.136")
 * @param brokerPort MQTT broker port (e.g., 1883)
 * @param clientID Client ID for MQTT connection
 * @param topic MQTT topic to publish sensor data
 * @return Execution result
 */
T_DjiReturnCode LAN_MQTTClient_Init(const char *brokerAddr, uint16_t brokerPort,
                                     const char *clientID, const char *topic);

/**
 * @brief Publish sensor data to MQTT broker.
 * @param data Pointer to data buffer
 * @param len Data length in bytes
 * @return Execution result
 */
T_DjiReturnCode LAN_MQTTClient_SendData(const uint8_t *data, uint32_t len);

/**
 * @brief Deinitialize MQTT client and disconnect.
 * @return Execution result
 */
T_DjiReturnCode LAN_MQTTClient_DeInit(void);

/**
 * @brief Check if MQTT client is connected.
 * @return true if connected, false otherwise
 */
bool LAN_MQTTClient_IsConnected(void);

#ifdef __cplusplus
}
#endif

#endif // LAN_MQTT_CLIENT_H

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
