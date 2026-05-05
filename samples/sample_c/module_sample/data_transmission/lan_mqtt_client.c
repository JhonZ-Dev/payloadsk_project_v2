/**
 ********************************************************************
 * @file    lan_mqtt_client.c
 * @brief   LAN MQTT Client implementation
 *          Sends sensor data directly to Mosquitto broker over local LAN.
 *          No internet connection required on Raspberry Pi.
 *
 *          Architecture:
 *          Raspberry Pi (no internet) ──[LAN]──> Mosquitto Broker (172.16.10.136:1883)
 *
 *          Uses dynamic loading (dlopen/dlsym) to link libmosquitto
 *          at runtime since headers may not be available.
 *
 * @copyright (c) 2021 DJI. All rights reserved.
 *********************************************************************
 */

#include "lan_mqtt_client.h"
#include "dji_logger.h"
#include "dji_platform.h"
#include "utils/util_misc.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Private constants ---------------------------------------------------------*/
#define MQTT_CLIENT_TASK_FREQ_MS    (1000)
#define MQTT_CLIENT_TASK_STACK_SIZE (4096)
#define MQTT_MAX_BROKER_ADDR_LEN    (64)
#define MQTT_MAX_CLIENT_ID_LEN      (64)
#define MQTT_MAX_TOPIC_LEN          (128)
#define MQTT_MAX_MESSAGE_LEN        (1024)
#define MQTT_LIB_NAME               "libmosquitto.so.2"

/* MQTT function pointers (dynamic loading) ----------------------------------*/
static void *s_mqttLib = NULL;

/* Function pointer types for libmosquitto functions */
typedef void *(*t_mosquitto_new)(const char *id, bool clean_session, void *obj);
typedef void (*t_mosquitto_destroy)(void *client);
typedef int (*t_mosquitto_connect)(void *client, const char *host, int port, int keepalive);
typedef int (*t_mosquitto_disconnect)(void *client);
typedef int (*t_mosquitto_publish)(void *client, void *mid, const char *topic, int payloadlen, 
                                    const void *payload, int qos, bool retain);
typedef int (*t_mosquitto_loop)(void *client, int timeout, int max_packets);
typedef const char *(*t_mosquitto_strerror)(int rc);
typedef void (*t_mosquitto_lib_init)(void);
typedef void (*t_mosquitto_lib_cleanup)(void);

/* Global function pointers */
static t_mosquitto_new fn_mosquitto_new = NULL;
static t_mosquitto_destroy fn_mosquitto_destroy = NULL;
static t_mosquitto_connect fn_mosquitto_connect = NULL;
static t_mosquitto_disconnect fn_mosquitto_disconnect = NULL;
static t_mosquitto_publish fn_mosquitto_publish = NULL;
static t_mosquitto_loop fn_mosquitto_loop = NULL;
static t_mosquitto_strerror fn_mosquitto_strerror = NULL;
static t_mosquitto_lib_init fn_mosquitto_lib_init = NULL;
static t_mosquitto_lib_cleanup fn_mosquitto_lib_cleanup = NULL;

/* Private variables ---------------------------------------------------------*/
static void *s_mqttClient = NULL;
static char s_brokerAddr[MQTT_MAX_BROKER_ADDR_LEN];
static uint16_t s_brokerPort = 0;
static char s_clientID[MQTT_MAX_CLIENT_ID_LEN];
static char s_topic[MQTT_MAX_TOPIC_LEN];
static bool s_isConnected = false;
static bool s_isInitialized = false;
static T_DjiTaskHandle s_mqttTask = NULL;
static T_DjiOsalHandler *s_osalHandler = NULL;
static uint8_t s_sendBuffer[MQTT_MAX_MESSAGE_LEN];
static uint32_t s_sendLen = 0;

/* Private functions ---------------------------------------------------------*/
static int MQTTClient_LoadLibrary(void);
static void MQTTClient_UnloadLibrary(void);
static T_DjiReturnCode MQTTClient_Connect(void);
static void MQTTClient_Disconnect(void);

/**
 * @brief Load libmosquitto dynamically.
 */
