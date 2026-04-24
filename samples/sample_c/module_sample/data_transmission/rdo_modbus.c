/**
 ********************************************************************
 * @file    rdo_modbus.c
 * @brief   RDO Blue sensor Modbus RTU reader — pure C using POSIX termios.
 *
 *          Modbus RTU protocol implementation for reading float registers
 *          from an In-Situ RDO Blue dissolved oxygen sensor.
 *
 *          Serial config: 19200 baud, 8 data bits, Even parity, 1 stop bit.
 *          Modbus address: 1 (default for RDO Blue).
 *
 *          Register map (holding registers, function code 0x03):
 *            37  → Dissolved Oxygen concentration (float, 2 regs)
 *            45  → Temperature (float, 2 regs)
 *            53  → DO Saturation % (float, 2 regs)
 *            61  → Oxygen Partial Pressure in torr (float, 2 regs)
 *********************************************************************
 */

#include "rdo_modbus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/select.h>

/* ---------- Configuration ---------- */
#define MODBUS_SLAVE_ID         1
#define MODBUS_FUNC_READ_HOLD   0x03
#define MODBUS_TIMEOUT_MS       2000
#define MODBUS_READ_RETRIES     3
#define MODBUS_RETRY_DELAY_US   250000   /* 250 ms */
#define MODBUS_INTER_FRAME_US   50000    /* 50 ms between requests */

/* RDO Blue register addresses (0-based holding register) */
#define REG_DO_CONC             37
#define REG_TEMPERATURE         45
#define REG_DO_SAT              53
#define REG_O2_PARTIAL_PRESSURE 61

/* ---------- Private state ---------- */
static int s_serialFd = -1;

/* ---------- CRC-16 Modbus ---------- */
static uint16_t ModbusCRC16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* ---------- Serial helpers ---------- */

/**
 * @brief Flush any pending data in the serial buffers.
 */
static void SerialFlush(void)
{
    if (s_serialFd >= 0) {
        tcflush(s_serialFd, TCIOFLUSH);
    }
}

/**
 * @brief Write exactly `len` bytes to the serial port.
 * @return 0 on success, -1 on failure.
 */
static int SerialWrite(const uint8_t *buf, size_t len)
{
    ssize_t written = write(s_serialFd, buf, len);
    if (written < 0 || (size_t)written != len) {
        return -1;
    }
    /* Wait for TX to complete */
    tcdrain(s_serialFd);
    return 0;
}

/**
 * @brief Read up to `maxLen` bytes from serial with timeout.
 * @return Number of bytes read, or -1 on error/timeout.
 */
static int SerialRead(uint8_t *buf, size_t maxLen, int timeoutMs)
{
    fd_set readSet;
    struct timeval tv;
    size_t totalRead = 0;

    while (totalRead < maxLen) {
        FD_ZERO(&readSet);
        FD_SET(s_serialFd, &readSet);

        int remainMs = timeoutMs;
        if (remainMs <= 0) remainMs = 1;
        tv.tv_sec = remainMs / 1000;
        tv.tv_usec = (remainMs % 1000) * 1000;

        int ret = select(s_serialFd + 1, &readSet, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ret == 0) {
            /* Timeout — return what we have so far */
            break;
        }

        ssize_t n = read(s_serialFd, buf + totalRead, maxLen - totalRead);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        totalRead += (size_t)n;

        /* For Modbus RTU we know the expected response length,
           so if we got enough bytes, break early */
    }

    return (int)totalRead;
}

/* ---------- Modbus RTU transaction ---------- */

/**
 * @brief Send Modbus read holding registers request and parse response.
 *        Reads `numRegs` 16-bit registers starting at `startReg`.
 * @param startReg: 0-based register address
 * @param numRegs: number of 16-bit registers to read
 * @param regsOut: output buffer for register values (host byte order)
 * @return 0 on success, -1 on failure
 */
static int ModbusReadRegisters(uint16_t startReg, uint16_t numRegs, uint16_t *regsOut)
{
    /* Build request frame:
       [SlaveID(1)] [FuncCode(1)] [StartRegHi(1)] [StartRegLo(1)] [NumRegsHi(1)] [NumRegsLo(1)] [CRC_Lo(1)] [CRC_Hi(1)]
    */
    uint8_t request[8];
    request[0] = MODBUS_SLAVE_ID;
    request[1] = MODBUS_FUNC_READ_HOLD;
    request[2] = (uint8_t)(startReg >> 8);
    request[3] = (uint8_t)(startReg & 0xFF);
    request[4] = (uint8_t)(numRegs >> 8);
    request[5] = (uint8_t)(numRegs & 0xFF);
    uint16_t crc = ModbusCRC16(request, 6);
    request[6] = (uint8_t)(crc & 0xFF);        /* CRC low byte first */
    request[7] = (uint8_t)((crc >> 8) & 0xFF);

    /* Expected response: [SlaveID(1)] [FuncCode(1)] [ByteCount(1)] [Data(numRegs*2)] [CRC_Lo(1)] [CRC_Hi(1)] */
    size_t expectedLen = 3 + (size_t)(numRegs * 2) + 2;

    SerialFlush();

    if (SerialWrite(request, 8) != 0) {
        return -1;
    }

    uint8_t response[64];
    if (expectedLen > sizeof(response)) {
        return -1;
    }

    int bytesRead = SerialRead(response, expectedLen, MODBUS_TIMEOUT_MS);
    if (bytesRead < (int)expectedLen) {
        return -1;
    }

    /* Validate response */
    if (response[0] != MODBUS_SLAVE_ID) {
        return -1;
    }

    /* Check for exception response */
    if (response[1] & 0x80) {
        return -1;
    }

    if (response[1] != MODBUS_FUNC_READ_HOLD) {
        return -1;
    }

    uint8_t byteCount = response[2];
    if (byteCount != numRegs * 2) {
        return -1;
    }

    /* Verify CRC */
    uint16_t respCrc = ModbusCRC16(response, (uint16_t)(3 + byteCount));
    uint16_t recvCrc = (uint16_t)(response[3 + byteCount]) |
                       ((uint16_t)(response[4 + byteCount]) << 8);
    if (respCrc != recvCrc) {
        return -1;
    }

    /* Extract register values (big-endian in Modbus) */
    for (uint16_t i = 0; i < numRegs; i++) {
        uint16_t hi = response[3 + i * 2];
        uint16_t lo = response[3 + i * 2 + 1];
        regsOut[i] = (hi << 8) | lo;
    }

    return 0;
}

