# Arquitectura del Proyecto DJI Payload SDK (PSDK) v3.15.0

## 1. Resumen General

Este proyecto es una implementación de **DJI Payload SDK (PSDK)** versión 3.15.0, un kit de desarrollo que permite crear payloads (dispositivos adicionales) montables en drones DJI como la serie Matrice (M300_RTK, M350_RTK, M30, M300, etc.). El proyecto está configurado actualmente para ejecutarse en una **Raspberry Pi** como plataforma de procesamiento del payload, conectándose al drone a través del puerto E-Port/SkyPort/X-Port.

**Propósito actual del proyecto**: El proyecto implementa un payload llamado **"Geo"** (ID: 177409) que incluye:
- Simulación de sensores (temperatura y oxígeno) transmitidos vía Data Transmission
- Emulación de gimbal PSDK
- Gestión de energía (Power Management)
- Widget interaction (interfaz en DJI Pilot)
- Soporte para speaker

---

## 2. Estructura de Directorios

```
payloadsk_project_v2/
├── CMakeLists.txt                    # Punto de entrada principal del build
├── README.md                         # Documentación general
├── PLAN_IMPLEMENTACION.md            # Plan de implementación (en español)
├── pruebas.py                        # Script de pruebas Python
├── test_integration.sh               # Script de integración
├── psdk_lib/                         # Biblioteca DJI PSDK precompilada
│   ├── include/                      # Headers de la PSDK (~48 módulos)
│   └── lib/                          # Libs precompiladas por arquitectura
│       ├── x86_64-linux-gnu-gcc/
│       ├── aarch64-linux-gnu-gcc/
│       ├── arm-linux-gnueabihf-gcc/
│       ├── arm-linux-gnueabi-gcc/
│       └── armcc_cortex-m4/
├── samples/
│   ├── sample_c/                     # Implementaciones en C
│   │   ├── module_sample/            # Módulos de funcionalidad PSDK
│   │   └── platform/                 # Adaptadores por plataforma
│   └── sample_c++/                   # Implementaciones en C++
│       ├── module_sample/
│       └── platform/
├── doc/                              # Documentación de referencia
└── tools/                            # Herramientas auxiliares
```

---

## 3. Arquitectura de Capas

```
┌─────────────────────────────────────────────────────────┐
│              Aplicación DJI (main.c)                     │
│  - Inicialización de módulos (configurados por macros)  │
│  - Bucle principal + Task de monitorización             │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│         DJI Payload SDK Core (dji_core.h)               │
│  - DjiCore_Init(), DjiCore_ApplicationStart()           │
│  - Gestión del ciclo de vida del payload                │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│         Módulos de Funcionalidad (module_sample/)       │
│  - Camera, Gimbal, Flight Controller, LiveView          │
│  - Data Transmission, Widget, Cloud, HMS                │
│  - Power Management, Upgrade, Waypoint, etc.            │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│         Capa de Abstracción de Plataforma (HAL/OSAL)    │
│  - UART, USB Bulk, Network (TCP/IP)                     │
│  - File System, Memory, Timers, Tasks                   │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│         Hardware                                          │
│  - Raspberry Pi / Manifold / STM32F4                     │
│  - Conectado al drone vía E-Port / SkyPort / X-Port     │
└─────────────────────────────────────────────────────────┘
```

---

## 4. Plataformas Soportadas

### 4.1 Linux (Sistema Operativo)

| Plataforma | Ubicación | Arquitectura |
|------------|-----------|--------------|
| **Raspberry Pi** | `platform/linux/raspberry_pi/` | ARM (aarch64/armhf) |
| **Manifold 2** | `platform/linux/manifold2/` | x86_64 |
| **Manifold 3** | `platform/linux/manifold3/` | x86_64 |
| **NVIDIA Jetson** | `platform/linux/nvidia_jetson/` | aarch64 |

### 4.2 RTOS (FreeRTOS)

| Plataforma | MCU | Herramienta |
|------------|-----|-------------|
| STM32F4 Discovery | STM32F407VG | ARM GCC / Keil MDK |
| GD32F527 Board | GD32F527 | ARM GCC / Keil MDK |

---

## 5. Módulos de Funcionalidad PSDK

El proyecto `sample_c` incluye los siguientes módulos en [`module_sample/`](samples/sample_c/module_sample/):

### 5.1 Módulos Principalmente Activos (configurados en dji_sdk_config.h)

| Módulo | Archivo Principal | Función |
|--------|-------------------|---------|
| **Power Management** | `power_management/` | Gestión de energía del payload |
| **Gimbal Emulation** | `gimbal_emu/` | Emulación de gimbal controlable por RC |
| **Widget** | `widget/` | Interfaz personalizada en DJI Pilot |
| **Sensor Simulation** | `data_transmission/sensor_simulation.c` | Envío de datos simulados (temp/oxígeno) |

