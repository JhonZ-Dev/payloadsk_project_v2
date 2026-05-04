/**
 ********************************************************************
 * @file    rdo_mqtt_client.h
 * @brief   Header file for MQTT client to send RDO sensor data to external MQTT broker.
 *          This bypasses the DJI Cloud API which only works on Manifold 3.
 *
 * @copyright (c) 2024 DJI. All rights reserved.
 *********************************************************************
 */

#ifndef RDO_MQTT_CLIENT_H
#define RDO_MQTT_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "dji_typedef.h"
#include <stdint.h>
#include <stdbool.h>

/* Exported types ------------------------------------------------------------*/

/**
 * @brief MQTT client configuration structure
 */
typedef struct {
    char broker_host[128];       /*!< MQTT broker hostname or IP address */
    uint16_t broker_port;        /*!< MQTT broker port (default: 1883) */
    char client_id[64];          /*!< MQTT client ID (auto-generated if empty) */
    char username[64];           /*!< MQTT username (empty for anonymous) */
    char password[64];           /*!< MQTT password (empty for anonymous) */
    char topic[128];             /*!< MQTT topic to publish sensor data */
    uint8_t qos;                 /*!< MQTT QoS level (0, 1, or 2) */
    bool retain_message;         /*!< Whether to retain messages */
    uint16_t keep_alive_interval; /*!< Keep-alive interval in seconds */
} T_RdoMqttConfig;

/**
 * @brief MQTT connection status
 */
typedef enum {
    RDO_MQTT_DISCONNECTED = 0,    /*!< Not connected */
    RDO_MQTT_CONNECTING,          /*!< Connecting to broker */
    RDO_MQTT_CONNECTED,           /*!< Connected to broker */
    RDO_MQTT_ERROR                /*!< Connection error */
} E_RdoMqttStatus;

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize MQTT client with configuration
 * @param config: pointer to MQTT configuration structure
 * @return Execution result
 */
T_DjiReturnCode RdoMqtt_Init(const T_RdoMqttConfig *config);

/**
 * @brief Start MQTT client connection and background task
 * @return Execution result
 */
T_DjiReturnCode RdoMqtt_Start(void);

/**
 * @brief Stop MQTT client and disconnect
 * @return Execution result
 */
T_DjiReturnCode RdoMqtt_Stop(void);

/**
 * @brief Send sensor data to MQTT broker
 * @param temperature: temperature in Celsius
 * @param oxygen: dissolved oxygen in mg/L
 * @param saturation: DO saturation percentage
 * @param partial_pressure: oxygen partial pressure in Torr
 * @param latitude: GPS latitude
 * @param longitude: GPS longitude
 * @param altitude: GPS altitude in meters
 * @param gps_valid: GPS validity flag (0 or 1)
 * @return Execution result
 */
T_DjiReturnCode RdoMqtt_SendSensorData(float temperature, float oxygen, float saturation,
                                       float partial_pressure, double latitude,
                                       double longitude, double altitude, uint8_t gps_valid);

/**
 * @brief Get current MQTT connection status
 * @return Current connection status
 */
E_RdoMqttStatus RdoMqtt_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* RDO_MQTT_CLIENT_H */

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
