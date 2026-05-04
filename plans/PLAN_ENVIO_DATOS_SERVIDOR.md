# Plan de Implementación — Envío de Datos vía DJI FlightHub 2

## Contexto Crítico

1. **Raspberry Pi SIN internet** — solo conectada al drone vía puerto E-Port
2. **Solo DJI Pilot 2 conectado** — no se puede usar MSDK (DJI no permite 2 apps simultáneas)
3. **RC SÍ tiene internet** — a través de la app Pilot 2 que se conecta a FlightHub 2
4. **Objetivo**: Enviar datos de sensores (temp, O2, saturación) desde el RC a un servidor externo

## Arquitectura Final

```
┌──────────────────────────────────────────────────────────────────────┐
│                        ARQUITECTURA FINAL                             │
└──────────────────────────────────────────────────────────────────────┘

  ┌──────────────────┐
  │  Sensor RDO Blue  │  (RS-485 / Modbus RTU)
  └────────┬─────────┘
           │
  ┌────────▼─────────┐                    ┌───────────────────────┐
  │ Raspberry Pi     │                    │  DJI FlightHub 2      │
  │ (SIN INTERNET)   │                    │  (Cloud DJI)          │
  │                  │                    │                       │
  │ sensor_simulation│                    │  - Recibe telemetría   │
  │ .c               │                    │  - Webhooks           │
  │                  │                    │  - API REST           │
  │ Envía datos:     │                    └───────────┬───────────┘
  │ - LowSpeedData   │                                │
  │   → RC (Pilot 2) │                                │ Webhook
  │ - Cloud API      │                                │
  │   → DJI Cloud ───┼────────────────────────────────┘
  └──────────────────┘                    │
                                          ▼
                                   ┌────────────────┐
                                   │ Tu Servidor    │
                                   │ Externo        │
                                   │ (con internet) │
                                   └────────────────┘
```

## Pasos de Implementación

### Paso 1: Configurar DJI FlightHub 2 (Antes de programar)

1. **Crear cuenta en FlightHub 2**
   - Ir a https://flighthub2.dji.com/
   - Registrarse como organizacion

2. **Crear Network Space**
   - Crear un "Network Space" (espacio de red)
   - Este es tu "grupo de drones" en FlightHub 2

3. **Registrar el drone**
   - Agregar el M300/M350 al Network Space
   - El drone debe aparecer online cuando esté conectado a Pilot 2

4. **Conectar DJI Pilot 2 a FlightHub 2**
   - En Pilot 2, iniciar sesión con la misma cuenta de FlightHub 2
   - El drone debe aparecer como conectado en FlightHub 2

5. **Configurar Webhook**
   - En FlightHub 2 → Settings → Webhooks
   - URL: `https://tu-servidor.com/api/dji-webhook`
   - Eventos: `telemetry`, `status`, `alarm`
   - Esto reenviará datos de telemetría a tu servidor

### Paso 2: Modificar `sensor_simulation.c` para usar Cloud API

#### 2.1 Incluir header del Cloud API

```c
#include "dji_cloud_api_by_websockt.h"
```

#### 2.2 Enviar datos a la nube DJI

En la tarea `SensorSim_Task()`, después de enviar al RC, agregar:

```c
// Enviar también a la nube DJI (FlightHub 2)
if (DjiCloudApi_SendDataByWebSocket(
        (const uint8_t *)payload, 
        (uint32_t)len, 
        &realLen) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_DEBUG("sensor sim: sent to DJI Cloud, len=%d", realLen);
} else {
    USER_LOG_WARN("sensor sim: failed to send to DJI Cloud");
}
```

#### 2.3 Inicializar Cloud API en `DjiTest_SensorSimStartService()`

```c
// Inicializar Cloud API
T_DjiCloudApiInitParam cloudApiParam = {
    .transferType = DJI_FIRMWARE_TRANSFER_TYPE_DCFTP,
    // ... otros parámetros
};
T_DjiReturnCode cloudStat = DjiCloudApi_Init(&cloudApiParam);
if (cloudStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
    USER_LOG_WARN("sensor sim: cloud api init failed, continuing without cloud");
}
```

### Paso 3: Configurar `dji_sdk_config.h`

```c
// Habilitar módulo de transmisión de datos (para canal Cloud API)
#define CONFIG_MODULE_SAMPLE_DATA_TRANSMISSION_ON  true

// Mantener sensor simulation activo
#define CONFIG_MODULE_SAMPLE_SENSOR_SIM_ON         true
```