/**
 * @brief Read a 32-bit IEEE 754 float from two consecutive Modbus registers.
 *        RDO Blue uses big-endian byte order (high register first).
 */
static int ModbusReadFloat(uint16_t startReg, float *value)
{
    uint16_t regs[2];
    if (ModbusReadRegisters(startReg, 2, regs) != 0) {
        return -1;
    }

    /* Big-endian: regs[0] = high word, regs[1] = low word */
    uint32_t raw = ((uint32_t)regs[0] << 16) | (uint32_t)regs[1];
    memcpy(value, &raw, sizeof(float));

    return 0;
}

/**
 * @brief Read a float with retry logic (same as Python read_float_with_retry).
 */
static int ModbusReadFloatRetry(uint16_t startReg, float *value)
{
    for (int attempt = 0; attempt < MODBUS_READ_RETRIES; attempt++) {
        if (ModbusReadFloat(startReg, value) == 0) {
            return 0;
        }
        usleep(MODBUS_RETRY_DELAY_US);
    }
    return -1;
}

/* ---------- Public API ---------- */

int RdoModbus_Init(const char *portPath)
{
    if (s_serialFd >= 0) {
        close(s_serialFd);
        s_serialFd = -1;
    }

    s_serialFd = open(portPath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (s_serialFd < 0) {
        fprintf(stderr, "[RdoModbus] Failed to open %s: %s\n", portPath, strerror(errno));
        return -1;
    }

    /* Clear non-blocking after open */
    int flags = fcntl(s_serialFd, F_GETFL, 0);
    fcntl(s_serialFd, F_SETFL, flags & ~O_NONBLOCK);

    /* Get exclusive access */
    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(s_serialFd, &tty) != 0) {
        fprintf(stderr, "[RdoModbus] tcgetattr error: %s\n", strerror(errno));
        close(s_serialFd);
        s_serialFd = -1;
        return -1;
    }

    /* 19200 baud */
    cfsetispeed(&tty, B19200);
    cfsetospeed(&tty, B19200);

    /* 8 data bits */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    /* Even parity */
    tty.c_cflag |= PARENB;
    tty.c_cflag &= ~PARODD;

    /* 1 stop bit */
    tty.c_cflag &= ~CSTOPB;

    /* No hardware flow control */
    tty.c_cflag &= ~CRTSCTS;

    /* Enable receiver, local mode */
    tty.c_cflag |= (CLOCAL | CREAD);

    /* Raw mode — no canonical, no echo, no signals */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;

    /* Read timeout settings */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 20; /* 2 second timeout in tenths of a second */

    if (tcsetattr(s_serialFd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "[RdoModbus] tcsetattr error: %s\n", strerror(errno));
        close(s_serialFd);
        s_serialFd = -1;
        return -1;
    }

    /* Flush buffers */
    SerialFlush();

    fprintf(stdout, "[RdoModbus] Opened %s at 19200 8E1\n", portPath);
    return 0;
}

void RdoModbus_DeInit(void)
{
    if (s_serialFd >= 0) {
        close(s_serialFd);
        s_serialFd = -1;
    }
}

int RdoModbus_ReadSensorData(T_RdoSensorData *data)
{
    if (s_serialFd < 0 || data == NULL) {
        return -1;
    }

    memset(data, 0, sizeof(T_RdoSensorData));

    /* Read 4 floats with inter-frame delay between each request */
    if (ModbusReadFloatRetry(REG_DO_CONC, &data->dissolved_oxygen_mg_l) != 0) {
        return -1;
    }
    usleep(MODBUS_INTER_FRAME_US);

    if (ModbusReadFloatRetry(REG_TEMPERATURE, &data->temperature_c) != 0) {
        return -1;
    }
    usleep(MODBUS_INTER_FRAME_US);

    if (ModbusReadFloatRetry(REG_DO_SAT, &data->do_saturation_percent) != 0) {
        return -1;
    }
    usleep(MODBUS_INTER_FRAME_US);

    if (ModbusReadFloatRetry(REG_O2_PARTIAL_PRESSURE, &data->oxygen_partial_pressure_torr) != 0) {
        return -1;
    }

    /* Timestamp */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    data->timestamp_ms = (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
    data->valid = 1;

    return 0;
}
