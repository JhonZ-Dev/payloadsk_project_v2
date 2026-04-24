/**
 ********************************************************************
 * @file    sensor_simulation.c
 * @brief   Read real RDO Blue sensor data from shared JSON file and send them
 *          to the remote controller via data transmission APIs.
 *          The data is written continuously by the Python reader script.
 *********************************************************************
 */

#include "sensor_simulation.h"
#include "dji_logger.h"
#include "dji_platform.h"
#include "utils/util_misc.h"
#include "dji_low_speed_data_channel.h"
#include "dji_high_speed_data_channel.h"
#include "dji_aircraft_info.h"
#include "dji_fc_subscription.h"
#include "widget_interaction_test/test_widget_interaction.h"
#include <stdio.h>
#include <string.h>

/* Private constants ---------------------------------------------------------*/
#define SENSOR_SIM_TASK_FREQ_MS        (2000)
#define SENSOR_SIM_TASK_STACK_SIZE     (2048)
#define SHARED_SENSOR_DATA_FILE        "/tmp/rdo_sensor_data.json"
#define GPS_POSITION_SCALE             (10000000.0)

/* Private variables ---------------------------------------------------------*/
static T_DjiTaskHandle s_sensorSimThread = 0;
static bool s_gpsTopicSubscribed = false;

/* Private functions ---------------------------------------------------------*/
static void *SensorSim_Task(void *arg);
static int SensorSim_ReadJsonData(float *temperature, float *oxygen, float *saturation, float *partial_pressure);

