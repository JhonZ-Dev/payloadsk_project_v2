import minimalmodbus
import serial
import time
import json
import os

PORT = '/dev/serial/by-id/usb-1a86_USB_Single_Serial_5958032059-if00'
SLAVE_ID = 1 
READ_INTERVAL_SECONDS = 1.0
READ_RETRIES = 3
RETRY_DELAY_SECONDS = 0.25

# Archivo compartido con el código C del Payload SDK
SHARED_DATA_FILE = '/tmp/rdo_sensor_data.json'

# Mapa Modbus (base 40001):
# 40038->reg 37 DO Concentration (float)
# 40046->reg 45 Temperature (float)
# 40054->reg 53 DO % Saturation (float)
# 40062->reg 61 Oxygen Partial Pressure (float)
REG_DO_CONC = 37
REG_TEMPERATURE = 45
REG_DO_SAT = 53
REG_O2_PARTIAL_PRESSURE = 61

# Registros de estado/configuración útiles
REG_SENSOR_ID = 0
REG_SENSOR_SERIAL = 1
REG_SENSOR_STATUS = 3
REG_WARMUP_MS = 16
REG_SAMPLE_MS = 17
REG_PARAM_COUNT = 18


def read_sensor_metadata(sensor):
    metadata = {
        "sensor_id": sensor.read_register(REG_SENSOR_ID, number_of_decimals=0, functioncode=3, signed=False),
        "sensor_serial": sensor.read_long(REG_SENSOR_SERIAL, functioncode=3, signed=False, byteorder=minimalmodbus.BYTEORDER_BIG),
        "sensor_status": sensor.read_register(REG_SENSOR_STATUS, number_of_decimals=0, functioncode=3, signed=False),
        "warmup_ms": sensor.read_register(REG_WARMUP_MS, number_of_decimals=0, functioncode=3, signed=False),
        "sample_ms": sensor.read_register(REG_SAMPLE_MS, number_of_decimals=0, functioncode=3, signed=False),
        "parameter_count": sensor.read_register(REG_PARAM_COUNT, number_of_decimals=0, functioncode=3, signed=False),
    }
    return metadata


def read_float_with_retry(sensor, register_address):
    last_error = None
    for _ in range(READ_RETRIES):
        try:
            return sensor.read_float(
                register_address,
                functioncode=3,
                number_of_registers=2,
                byteorder=minimalmodbus.BYTEORDER_BIG,
            )
        except Exception as error:
            last_error = error
            time.sleep(RETRY_DELAY_SECONDS)

    raise last_error

try:
    sensor = minimalmodbus.Instrument(PORT, SLAVE_ID)
    sensor.serial.baudrate = 19200
    sensor.serial.bytesize = 8
    sensor.serial.parity   = serial.PARITY_EVEN
    sensor.serial.stopbits = 1
    
    # SUBE EL TIMEOUT: Algunos adaptadores en Linux son más lentos
    sensor.serial.timeout  = 2.0 
    sensor.mode = minimalmodbus.MODE_RTU
    sensor.clear_buffers_before_each_transaction = True
    sensor.close_port_after_each_call = False

    print(f"Leyendo RDO Blue en {PORT} (Ctrl+C para salir)...")
    try:
        metadata = read_sensor_metadata(sensor)
        print(
            "Sensor: "
            f"ID={metadata['sensor_id']} | "
            f"Serial={metadata['sensor_serial']} | "
            f"Status={metadata['sensor_status']} | "
            f"Warm-up={metadata['warmup_ms']} ms | "
            f"Sample={metadata['sample_ms']} ms | "
            f"N={metadata['parameter_count']}"
        )
    except Exception as metadata_error:
        print(f"Aviso: no se pudo leer metadata al inicio: {metadata_error}")
        print("Continuando con lecturas en vivo...")
    
    # Agregamos una pequeña pausa antes de la primera lectura
    time.sleep(0.5)

    while True:
        try:
            do_mg_l = read_float_with_retry(sensor, REG_DO_CONC)
            temp_c = read_float_with_retry(sensor, REG_TEMPERATURE)
            do_sat = read_float_with_retry(sensor, REG_DO_SAT)
            o2_pp_torr = read_float_with_retry(sensor, REG_O2_PARTIAL_PRESSURE)

            ts = time.strftime('%Y-%m-%d %H:%M:%S')
            print(
                f"[{ts}] DO: {do_mg_l:.2f} mg/L | "
                f"Temp: {temp_c:.2f} °C | "
                f"DO Sat: {do_sat:.2f} % | "
                f"pO2: {o2_pp_torr:.2f} torr"
            )
            
            # Guardar datos en JSON para que el código C del Payload SDK los lea
            data_dict = {
                "timestamp_ms": int(time.time() * 1000),
                "temperature_c": round(temp_c, 2),
                "dissolved_oxygen_mg_l": round(do_mg_l, 2),
                "do_saturation_percent": round(do_sat, 2),
                "oxygen_partial_pressure_torr": round(o2_pp_torr, 2),
            }
            try:
                with open(SHARED_DATA_FILE, 'w') as f:
                    json.dump(data_dict, f)
            except Exception as write_error:
                print(f"[{ts}] Fallo al escribir JSON compartido: {write_error}")

        except Exception as read_error:
            ts = time.strftime('%Y-%m-%d %H:%M:%S')
            print(f"[{ts}] Error de lectura: {read_error}")

        time.sleep(READ_INTERVAL_SECONDS)

except KeyboardInterrupt:
    print("Lectura detenida por el usuario.")
    try:
        sensor.serial.close()
    except Exception:
        pass

except Exception as e:
    print(f"Error: {e}")