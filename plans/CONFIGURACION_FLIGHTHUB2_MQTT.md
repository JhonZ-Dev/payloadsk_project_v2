# Configuración FlightHub 2 y Cloud API para envío de datos a MQTT

## Problema actual
El error `0x000000EE` indica que `DjiCloudApi_SendDataByWebSocket()` no puede establecer conexión con los servidores de DJI.

## Arquitectura requerida

```
Pi (sin internet) 
  → E-Port/MSDK 
    → RC M400 (con internet) 
      → DJI Cloud (FlightHub 2) 
        → Cloud API Platform (MQTT Forwarding) 
          → MQTT Broker (172.16.10.136:1883) 
            → Backend Java (PsdkDataTransmissionService.java)
```

## Pasos para configurar FlightHub 2

### Paso 1: Registrar el producto en DJI Developer Portal

1. Ir a [DJI Developer Portal](https://developer.dji.com/)
2. Ir a "My Apps" → "Register App"
3. Crear una app con:
   - App Name: tu nombre de app
   - App Description: descripción
   - App Type: Payload / SDK
4. Anotar el **App ID** y **App Key**

### Paso 2: Crear proyecto en FlightHub 2

1. Ir a [DJI FlightHub 2](https://fh2.dji.com/)
2. Iniciar sesión con la cuenta de developer
3. Crear un nuevo proyecto
4. Asociar el producto (App ID) creado en el Paso 1

### Paso 3: Configurar Cloud API Platform

1. Ir al Cloud API Platform de tu cuenta
2. Seleccionar el producto
3. Ir a "Reenvío de datos de telemetría (MQTT)"
4. **ACTIVAR el toggle** de "Servicio de reenvío"
5. Hacer clic en "Configurar servidor" y llenar:

| Campo | Valor |
|-------|-------|
| Dirección del servidor | `172.16.10.136` |
| Puerto | `1883` |
| Nombre de usuario | (si requiere) |
| Contraseña | (si requiere) |

6. Guardar la configuración

### Paso 4: Verificar en DJI Pilot 2

1. Abrir DJI Pilot 2 en la RC
2. Ir a Settings → Cloud API
3. Verificar que esté conectado al FlightHub 2
4. Verificar que el estado sea "Connected"

### Paso 5: Verificar la app en la RC

Si tu app está corriendo en la RC (no en un Pi sin internet):

1. La app debe estar registrada como una "Payload App" en DJI Pilot 2
2. La app debe estar activa y conectada
3. El estado de conexión debe mostrar "Connected"

## Código actual del PSDK

El código ya está configurado correctamente para:

1. **Low Speed Data Channel** - Envía datos al RC (funciona)
2. **Cloud API WebSocket** - Intenta enviar a DJI Cloud (falla con 0x000000EE)

El problema NO está en el código, sino en la infraestructura cloud.

## Debugging del error 0x000000EE

Este error significa que el WebSocket no se pudo establecer. Causas posibles:

1. **RC sin internet** - Verificar que la RC tenga conexión WiFi activa
2. **FlightHub 2 no conectado** - Verificar en DJI Pilot 2 que el estado sea "Connected"
3. **Producto no asociado** - Verificar que el App ID esté asociado al proyecto FlightHub 2
4. **Firewall bloqueando WebSocket** - Verificar que los puertos 443/80 estén abiertos

## Alternativa: App Android personalizada

Si no puedes configurar FlightHub 2, necesitas crear una app Android que:

1. Se ejecute en la RC (o en un tablet conectado)
2. Se conecte al PSDK vía MOP Channel
3. Reciba los datos del sensor
4. Los publique al MQTT broker

### Código para enviar datos vía MOP Channel

El código actual en `sensor_simulation.c` puede modificarse para usar MOP Channel en lugar de Cloud API:

```c
// En lugar de:
T_DjiReturnCode cloudStat = DjiCloudApi_SendDataByWebSocket(...);

// Usar:
T_DjiReturnCode mopStat = DjiMopChannel_SendData(channelHandle, data, len, &realLen);
```

Esto requiere:
1. Crear un MOP Channel con `DjiMopChannel_Create()`
2. Bind al channel ID con `DjiMopChannel_Bind()`
3. Accept la conexión con `DjiMopChannel_Accept()`
4. Enviar datos con `DjiMopChannel_SendData()`

La app Android debe tener un servicio que:
1. Escuche en el MOP Channel
2. Reciba los datos
3. Los publique al MQTT broker

## Próximos pasos

1. **Verificar FlightHub 2**: Confirmar que está configurado y conectado
2. **Verificar RC internet**: Confirmar que la RC tiene acceso a internet
3. **Activar toggle MQTT**: En Cloud API Platform, activar el "Servicio de reenvío"
4. **Probar de nuevo**: Desplegar el código y verificar que no haya error 0x000000EE

## Resumen

| Componente | Estado requerido |
|------------|-----------------|
| Pi | Sin internet (solo E-Port) ✓ |
| RC M400 | Con internet a DJI servers |
| FlightHub 2 | Proyecto creado y asociado |
| Cloud API Platform | MQTT Forwarding activado |
| DJI Pilot 2 | Conectado a FlightHub 2 |
| Backend Java | Escuchando MQTT topic |