### 5.2 Módulos Disponibles (desactivados)

| Módulo | Macro de Habilitación | Función |
|--------|----------------------|---------|
| Data Transmission | `CONFIG_MODULE_SAMPLE_DATA_TRANSMISSION_ON` | Transmisión de datos en tiempo real |
| Widget Speaker | `CONFIG_MODULE_SAMPLE_WIDGET_SPEAKER_ON` | Altavoz/altavoz integrado |
| Camera Emulation | `CONFIG_MODULE_SAMPLE_CAMERA_EMU_ON` | Emulación de cámara |
| Camera Media | `CONFIG_MODULE_SAMPLE_CAMERA_MEDIA_ON` | Gestión de archivos multimedia |
| FC Subscription | `CONFIG_MODULE_SAMPLE_FC_SUBSCRIPTION_ON` | Suscripción a datos de FC |
| XPort | `CONFIG_MODULE_SAMPLE_XPORT_ON` | Soporte X-Port |
| Payload Collaboration | `CONFIG_MODULE_SAMPLE_PAYLOAD_COLLABORATION_ON` | Colaboración payload-drone |
| Upgrade | `CONFIG_MODULE_SAMPLE_UPGRADE_ON` | Actualización de firmware remota |
| HMS Customization | `CONFIG_MODULE_SAMPLE_HMS_CUSTOMIZATION_ON` | Health Management System |
| MOP Channel | `CONFIG_MODULE_SAMPLE_MOP_CHANNEL_ON` | Canal de comunicación MSDK |
| Tethered Battery | `CONFIG_MODULE_SAMPLE_TETHERED_BATTERY_ON` | Batería conectada |
| Time Sync | `CONFIG_MODULE_SAMPLE_TIME_SYNC_ON` | Sincronización de tiempo PPS |
| Positioning | `CONFIG_MODULE_SAMPLE_POSITIONING_ON` | Posicionamiento GPS externo |

---

## 6. Flujo de Ejecución (main.c)

El flujo principal en [`main.c`](samples/sample_c/platform/linux/raspberry_pi/application/main.c:125) es:

```
1. Preparar entorno del sistema
   ├── Inicializar OSAL (task, mutex, semaphore, memory)
   ├── Configurar Logger (consola + archivo)
   ├── Inicializar HAL (UART, USB/Network)
   └── Crear task de monitorización del sistema

2. Configurar información de la aplicación
   ├── DjiCore_SetFirmwareVersion()
   ├── DjiCore_SetSerialNumber()
   └── DjiCore_SetAlias()

3. Inicializar núcleo PSDK
   └── DjiCore_Init(&userInfo)

4. Inicializar Widget Manager
   └── DjiWidgetManager_Init()

5. Inicializar módulos (según CONFIG_MODULE_SAMPLE_*_ON)
   ├── Power Management
   ├── Data Transmission
   ├── Widget
   ├── Sensor Simulation
   ├── Widget Speaker
   ├── Camera Emulation (si no es extension port)
   ├── Camera Media
   ├── FC Subscription
   ├── Gimbal Emulation
   ├── XPort
   ├── Payload Collaboration
   ├── MOP Channel
   ├── Upgrade
   ├── HMS Customization
   ├── HMS Manager (siempre activo)
   ├── Tethered Battery
   ├── Time Sync
   └── Positioning

6. Notificar a DJI Pilot que está listo
   └── DjiCore_ApplicationStart()

7. Bucle principal (sleep 1s) + monitor task
```

---

## 7. Sistema de Build (CMake)

### 7.1 Root CMakeLists.txt

El CMake raíz en [`CMakeLists.txt`](CMakeLists.txt) selecciona la arquitectura:

```cmake
set(USE_SYSTEM_ARCH LINUX)  # o RTOS

# LINUX → Raspberry Pi (sample_c + sample_c++)
# RTOS → STM32F4 Discovery (sample_c)
```

### 7.2 Raspberry Pi CMakeLists.txt

El build en [`platform/linux/raspberry_pi/CMakeLists.txt`](samples/sample_c/platform/linux/raspberry_pi/CMakeLists.txt):

1. Detecta arquitectura con `uname -m` (x86_64 o aarch64)
2. Recopila fuentes de:
   - `application/*.c` - Código de la app
   - `hal/*.c` - Hardware abstraction
   - `../../../module_sample/*.c` - Todos los módulos PSDK
   - `../common/*.c` - Código compartido
3. Link con `libpayloadsdk.a` de la arquitectura correspondiente
4. Busca dependencias: OPUS, LIBUSB

---

## 8. Configuración de la Aplicación

