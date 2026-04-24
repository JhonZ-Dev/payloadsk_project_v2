/**
 ********************************************************************
 * @file    sensor_simulation.c
 * @brief   Read real RDO Blue sensor data via native Modbus RTU (C) and send
 *          them to the remote controller via data transmission APIs.
 *          No external Python script needed.
 *********************************************************************
 */

#include "sensor_simulation.h"
#include "rdo_modbus.h"
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
#define SENSOR_SIM_TASK_FREQ_MS        (5000)
#define SENSOR_SIM_TASK_STACK_SIZE     (2048)
#define GPS_POSITION_SCALE             (10000000.0)

/* Serial port for the RS-485 adapter connected to RDO Blue sensor */
#define RDO_SERIAL_PORT "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5958032059-if00"

/* Private variables ---------------------------------------------------------*/
static T_DjiTaskHandle s_sensorSimThread = 0;
static bool s_gpsTopicSubscribed = false;
static bool s_modbusInitialized = false;
static T_DjiAircraftInfoBaseInfo s_aircraftInfoBaseInfo;

/* Private functions ---------------------------------------------------------*/
static void *SensorSim_Task(void *arg);

/* Receive callbacks (needed so SDK opens bidirectional channel to MSDK) */
static T_DjiReturnCode SensorSim_ReceiveDataFromMobile(const uint8_t *data, uint16_t len)
{
    USER_LOG_INFO("sensor sim: received %d bytes from MSDK", len);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode SensorSim_ReceiveDataFromPayloadChannel(const uint8_t *data, uint16_t len)
{
    USER_LOG_DEBUG("sensor sim: received %d bytes from payload channel", len);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode DjiTest_SensorSimStartService(void)
{
    T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();
    T_DjiReturnCode returnCode;

    if (osalHandler == NULL) {
        USER_LOG_ERROR("osal handler is null");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    /* Initialize Modbus RTU communication with RDO Blue sensor */
    if (RdoModbus_Init(RDO_SERIAL_PORT) == 0) {
        s_modbusInitialized = true;
        USER_LOG_INFO("sensor sim: RDO Blue Modbus RTU initialized on %s", RDO_SERIAL_PORT);
    } else {
        s_modbusInitialized = false;
        USER_LOG_WARN("sensor sim: RDO Blue Modbus init failed, will use fallback data");
    }

    /* Initialize low-speed data channel for sending sensor data */
    returnCode = DjiLowSpeedDataChannel_Init();
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("sensor sim: low speed data channel init error: 0x%08X", returnCode);
        return returnCode;
    }

    /* Get aircraft info for M400-specific channel setup */
    returnCode = DjiAircraftInfo_GetBaseInfo(&s_aircraftInfoBaseInfo);
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("sensor sim: get aircraft base info failed: 0x%08X", returnCode);
    }

    /* Register receive callback for MSDK channel (required for bidirectional pipe) */
    returnCode = DjiLowSpeedDataChannel_RegRecvDataCallback(DJI_CHANNEL_ADDRESS_MASTER_RC_APP,
                                                            SensorSim_ReceiveDataFromMobile);
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("sensor sim: register MSDK recv callback error: 0x%08X", returnCode);
    }

    /* M400: register all payload/extension-v2 channels for PSDK<->MSDK data */
    if (s_aircraftInfoBaseInfo.aircraftType == DJI_AIRCRAFT_TYPE_M400) {
        const E_DjiChannelAddress m400Channels[] = {
            DJI_CHANNEL_ADDRESS_PAYLOAD_PORT_NO1,
            DJI_CHANNEL_ADDRESS_PAYLOAD_PORT_NO2,
            DJI_CHANNEL_ADDRESS_PAYLOAD_PORT_NO3,
            DJI_CHANNEL_ADDRESS_EXTENSION_PORT_V2_NO4,
            DJI_CHANNEL_ADDRESS_EXTENSION_PORT_V2_NO5,
            DJI_CHANNEL_ADDRESS_EXTENSION_PORT_V2_NO6,
            DJI_CHANNEL_ADDRESS_EXTENSION_PORT_V2_NO7,
            DJI_CHANNEL_ADDRESS_EXTENSION_PORT_V2_NO8,
        };
        for (uint8_t i = 0; i < sizeof(m400Channels) / sizeof(m400Channels[0]); i++) {
            returnCode = DjiLowSpeedDataChannel_RegRecvDataCallback(m400Channels[i],
                                                                    SensorSim_ReceiveDataFromPayloadChannel);
            if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                USER_LOG_WARN("sensor sim: register M400 channel %d callback error: 0x%08X", m400Channels[i], returnCode);
            }
        }
        USER_LOG_INFO("sensor sim: M400 PSDK<->MSDK channels registered");
    } else if (s_aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1 ||
               s_aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_PAYLOAD_PORT_NO2 ||
               s_aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_PAYLOAD_PORT_NO3) {
        returnCode = DjiLowSpeedDataChannel_RegRecvDataCallback(DJI_CHANNEL_ADDRESS_EXTENSION_PORT,
                                                                SensorSim_ReceiveDataFromPayloadChannel);
        if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            USER_LOG_WARN("sensor sim: register extension port recv callback error: 0x%08X", returnCode);
        }
    } else if (s_aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_EXTENSION_PORT) {
        returnCode = DjiLowSpeedDataChannel_RegRecvDataCallback(DJI_CHANNEL_ADDRESS_PAYLOAD_PORT_NO1,
                                                                SensorSim_ReceiveDataFromPayloadChannel);
        if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            USER_LOG_WARN("sensor sim: register payload port recv callback error: 0x%08X", returnCode);
        }
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

    if (s_modbusInitialized) {
        RdoModbus_DeInit();
        s_modbusInitialized = false;
    }

    DjiLowSpeedDataChannel_DeInit();

    if (osalHandler->TaskDestroy(s_sensorSimThread) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("sensor sim task destroy error.");
        return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}