static int MQTTClient_LoadLibrary(void)
{
    if (s_mqttLib != NULL) {
        return 0; /* Already loaded */
    }

    s_mqttLib = dlopen(MQTT_LIB_NAME, RTLD_NOW);
    if (s_mqttLib == NULL) {
        USER_LOG_ERROR("lan_mqtt: failed to load %s: %s", MQTT_LIB_NAME, dlerror());
        return -1;
    }

    /* Load function pointers */
    #define LOAD_SYM(name) \
        fn_##name = (t_##name)dlsym(s_mqttLib, #name); \
        if (fn_##name == NULL) { \
            USER_LOG_ERROR("lan_mqtt: failed to load %s: %s", #name, dlerror()); \
            dlclose(s_mqttLib); \
            s_mqttLib = NULL; \
            return -1; \
        }

    LOAD_SYM(mosquitto_new)
    LOAD_SYM(mosquitto_destroy)
    LOAD_SYM(mosquitto_connect)
    LOAD_SYM(mosquitto_disconnect)
    LOAD_SYM(mosquitto_publish)
    LOAD_SYM(mosquitto_loop)
    LOAD_SYM(mosquitto_strerror)
    LOAD_SYM(mosquitto_lib_init)
    LOAD_SYM(mosquitto_lib_cleanup)

    #undef LOAD_SYM

    USER_LOG_INFO("lan_mqtt: libmosquitto loaded successfully");
    return 0;
}

/**
 * @brief Unload libmosquitto.
 */
static void MQTTClient_UnloadLibrary(void)
{
    if (s_mqttLib != NULL) {
        dlclose(s_mqttLib);
        s_mqttLib = NULL;
        fn_mosquitto_new = NULL;
        fn_mosquitto_destroy = NULL;
        fn_mosquitto_connect = NULL;
        fn_mosquitto_disconnect = NULL;
        fn_mosquitto_publish = NULL;
        fn_mosquitto_loop = NULL;
        fn_mosquitto_strerror = NULL;
        fn_mosquitto_lib_init = NULL;
        fn_mosquitto_lib_cleanup = NULL;
    }
}

/**
 * @brief Connect to MQTT broker.
 */
static T_DjiReturnCode MQTTClient_Connect(void)
{
    int rc;

    if (s_isConnected) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    if (fn_mosquitto_new == NULL) {
        USER_LOG_ERROR("lan_mqtt: library not loaded");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    s_mqttClient = fn_mosquitto_new(s_clientID, true, NULL);
    if (s_mqttClient == NULL) {
        USER_LOG_ERROR("lan_mqtt: mosquitto_new failed");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    rc = fn_mosquitto_connect(s_mqttClient, s_brokerAddr, (int)s_brokerPort, 60);
    if (rc != 0) {
        const char *errStr = (fn_mosquitto_strerror != NULL) ? fn_mosquitto_strerror(rc) : "unknown";
        USER_LOG_ERROR("lan_mqtt: connect failed (%d): %s", rc, errStr);
        fn_mosquitto_destroy(s_mqttClient);
        s_mqttClient = NULL;
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    s_isConnected = true;

    USER_LOG_INFO("lan_mqtt: connected to %s:%d", s_brokerAddr, s_brokerPort);

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/**
 * @brief Disconnect from MQTT broker.
 */
static void MQTTClient_Disconnect(void)
{
    if (s_mqttClient == NULL) {
        return;
    }

    if (fn_mosquitto_disconnect) {
        fn_mosquitto_disconnect(s_mqttClient);
    }
    if (fn_mosquitto_destroy) {
        fn_mosquitto_destroy(s_mqttClient);
    }
    s_mqttClient = NULL;
    s_isConnected = false;

    USER_LOG_INFO("lan_mqtt: disconnected from broker");
}

/**
 * @brief MQTT client task - handles connection and data sending.
 */
static void *MQTTClient_Task(void *arg)
{
    T_DjiReturnCode stat;

    USER_UTIL_UNUSED(arg);

    USER_LOG_INFO("lan_mqtt: task started");

    /* Initial connection attempt */
    stat = MQTTClient_Connect();
    if (stat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("lan_mqtt: initial connect failed, will retry");
    }

    while (1) {
        s_osalHandler->TaskSleepMs(MQTT_CLIENT_TASK_FREQ_MS);

        /* Call mosquitto_loop to maintain connection (keepalive, ping, etc.) */
        if (s_isConnected && s_mqttClient != NULL && fn_mosquitto_loop) {
            int rc = fn_mosquitto_loop(s_mqttClient, 100, 1);
            if (rc != 0) {
                const char *errStr = (fn_mosquitto_strerror != NULL) ? fn_mosquitto_strerror(rc) : "unknown";
                USER_LOG_WARN("lan_mqtt: loop failed (%d): %s", rc, errStr);
                
                /* Connection lost, disconnect and reconnect */
                MQTTClient_Disconnect();
                s_isConnected = false;
            }
        }

        /* Check if we have data to send */
        if (s_sendLen > 0) {
            if (s_isConnected && s_mqttClient != NULL) {
                int rc = fn_mosquitto_publish(s_mqttClient, NULL, s_topic,
                                             (int)s_sendLen, s_sendBuffer, 1, false);
                if (rc == 0) {
                    USER_LOG_DEBUG("lan_mqtt: published %d bytes to topic '%s'", s_sendLen, s_topic);
                } else {
                    const char *errStr = (fn_mosquitto_strerror != NULL) ? fn_mosquitto_strerror(rc) : "unknown";
                    USER_LOG_WARN("lan_mqtt: publish failed (%d): %s", rc, errStr);
                    
                    /* Try to reconnect if publish failed */
                    MQTTClient_Disconnect();
                    stat = MQTTClient_Connect();
                    if (stat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                        USER_LOG_ERROR("lan_mqtt: reconnect failed");
                    }
                }
            } else {
                USER_LOG_WARN("lan_mqtt: cannot publish - not connected");
            }
        }
        
        s_sendLen = 0; /* Clear buffer after send attempt */

        /* Check connection status periodically */
        if (!s_isConnected) {
            stat = MQTTClient_Connect();
            if (stat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                USER_LOG_WARN("lan_mqtt: reconnect attempt failed");
            }
        }
    }

    return NULL;
}

/* Exported functions --------------------------------------------------------*/

T_DjiReturnCode LAN_MQTTClient_Init(const char *brokerAddr, uint16_t brokerPort,
                                     const char *clientID, const char *topic)
{
    T_DjiReturnCode returnCode;
    char taskName[32];

    if (brokerAddr == NULL || clientID == NULL || topic == NULL) {
        USER_LOG_ERROR("lan_mqtt: invalid parameters");
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    s_osalHandler = DjiPlatform_GetOsalHandler();
    if (s_osalHandler == NULL) {
        USER_LOG_ERROR("lan_mqtt: osal handler is null");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    /* Load libmosquitto */
    if (MQTTClient_LoadLibrary() != 0) {
        USER_LOG_ERROR("lan_mqtt: failed to load libmosquitto");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    /* Initialize MQTT library */
    if (fn_mosquitto_lib_init) {
        fn_mosquitto_lib_init();
    }

    /* Store broker configuration */
    strncpy(s_brokerAddr, brokerAddr, MQTT_MAX_BROKER_ADDR_LEN - 1);
    s_brokerAddr[MQTT_MAX_BROKER_ADDR_LEN - 1] = '\0';
    s_brokerPort = brokerPort;
    
    strncpy(s_clientID, clientID, MQTT_MAX_CLIENT_ID_LEN - 1);
    s_clientID[MQTT_MAX_CLIENT_ID_LEN - 1] = '\0';
    
    strncpy(s_topic, topic, MQTT_MAX_TOPIC_LEN - 1);
    s_topic[MQTT_MAX_TOPIC_LEN - 1] = '\0';

    /* Create MQTT client task */
    snprintf(taskName, sizeof(taskName), "lan_mqtt_task");
    returnCode = s_osalHandler->TaskCreate(taskName, MQTTClient_Task,
                                           MQTT_CLIENT_TASK_STACK_SIZE, NULL,
                                           &s_mqttTask);
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("lan_mqtt: task create failed: 0x%08X", returnCode);
        if (fn_mosquitto_lib_cleanup) {
            fn_mosquitto_lib_cleanup();
        }
        MQTTClient_UnloadLibrary();
        return returnCode;
    }

    s_isInitialized = true;

    USER_LOG_INFO("lan_mqtt: initialized - broker=%s:%d, clientID=%s, topic=%s",
                  s_brokerAddr, s_brokerPort, s_clientID, s_topic);

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode LAN_MQTTClient_SendData(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    if (!s_isInitialized) {
        USER_LOG_WARN("lan_mqtt: not initialized");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    /* Copy data to buffer for task to send */
    if (len > MQTT_MAX_MESSAGE_LEN) {
        len = MQTT_MAX_MESSAGE_LEN;
        USER_LOG_WARN("lan_mqtt: data truncated to %d bytes", len);
    }
    
    memcpy(s_sendBuffer, data, len);
    s_sendLen = len;

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode LAN_MQTTClient_DeInit(void)
{
    if (!s_isInitialized) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    MQTTClient_Disconnect();
    
    if (fn_mosquitto_lib_cleanup) {
        fn_mosquitto_lib_cleanup();
    }
    MQTTClient_UnloadLibrary();

    s_isInitialized = false;
    s_isConnected = false;

    USER_LOG_INFO("lan_mqtt: deinitialized");

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

bool LAN_MQTTClient_IsConnected(void)
{
    return s_isConnected;
}

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
