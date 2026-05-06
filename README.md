# Magnetic Length Encoder RS485 — Firmware V0.1

Firmware pembaca panjang berbasis ESP32 + sensor magnetic encoder AS5600, berkomunikasi dengan master device/PLC via RS485.

Implementasi mengikuti dokumen [`protokol.txt`](protokol.txt).

---

## Hardware

| Komponen | Detail |
|----------|--------|
| MCU | ESP32 (dev / C3 / S3) |
| Sensor | AS5600 (I2C magnetic encoder) |
| Komunikasi | RS485 half-duplex, 230400 baud |
| Library | robtillaart/AS5600 v0.6.3 |

**Pin Assignment:**

| Fungsi | ESP32dev | ESP32-C3 |
|--------|----------|----------|
| RS485 RX (RO) | GPIO 13 | GPIO 13 |
| RS485 TX (DI) | GPIO 14 | GPIO 14 |
| RS485 EN (DE+RE) | Auto (hardware) | Auto (hardware) |
| I2C SDA | GPIO 21 | GPIO 8 |
| I2C SCL | GPIO 22 | GPIO 9 |

---

## Arsitektur Software

```
src/
├── main.cpp      — setup/loop: inisialisasi, dispatch paket
├── rs485.cpp     — driver RS485: read, send, parsing, CRC check
├── crc.cpp       — CRC16-Modbus (poly 0xA001, init 0xFFFF)
├── encoder.cpp   — baca AS5600, kalkulasi (sumber data sensor)
└── command.cpp   — handler perintah: measurement dan control
```

---

## Status Saat Ini

Measurement `CMD 0x00` membaca posisi kumulatif AS5600, menghitung delta dari titik tare, mengubahnya ke derajat (angle adjust), lalu mengonversinya ke panjang melalui fungsi kalibrasi `getDistance()` di [`encoder.cpp`](src/encoder.cpp). Reply mengirim **dua nilai**: jarak (`cm × 100`) dan sudut adjust (`deg × 100`).

| CMD | Handler | Keterangan |
|-----|---------|-------------|
| `0x00` Measurement | `handleMeasurement()` | Jarak (cm×100) + angle adjust (deg×100) dari AS5600 |
| `0x02` Control | `handleMode()` | (action langsung; ACK tetap dikirim) |

---

## Protokol RS485

### Frame Structure

```
┌─────────┬─────────┬────────┬──────┬─────────┬─────────┬──────────┬─────────┐
│ Header1 │ Header2 │ Length │ REQ  │ Address │ Command │   Data   │   CRC   │
│  0xD5   │  0xAA   │ 1 byte │ 0x88 │  0x06   │ 1 byte  │ Variable │ 2 bytes │
└─────────┴─────────┴────────┴──────┴─────────┴─────────┴──────────┴─────────┘
```

| Field | Ukuran | Keterangan |
|-------|--------|------------|
| Header | 2 byte | `0xD5 0xAA` — fixed |
| Length | 1 byte | `Length = 3 (REQ+ADDR+CMD) + Data_Length` |
| REQ | 1 byte | `0x88` = request (master → slave), `0x89` = response (slave → master) |
| Address | 1 byte | `0x06` — slave address. Hanya frame dengan address `0x06` yang diproses |
| Command | 1 byte | `0x00` measurement, `0x02` control |
| Data | n byte | Tergantung command type |
| CRC | 2 byte | Modbus CRC-16 (poly `0xA001`, init `0xFFFF`), big-endian (MSB first) |

---

## Command Set

### CMD `0x00` — Length Measurement

**Request (Master → Slave):**

```
D5 AA 04 88 06 00 00 [CRC_H] [CRC_L]
```

Byte setelah `CMD` adalah reserved dan harus `0x00`.

**Response (Slave → Master):**

```
D5 AA 0A 89 06 00 00 [DIST_H DIST_M DIST_L] [ANGLE_H ANGLE_M ANGLE_L] [CRC_H] [CRC_L]
```

| Field | Ukuran | Keterangan |
|-------|--------|------------|
| Length | 1 byte | `0x0A` (10 bytes: REQ+ADDR+CMD+reserved+3 byte dist+3 byte angle) |
| Reserved | 1 byte | `0x00` — fixed, setelah CMD sebelum data |
| DIST | 3 byte | Jarak/panjang (`cm × 100`) — 24-bit signed integer big-endian |
| ANGLE | 3 byte | Sudut adjust (`deg × 100`) — 24-bit signed integer big-endian |

**Decoding di sisi master:**

```c
int32_t length_cm_x100  = (int32_t)((DIST_H  << 16) | (DIST_M  << 8) | DIST_L);
float   length_cm       = length_cm_x100 / 100.0f;

int32_t angle_deg_x100  = (int32_t)((ANGLE_H << 16) | (ANGLE_M << 8) | ANGLE_L);
float   angle_deg       = angle_deg_x100 / 100.0f;
```

Contoh: panjang = 8.28 cm → encoded = 828 (`0x00033C`) → bytes = `00 03 3C`

Contoh: angle adjust = 220.09° → encoded = 22009 (`0x0055F9`) → bytes = `00 55 F9`

**Syarat:** State harus `Operation`. Jika `Standby`, encoder membalas Error.

---

### CMD `0x02` — Control Commands

**Request (Master → Slave):**