static void *SensorSim_Task(void *arg)
{
    T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();
    T_DjiReturnCode djiStat;
    E_DjiChannelAddress channelAddress;
    USER_UTIL_UNUSED(arg);

    USER_LOG_INFO("SensorSim_Task: RDO Modbus reader active (period=%dms)", SENSOR_SIM_TASK_FREQ_MS);

    while (1) {
        osalHandler->TaskSleepMs(SENSOR_SIM_TASK_FREQ_MS);

        /* --- GPS --- */
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

        /* --- Sensor reading --- */
        float temperature = 0.0f;
        float oxygen = 0.0f;
        float saturation = 0.0f;
        float partial_pressure = 0.0f;
        int sensorOk = 0;
        uint64_t sensorTsMs = 0;

        /* Keep sensorData in outer scope so we can use its timestamp */
        T_RdoSensorData sensorData = {0};
        if (s_modbusInitialized) {
            if (RdoModbus_ReadSensorData(&sensorData) == 0) {
                temperature = sensorData.temperature_c;
                oxygen = sensorData.dissolved_oxygen_mg_l;
                saturation = sensorData.do_saturation_percent;
                partial_pressure = sensorData.oxygen_partial_pressure_torr;
                sensorTsMs = sensorData.timestamp_ms;
                sensorOk = 1;
            } else {
                USER_LOG_WARN("sensor sim: Modbus read failed, retrying next cycle");
            }
        }

        if (!sensorOk) {
            USER_LOG_WARN("sensor sim: no valid data, skipping send");
            continue;
        }

        /* --- Build and send payload --- */
        /* Prefer sensor-provided timestamp (ms since epoch). Fallback to OSAL time if missing. */
        uint64_t usedTsMs = sensorTsMs;
        if (usedTsMs == 0) {
            uint32_t currentTimeMs = 0;
            osalHandler->GetTimeMs(&currentTimeMs);
            usedTsMs = (uint64_t)currentTimeMs;
        }

        char payload[256];
        int len = snprintf(payload, sizeof(payload),
                       "{\"t\":%.1f,\"o\":%.2f,\"s\":%.1f,\"p\":%.1f,\"la\":%.7f,\"lo\":%.7f,\"a\":%.1f,\"g\":%u,\"ts\":%llu}",
                       temperature, oxygen, saturation, partial_pressure,
                       latitudeDeg, longitudeDeg, altitudeM, gpsValid,
                       (unsigned long long)(usedTsMs / 1ULL));
        if (len >= (int)sizeof(payload)) len = (int)sizeof(payload) - 1;

        /* Send to mobile/RC */
        channelAddress = DJI_CHANNEL_ADDRESS_MASTER_RC_APP;
        djiStat = DjiLowSpeedDataChannel_SendData(channelAddress, (const uint8_t *)payload, (uint16_t)len);
        if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            USER_LOG_ERROR("sensor sim: send data to mobile error.");
        } else {
            USER_LOG_DEBUG("sensor sim: sent REAL data: %s", payload);
            DjiTest_WidgetLogAppend("RDO: T=%.1f°C O2=%.1fmg/L Sat=%.1f%%", temperature, oxygen, saturation);
        }

        /* Also send to extension/payload ports depending on mount */
        if (s_aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_PAYLOAD_PORT_NO1 ||
            s_aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_PAYLOAD_PORT_NO2 ||
            s_aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_PAYLOAD_PORT_NO3) {
            channelAddress = DJI_CHANNEL_ADDRESS_EXTENSION_PORT;
            djiStat = DjiLowSpeedDataChannel_SendData(channelAddress, (const uint8_t *)payload, (uint16_t)len);
        } else if (s_aircraftInfoBaseInfo.mountPosition == DJI_MOUNT_POSITION_EXTENSION_PORT) {
            channelAddress = DJI_CHANNEL_ADDRESS_PAYLOAD_PORT_NO1;
            djiStat = DjiLowSpeedDataChannel_SendData(channelAddress, (const uint8_t *)payload, (uint16_t)len);
        }
    }

    return NULL;
}

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