### 8.1 Información de la App (dji_sdk_app_info.h)

| Campo | Valor Actual |
|-------|-------------|
| Nombre | Geo |
| App ID | 177409 |
| Key | d097535f8aac4dd629e1f3b4e909856 |
| License | (base64, firmado) |
| Developer | proyectosidipsp@gmail.com |
| Baud Rate | 460800 |

### 8.2 Configuración de Módulos (dji_sdk_config.h)

```c
#define CONFIG_HARDWARE_CONNECTION DJI_USE_ONLY_USB_BULK_DEVICE

#define CONFIG_MODULE_SAMPLE_POWER_MANAGEMENT_ON    true   // ACTIVO
#define CONFIG_MODULE_SAMPLE_DATA_TRANSMISSION_ON   false
#define CONFIG_MODULE_SAMPLE_WIDGET_ON              true   // ACTIVO
#define CONFIG_MODULE_SAMPLE_SENSOR_SIM_ON          true   // ACTIVO
#define CONFIG_MODULE_SAMPLE_WIDGET_SPEAKER_ON      false
#define CONFIG_MODULE_SAMPLE_GIMBAL_EMU_ON          true   // ACTIVO
// ... (otros módulos desactivados)
```

---

## 9. Módulos de Simulación de Sensores

### 9.1 Arquitectura del Módulo

```
sensor_simulation.h / sensor_simulation.c
│
├── DjiTest_SensorSimStartService()
│   ├── Crea tarea OSAL periódica
│   └── Genera JSON: {"temp": XX.X, "oxi": XX.X, "ts": 1234567890}
│
├── DjiTest_SensorSimStopService()
│   └── Destruye tarea y limpia estado
│
└── Envío vía:
    ├── DjiLowSpeedDataChannel_SendData()
    └── DjiTest_WidgetLogAppend() (logs visibles en DJI Pilot)
```

### 9.2 Integración en main.c

```c
#if CONFIG_MODULE_SAMPLE_SENSOR_SIM_ON
    returnCode = DjiTest_SensorSimStartService();
    // ...
#endif
```

---

## 10. Capa HAL (Hardware Abstraction Layer)

### 10.1 Raspberry Pi

| Archivo | Función |
|---------|---------|
| `hal_uart.c/h` | Comunicación serial con la aeronave |
| `hal_usb_bulk.c/h` | Transferencia USB bulk (alternativa a UART) |
| `hal_i2c.c/h` | Bus I2C para sensores externos |
| `hal_network.c/h` | Comunicación de red (TCP/IP) |

### 10.2 OSAL (Operating System Abstraction Layer)

Ubicado en `platform/linux/common/osal/`:

| Archivo | Función |
|---------|---------|
| `osal.c/h` | TaskCreate, TaskDestroy, TaskSleepMs, Mutex, Semaphore |
| `osal_fs.c/h` | Operaciones de sistema de archivos |
| `osal_socket.c/h` | Sockets de red |

---

## 11. Diagrama de Flujo de Datos

```
┌──────────────────────────────────────────────────────────────┐
│                    Drone DJI (Aeronave)                       │
│  ┌──────────┐    ┌──────────┐    ┌────────────────────────┐  │
│  │  DJI Pilot│    │  Flight  │    │  Mobile SDK (MSDK)    │  │
│  │  (RC App) │    │ Controller│    │  (App externa)        │  │
│  └─────┬─────┘    └─────┬─────┘    └──────────┬───────────┘  │
│        │                │                     │              │
│        │  E-Port/SkyPort│                     │  Internet    │
│        │  USB/Network   │                     │              │
│        └────────┬───────┘                     │              │
└─────────────────┼─────────────────────────────┘──────────────┘
                  │
          ┌───────▼────────┐
          │  Payload PSDK  │
          │  (Raspberry Pi)│
          └───┬────────────┘
              │
    ┌─────────┼──────────────────────────────────┐
    │         │                                  │
    ▼         ▼                                  ▼
┌────────┐ ┌────────────┐              ┌────────────────┐
│ UART/  │ │ Módulo de  │              │ Nube / WebSockt│
│ USB    │ │ Sensor     │              │ (Cloud API)    │
│        │ │ Simulado   │              │                │
│ Envía  │ │ (temp/ox)  │              │ Telemetría     │
│ datos  │ │ → RC App   │              │ remota         │
└────────┘ └────────────┘              └────────────────┘
```

---

## 12. Biblioteca PSDK (psdk_lib/include)

La biblioteca proporciona ~48 módulos API:

### Núcleo
- [`dji_core.h`](psdk_lib/include/dji_core.h) - Inicialización y ciclo de vida
- [`dji_error.h`](psdk_lib/include/dji_error.h) - Código de errores
- [`dji_typedef.h`](psdk_lib/include/dji_typedef.h) - Tipos de datos
- [`dji_platform.h`](psdk_lib/include/dji_platform.h) - Abstracción de plataforma
- [`dji_logger.h`](psdk_lib/include/dji_logger.h) - Sistema de logging

### Control de Vuelo y Sensores
- [`dji_flight_controller.h`](psdk_lib/include/dji_flight_controller.h) - Control del drone
- [`dji_fc_subscription.h`](psdk_lib/include/dji_fc_subscription.h) - Suscripción a datos FC
- [`dji_positioning.h`](psdk_lib/include/dji_positioning.h) - Posicionamiento GPS
- [`dji_time_sync.h`](psdk_lib/include/dji_time_sync.h) - Sincronización PPS

### Cámara y Gimbal
- [`dji_camera_manager.h`](psdk_lib/include/dji_camera_manager.h) - Gestión de cámara
- [`dji_payload_camera.h`](psdk_lib/include/dji_payload_camera.h) - Cámara payload
- [`dji_gimbal_manager.h`](psdk_lib/include/dji_gimbal_manager.h) - Control de gimbal
- [`dji_gimbal.h`](psdk_lib/include/dji_gimbal.h) - Gimbal API

### Comunicación
- [`dji_liveview.h`](psdk_lib/include/dji_liveview.h) - Video en vivo
- [`dji_low_speed_data_channel.h`](psdk_lib/include/dji_low_speed_data_channel.h) - Data transmission
- [`dji_high_speed_data_channel.h`](psdk_lib/include/dji_high_speed_data_channel.h) - High speed data
- [`dji_mop_channel.h`](psdk_lib/include/dji_mop_channel.h) - MOP channel
- [`dji_cloud_api_by_websockt.h`](psdk_lib/include/dji_cloud_api_by_websockt.h) - Cloud API

### Interfaz de Usuario
- [`dji_widget.h`](psdk_lib/include/dji_widget.h) - Widgets personalizados
- [`dji_widget_manager.h`](psdk_lib/include/dji_widget_manager.h) - Widget manager
- [`dji_xport.h`](psdk_lib/include/dji_xport.h) - X-Port control

### Gestión y Mantenimiento
- [`dji_hms_manager.h`](psdk_lib/include/dji_hms_manager.h) - Health management
- [`dji_upgrade.h`](psdk_lib/include/dji_upgrade.h) - Firmware upgrade
- [`dji_power_management.h`](psdk_lib/include/dji_power_management.h) - Power management
- [`dji_aircraft_info.h`](psdk_lib/include/dji_aircraft_info.h) - Info de aeronave

### Waypoint y Misiones
- [`dji_waypoint_v2.h`](psdk_lib/include/dji_waypoint_v2.h) - Waypoint mission v2
- [`dji_waypoint_v3.h`](psdk_lib/include/dji_waypoint_v3.h) - Waypoint mission v3
- [`dji_interest_point.h`](psdk_lib/include/dji_interest_point.h) - Point of interest

---

## 13. Versiones de C/C++

| Carpeta | Lenguaje | Descripción |
|---------|----------|-------------|
| `sample_c/` | C (C99) | Implementación principal en C |
| `sample_c++/` | C++ (C++11) | Implementación en C++ con características avanzadas (YOLO, TensorFlow) |

---

## 14. Dependencias del Sistema

| Dependencia | Propósito |
|-------------|-----------|
| **OPUS** | Codificación de audio (widget speaker) |
| **LIBUSB** | Comunicación USB bulk |
| **pthread** | Multi-threading (Linux) |
| **libpayloadsdk.a** | Biblioteca DJI PSDK principal |

---

## 15. Notas de Diseño

1. **Modularidad**: Cada módulo PSDK se activa/desactiva mediante macro en `dji_sdk_config.h`, permitiendo builds ligeros o completos.

2. **Multi-plataforma**: El mismo código de módulo puede compilarse para Linux (Raspberry Pi, Manifold) o RTOS (STM32) con los adaptadores HAL apropiados.

3. **Separación de concerns**: 
   - `module_sample/` - Lógica de negocio PSDK
   - `hal/` - Abstracción de hardware
   - `osal/` - Abstracción de sistema operativo
   - `application/` - Punto de entrada y configuración

4. **HMS siempre activo**: El Health Management System se inicializa independientemente de la configuración de HMS customization para monitoreo de errores.

5. **MontPosition-aware**: Algunas funcionalidades (camera, gimbal) se deshabilitan automáticamente si el payload está en extension port (E-Port Lite del M300/M350).

---

*Documento generado como parte del análisis de arquitectura del proyecto DJI PSDK v3.15.0*