### Paso 4: Integrar en `main.c`

```c
// En main.c, después de inicializar sensor simulation:

#if CONFIG_MODULE_SAMPLE_DATA_TRANSMISSION_ON
    // Inicializar data transmission para habilitar canal Cloud API
    returnCode = DjiTest_DataTransmissionStartService();
    if (returnCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("data transmission init error (cloud may not work)");
    }
#endif
```

### Paso 5: Servidor Externo — Recibir Webhooks de FlightHub 2

Tu servidor debe recibir JSON de FlightHub 2 con formato como:

```json
{
  "event": "telemetry",
  "data": {
    "aircraftId": "...",
    "droneId": "...",
    "telemetry": {
      "position": { "lat": ..., "lon": ..., "alt": ... },
      "battery": { "percentage": ... },
      // ... datos del payload
    }
  }
}
```

Endpoint necesario:
```
POST /api/dji-webhook
Content-Type: application/json

Response: 200 OK
```

---

## Limitaciones y Consideraciones

### Limitación 1: Datos Personalizados en FlightHub 2

FlightHub 2 recibe automáticamente:
- ✅ Posición GPS del drone
- ✅ Estado de batería
- ✅ Estado de conexión
- ⚠️ **Datos personalizados del payload** — dependen de cómo se configuren en el Cloud API

Para que los datos de temperatura y oxígeno lleguen a FlightHub 2, necesitas:
1. Usar `DjiCloudApi_SendDataByWebSocket()` para enviar los datos
2. Configurar los datos como "telemetry" en el Cloud API
3. FlightHub 2 recibirá los datos y los reenviará vía webhook

### Limitación 2: Latencia

El flujo completo tiene latencia:
```
PSDK → RC (instantáneo) → DJI Cloud (1-5s) → Webhook (1-3s) → Servidor
```
**Latencia total estimada: 2-10 segundos**

### Limitación 3: Ancho de Banda

- Cloud API tiene límite de ~1KB por mensaje
- Frecuencia máxima recomendada: 1 mensaje cada 1-5 segundos
- Tus datos JSON son ~150 bytes, así que cabe sin problema

### Limitación 4: Dependencia de Pilot 2

Los datos solo llegan a la nube cuando:
- Pilot 2 está conectado al drone
- Pilot 2 tiene conexión a internet (4G/5G)
- Pilot 2 está conectado a FlightHub 2

Si Pilot 2 se desconecta, los datos dejan de llegar a la nube.

---

## Alternativa: Usar MOP Channel con App Personalizada

Si FlightHub 2 no es suficiente, existe una alternativa:

### Crear una App Android Personalizada con MSDK

En lugar de usar Pilot 2, puedes crear una app Android personalizada que:
1. Se conecte al drone vía MSDK
2. Reciba datos del PSDK
3. Envíe datos a tu servidor

**Pero el usuario mencionó que no puede usar MSDK** porque DJI no permite dos apps conectadas al mismo tiempo.

**Solución**: Usar **solo** la app personalizada (no Pilot 2), y diseñar la UI para control del vuelo + visualización de datos.

---

## Resumen de Viabilidad

| Pregunta | Respuesta |
|----------|-----------|
| ¿Se puede enviar datos sin internet en la Pi? | ✅ Sí, vía RC → FlightHub 2 |
| ¿Se puede usar Cloud API DJI? | ✅ Sí, con `DjiCloudApi_SendDataByWebSocket()` |
| ¿FlightHub 2 reenvía a servidor externo? | ✅ Sí, vía webhooks |
| ¿Latencia aceptable? | ⚠️ 2-10 segundos (depende del uso) |
| ¿Necesario Pilot 2? | ✅ Sí, como puente RC → Cloud |

**Conclusión: SÍ es factible usando DJI FlightHub 2 como puente entre el RC y tu servidor externo.**

---

## Próximos Pasos

1. [ ] **Configurar cuenta en DJI FlightHub 2** (obligatorio)
2. [ ] **Crear Network Space y registrar drone**
3. [ ] **Conectar Pilot 2 a FlightHub 2**
4. [ ] **Implementar Cloud API en sensor_simulation.c**
5. [ ] **Configurar webhook en FlightHub 2**
6. [ ] **Probar flujo completo**: Sensor → Pi → RC → Cloud → Servidor
