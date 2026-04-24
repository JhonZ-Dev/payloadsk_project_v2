# Plan de implementación — Integración de Simulación de Sensores

Objetivo
- Añadir una funcionalidad que envíe lecturas de sensores (temperatura y oxígeno) simuladas desde la Raspberry Pi hacia el control remoto (MSDK) usando las APIs de Data Transmission, sin romper la funcionalidad existente.

Alcance
- Módulo de simulación de sensores en `samples/sample_c/module_sample/data_transmission/`.
- Integración en el flujo de inicio/parada de `samples/sample_c/platform/linux/raspberry_pi/application/main.c`.
- Configuración mediante macro en `samples/sample_c/platform/linux/raspberry_pi/application/dji_sdk_config.h`.

Requisitos
- No modificar la lógica existente de `test_data_transmission.c` (solo ampliar). 
- Usar las APIs: `DjiLowSpeedDataChannel_SendData`, `DjiTest_WidgetLogAppend` y el OSAL para tareas.

Plan de tareas
1. Revisar la API de `test_data_transmission.c` y los callbacks existentes (hecho).
2. Añadir `sensor_simulation.h` y `sensor_simulation.c` que:
   - Cree una tarea OSAL que genere lecturas simuladas periódicas.
   - Arme un payload JSON simple con `temp`, `oxi` y `ts`.
   - Envíe via `MASTER_RC_APP` y también al puerto de payload/extension según `mountPosition`.
   - Escriba mensajes en el widget log (`DjiTest_WidgetLogAppend`) para ser visibles en MSDK/RC.
3. Registrar inicio/parada del servicio en `main.c` protegido por macro `CONFIG_MODULE_SAMPLE_SENSOR_SIM_ON`.
4. Añadir macro `CONFIG_MODULE_SAMPLE_SENSOR_SIM_ON` (default `false`) en `dji_sdk_config.h`.
5. Compilar y probar localmente:
   - Habilitar macro a `true` para pruebas.
   - Compilar (por ejemplo con `cmake` / `make`) y ejecutar en Raspberry o entorno de prueba.
6. Validación en RC/MSDK:
   - Conectar RC o app MSDK al PSDK.
   - Verificar logs/widget y/o adaptar la app MSDK para parsear el JSON si es necesario.

Notas de diseño
- Payload JSON legible facilita debug y pruebas de integración con MSDK. Para producción, considerar formato binario o CBOR.
- Frecuencia por defecto: 2s (configurable dentro del módulo).
- Mantener las funciones y firmas públicas simples: `DjiTest_SensorSimStartService()` / `DjiTest_SensorSimStopService()`.

Comprobaciones posteriores
- Si se desea, agregar una opción para alternar entre datos simulados y datos reales por I2C/ADC cuando se conecte el sensor físico.

Archivo creado por: GitHub Copilot (asistente en repo)