T_DjiReturnCode DjiTest_SensorSimStartService(void)
{
    T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();
    T_DjiReturnCode returnCode;

    if (osalHandler == NULL) {
        USER_LOG_ERROR("osal handler is null");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    returnCode = DjiFcSubscription_Init();
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("sensor sim: fc subscription init failed, gps disabled. code=0x%08X", returnCode);
    } else {
        returnCode = DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_GPS_POSITION,
                                                      DJI_DATA_SUBSCRIPTION_TOPIC_1_HZ,
                                                      NULL);
        if (returnCode == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            s_gpsTopicSubscribed = true;
            USER_LOG_INFO("sensor sim: GPS topic subscription enabled");
        } else {
            USER_LOG_WARN("sensor sim: GPS topic subscribe failed, continue without gps. code=0x%08X", returnCode);
        }
    }

    if (osalHandler->TaskCreate("sensor_sim_task", SensorSim_Task,
                                SENSOR_SIM_TASK_STACK_SIZE, NULL, &s_sensorSimThread) !=
        DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("sensor sim task create error.");
        return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode DjiTest_SensorSimStopService(void)
{
    T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();

    if (osalHandler == NULL) {
        USER_LOG_ERROR("osal handler is null");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    if (s_gpsTopicSubscribed) {
        DjiFcSubscription_UnSubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_GPS_POSITION);
        s_gpsTopicSubscribed = false;
    }

    if (osalHandler->TaskDestroy(s_sensorSimThread) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("sensor sim task destroy error.");
        return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static int SensorSim_ReadJsonData(float *temperature, float *oxygen, float *saturation, float *partial_pressure)
{
    FILE *fp = fopen(SHARED_SENSOR_DATA_FILE, "r");
    if (fp == NULL) {
        USER_LOG_DEBUG("Sensor data file not available yet: %s", SHARED_SENSOR_DATA_FILE);
        return -1;
    }

    char buffer[256];
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Simple JSON parsing (assumes fixed format from Python script) */
    /* Expected: {"timestamp_ms": ..., "temperature_c": X, "dissolved_oxygen_mg_l": Y, ...} */
    if (sscanf(buffer, 
               "{\"timestamp_ms\": %*d, \"temperature_c\": %f, \"dissolved_oxygen_mg_l\": %f, \"do_saturation_percent\": %f, \"oxygen_partial_pressure_torr\": %f",
               temperature, oxygen, saturation, partial_pressure) != 4) {
        USER_LOG_DEBUG("Failed to parse sensor JSON: %s", buffer);
        return -1;
    }

    return 0;
}
static void *SensorSim_Task(void *arg)
{
    T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();
    T_DjiReturnCode djiStat;
    E_DjiChannelAddress channelAddress;
    T_DjiAircraftInfoBaseInfo aircraftInfoBaseInfo;
    USER_UTIL_UNUSED(arg);

    if (DjiAircraftInfo_GetBaseInfo(&aircraftInfoBaseInfo) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("get aircraft base info fail in sensor sim");
    }

    USER_LOG_INFO("SensorSim_Task: Starting to read from %s", SHARED_SENSOR_DATA_FILE);

    while (1) {
        osalHandler->TaskSleepMs(SENSOR_SIM_TASK_FREQ_MS);

        T_DjiFcSubscriptionGpsPosition gpsPosition = {0};
        T_DjiDataTimestamp gpsTimestamp = {0};
        double longitudeDeg = 0.0;
        double latitudeDeg = 0.0;
        double altitudeM = 0.0;
        uint8_t gpsValid = 0;

        if (s_gpsTopicSubscribed) {
            djiStat = DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_GPS_POSITION,
                                                              (uint8_t *) &gpsPosition,
                                                              sizeof(T_DjiFcSubscriptionGpsPosition),
                                                              &gpsTimestamp);
            if (djiStat == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                longitudeDeg = ((double) gpsPosition.x) / GPS_POSITION_SCALE;
                latitudeDeg = ((double) gpsPosition.y) / GPS_POSITION_SCALE;
                altitudeM = ((double) gpsPosition.z) / 1000.0;
                gpsValid = 1;
            }
        }

        /* Try to read real sensor data from shared JSON file */
        float temperature = 0.0f;
        float oxygen = 0.0f;
        float saturation = 0.0f;
        float partial_pressure = 0.0f;

        if (SensorSim_ReadJsonData(&temperature, &oxygen, &saturation, &partial_pressure) == 0) {
            /* Successfully read real data from Python/RDO Blue sensor */
            uint32_t currentTimeMs = 0;
            osalHandler->GetTimeMs(&currentTimeMs);
            
            char payload[320];
            int len = snprintf(payload, sizeof(payload), 
                           "{\"type\":\"sensor\",\"temp\":%.2f,\"oxi\":%.2f,\"sat\":%.2f,\"pp\":%.2f,\"lat\":%.7f,\"lon\":%.7f,\"alt\":%.2f,\"gps_valid\":%u,\"ts\":%u}",
                           temperature, oxygen, saturation, partial_pressure,
                           latitudeDeg, longitudeDeg, altitudeM, gpsValid,
                           (unsigned int)(currentTimeMs / 1000));

            /* Send to mobile/RC first */
            channelAddress = DJI_CHANNEL_ADDRESS_MASTER_RC_APP;
            djiStat = DjiLowSpeedDataChannel_SendData(channelAddress, (const uint8_t *)payload, (uint16_t)len);
            if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                USER_LOG_ERROR("sensor sim: send data to mobile error.");
            } else {
                USER_LOG_DEBUG("sensor sim: sent real RDO data: %s", payload);
                DjiTest_WidgetLogAppend("RDO Real: T=%.1f°C O2=%.1fmg/L Sat=%.1f%%", temperature, oxygen, saturation);
            }

            /* Also send to extension/payload ports depending on mount */
            if (aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1 ||
                aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_PAYLOAD_PORT_NO2 ||
                aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_PAYLOAD_PORT_NO3) {
                channelAddress = DJI_CHANNEL_ADDRESS_EXTENSION_PORT;
                djiStat = DjiLowSpeedDataChannel_SendData(channelAddress, (const uint8_t *)payload, (uint16_t)len);
                if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                    USER_LOG_ERROR("sensor sim: send data to extension port error.");
                }
            } else if (aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_EXTENSION_PORT) {
                channelAddress = DJI_CHANNEL_ADDRESS_PAYLOAD_PORT_NO1;
                djiStat = DjiLowSpeedDataChannel_SendData(channelAddress, (const uint8_t *)payload, (uint16_t)len);
                if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                    USER_LOG_ERROR("sensor sim: send data to payload port error.");
                }
            }
        } else {
            /* Fallback: generate synthetic data if Python reader is not running */
            uint32_t t = 0;
            uint16_t randomNum1 = 0;
            uint16_t randomNum2 = 0;
            
            osalHandler->GetTimeMs(&t);
            osalHandler->GetRandomNum(&randomNum1);
            osalHandler->GetRandomNum(&randomNum2);
            
            float temperature_sim = 20.0f + (float)(randomNum1 % 100) / 10.0f;
            float oxygen_sim = 8.0f + (float)(randomNum2 % 50) / 10.0f;
            float saturation_sim = 80.0f + (float)(randomNum1 % 200) / 10.0f;
            float partial_pressure_sim = 100.0f + (float)(randomNum2 % 100) / 1.0f;

            char payload[300];
            int len = snprintf(payload, sizeof(payload), "{\"type\":\"sensor\",\"temp\":%.2f,\"oxi\":%.2f,\"sat\":%.2f,\"pp\":%.2f,\"lat\":%.7f,\"lon\":%.7f,\"alt\":%.2f,\"gps_valid\":%u,\"ts\":%u}",
                           temperature_sim, oxygen_sim, saturation_sim, partial_pressure_sim, latitudeDeg, longitudeDeg, altitudeM, gpsValid, (unsigned int)(t / 1000));

            channelAddress = DJI_CHANNEL_ADDRESS_MASTER_RC_APP;
            djiStat = DjiLowSpeedDataChannel_SendData(channelAddress, (const uint8_t *)payload, (uint16_t)len);
            if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                USER_LOG_ERROR("sensor sim: send fallback data to mobile error.");
            } else {
                USER_LOG_DEBUG("sensor sim: sent simulated data (Python reader not running): %s", payload);
            }
        }
    }

    return NULL;
}

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