```
D5 AA 04 88 06 02 [SUBCMD] [CRC_H] [CRC_L]
```

| SUBCMD | Command | Description |
|--------|---------|-------------|
| `0x01` | Standby | Enter low power mode (WiFi/BT off, CPU 80MHz) |
| `0x02` | Operation | Exit standby / ready |
| `0x03` | Tare | Set posisi AS5600 saat ini sebagai titik nol |
| `0x04` | Restart | Reboot ESP32 |

Untuk `CMD 0x02`, byte parameter setelah `CMD` diisi dengan `SUBCMD`.

**Response (ACK):** `D5 AA 05 89 06 02 00 [SUBCMD echo] [CRC_H] [CRC_L]`

Byte `00` setelah CMD adalah reserved, lalu diikuti echo SUBCMD.

| ACK | Frame |
|-----|-------|
| Standby | `D5 AA 05 89 06 02 00 01 [CRC]` |
| Operation | `D5 AA 05 89 06 02 00 02 [CRC]` |
| Tare | `D5 AA 05 89 06 02 00 03 [CRC]` |
| Restart | `D5 AA 05 89 06 02 00 04 [CRC]` (sebelum reboot) |
| Error | `D5 AA 05 89 06 02 00 0E [CRC]` |

---

## Contoh Frame Lengkap

CRC ditulis big-endian sebagai `[CRC_H] [CRC_L]`. Nilai measurement bergantung posisi AS5600. Contoh di bawah memakai skenario jarak/panjang = 8.28 cm.

### Measurement

Master mengirim:

```
D5 AA 04 88 06 00 00 A6 BD
```

Slave menjawab:

```
D5 AA 0A 89 06 00 00 00 03 3C 00 55 F9 [CRC_H] [CRC_L]
```

Isi response:

| Field | Hex | Decimal | Arti |
|-------|-----|---------|------|
| Reserved | `00` | — | byte reserved setelah CMD |
| DIST | `00 03 3C` | 828 | 8.28 cm |
| ANGLE | `00 55 F9` | 22009 | 220.09° (angle adjust) |

### Standby

Master mengirim:

```
D5 AA 04 88 06 02 01 06 7D
```

Slave menjawab:

```
D5 AA 05 89 06 02 00 01 60 3A
```

### Operation

Master mengirim:

```
D5 AA 04 88 06 02 02 07 3D
```

Slave menjawab:

```
D5 AA 05 89 06 02 00 02 61 7A
```

### Tare

Master mengirim:

```
D5 AA 04 88 06 02 03 C7 FC
```

Slave menjawab:

```
D5 AA 05 89 06 02 00 03 A1 BB
```

### Restart

Master mengirim:

```
D5 AA 04 88 06 02 04 05 BD
```

Slave menjawab sebelum reboot:

```
D5 AA 05 89 06 02 00 04 63 FA
```

### Error

Contoh jika master mengirim subcontrol yang tidak dikenal:

```
D5 AA 04 88 06 02 FF 86 FC
```

Slave menjawab error. Response yang sama juga dipakai jika Measurement dikirim saat state `Standby`:

```
D5 AA 05 89 06 02 00 0E 64 7A
```

---

## CRC-16 Modbus

```c
uint16_t crc16_modbus(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc >>= 1;
        }
    }
    return crc;
}
```

Dihitung atas seluruh byte mulai `D5` sampai byte terakhir sebelum CRC. Output big-endian (MSB first di field CRC_H).

---

## State Machine

```
         [Standby]  <──── CMD 02 data=0x01
              │
              │ CMD 02 data=0x02
              ▼
         [Operation] ──── CMD 00 ──► Reply Measurement
              │
              │ CMD 02 data=0x03
              └──────────────────► Tare (tetap Operation)

         CMD 02 data=0x04 ──► Restart (dari state apapun)
```

State awal saat boot: **Operation**.

---

## Build & Flash

```bash
# Build untuk ESP32dev (aktif)
pio run -e esp32dev

# Build & flash (upload port: COM22)
pio run -e esp32dev --target upload

# Monitor serial (115200)
pio device monitor
```

> Environment `esp32c3` dan `esp32s3` tersedia di [`platformio.ini`](platformio.ini) namun dikomentari — aktifkan sesuai board yang digunakan.

---

## Simulasi dengan Docklight

File template: [`simulasiTesting/TestingSImulasi.ptp`](simulasiTesting/TestingSImulasi.ptp)

**Setting port:** Baud 230400, 8N1, RS485 half-duplex.

**Urutan pengujian normal:**

1. Kirim **Operation** (`D5 AA 04 88 06 02 02 07 3D`) → ACK `D5 AA 05 89 06 02 00 02 61 7A`
2. Kirim **Measurement** (`D5 AA 04 88 06 00 00 A6 BD`) → reply: `00` reserved + 3 byte jarak (`cm × 100`) + 3 byte angle adjust (`deg × 100`)
3. Kirim **Tare** (`D5 AA 04 88 06 02 03 C7 FC`) → ACK `D5 AA 05 89 06 02 00 03 A1 BB`
4. Kirim **Standby** (`D5 AA 04 88 06 02 01 06 7D`) → ACK `D5 AA 05 89 06 02 00 01 60 3A`
5. Kirim **Restart** (`D5 AA 04 88 06 02 04 05 BD`) → ACK `D5 AA 05 89 06 02 00 04 63 FA`, lalu reboot
