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
| RS485 RX (RO) | GPIO 4 | GPIO 4 |
| RS485 TX (DI) | GPIO 5 | GPIO 5 |
| RS485 EN (DE+RE) | Auto (hardware) | Auto (hardware) |
| I2C SDA | GPIO 21 | GPIO 8 |
| I2C SCL | GPIO 22 | GPIO 9 |
| Tombol Status | GPIO 0 | GPIO 0 |

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

Measurement `CMD 0x00` membaca posisi kumulatif AS5600, menghitung delta dari titik tare, mengubahnya ke derajat (angle adjust), lalu mengonversinya ke panjang melalui fungsi kalibrasi `getDistance()` di [`encoder.cpp`](src/encoder.cpp). Reply mengirim **tiga nilai**: jarak (`cm × 100`), sudut adjust (`deg × 100`), dan status tombol (`× 100`) — masing-masing 3 byte big-endian. Nilai dibulatkan ke 1 angka di belakang koma sebelum dikirim (misal 8.28 → 8.3 → encoded 830).

Setelah menerima CMD `0x00`, slave mengirim **satu paket measurement** langsung, lalu mengaktifkan auto-send: jika tidak ada pengiriman selama **3 detik**, slave otomatis mengirim lagi tanpa perlu request ulang dari master. Auto-send berhenti saat GPIO 0 ditekan (mengirim paket terakhir STATUS `0x000064`) atau saat masuk Standby.

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
D5 AA 0C 89 06 00 [DIST_H DIST_M DIST_L] [ANGLE_H ANGLE_M ANGLE_L] [STAT_H STAT_M STAT_L] [CRC_H] [CRC_L]
```

| Field | Ukuran | Keterangan |
|-------|--------|------------|
| Length | 1 byte | `0x0C` (12 bytes: REQ+ADDR+CMD+3 byte dist+3 byte angle+3 byte status) |
| DIST | 3 byte | Jarak/panjang (`cm × 100`, dibulatkan 1 desimal) — 24-bit signed integer big-endian |
| ANGLE | 3 byte | Sudut adjust (`deg × 100`, dibulatkan 1 desimal) — 24-bit signed integer big-endian |
| STATUS | 3 byte | Status tombol (`× 100`) — `0x000000` = streaming, `0x000064` = tombol GPIO0 ditekan (paket terakhir) |

**Decoding di sisi master:**

```c
int32_t length_cm_x100  = (int32_t)((DIST_H  << 16) | (DIST_M  << 8) | DIST_L);
float   length_cm       = length_cm_x100 / 100.0f;  // 1 decimal place

int32_t angle_deg_x100  = (int32_t)((ANGLE_H << 16) | (ANGLE_M << 8) | ANGLE_L);
float   angle_deg       = angle_deg_x100 / 100.0f;  // 1 decimal place

int32_t status_x100     = (int32_t)((STAT_H  << 16) | (STAT_M  << 8) | STAT_L);
// status_x100 == 0   → streaming, button not pressed
// status_x100 == 100 → final packet, GPIO0 button pressed
```

Contoh: panjang = 8.3 cm → encoded = 830 (`0x00033E`) → bytes = `00 03 3E`

Contoh: angle adjust = 220.1° → encoded = 22010 (`0x0055FA`) → bytes = `00 55 FA`

**Syarat:** State harus `Operation`. Jika `Standby`, encoder membalas Error.

**Error — AS5600 tidak terkoneksi:**

Jika sensor AS5600 tidak terdeteksi saat menerima CMD `0x00`, slave langsung membalas dengan error frame (CMD `0x02`, SUBCMD `0x99`) dan **tidak mengaktifkan auto-send**:

```
D5 AA 05 89 06 02 00 99 CA 3B
```

Selama auto-send aktif (`measurementRequested = true`), jika sensor terputus di tengah streaming, slave mengirim error yang sama dan **menghentikan auto-send**. Master harus mengirim ulang CMD `0x00` setelah sensor kembali terhubung.

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
| Standby | `D5 AA 05 89 06 02 00 01 60 3A` |
| Operation | `D5 AA 05 89 06 02 00 02 61 7A` |
| Tare | `D5 AA 05 89 06 02 00 03 A1 BB` |
| Restart | `D5 AA 05 89 06 02 00 04 63 FA` (sebelum reboot) |
| Error | `D5 AA 05 89 06 02 00 99 CA 3B` |

---

## Contoh Frame Lengkap

CRC ditulis big-endian sebagai `[CRC_H] [CRC_L]`. Nilai measurement bergantung posisi AS5600. Contoh di bawah memakai skenario jarak/panjang = 8.3 cm.

### Measurement

Master mengirim:

```
D5 AA 04 88 06 00 00 A6 BD
```

Slave menjawab (streaming, button not pressed):

```
D5 AA 0C 89 06 00 00 03 3E 00 55 FA 00 00 00 [CRC_H] [CRC_L]
```

| Field | Hex | Decimal | Arti |
|-------|-----|---------|------|
| DIST | `00 03 3E` | 830 | 8.3 cm (= 830 / 100) |
| ANGLE | `00 55 FA` | 22010 | 220.1° (= 22010 / 100) |
| STATUS | `00 00 00` | 0 | streaming, button not pressed |

Final packet saat tombol GPIO0 ditekan:

```
D5 AA 0C 89 06 00 00 03 3E 00 55 FA 00 00 64 [CRC_H] [CRC_L]
```

| Field | Hex | Decimal | Arti |
|-------|-----|---------|------|
| DIST | `00 03 3E` | 830 | 8.3 cm (= 830 / 100) |
| ANGLE | `00 55 FA` | 22010 | 220.1° (= 22010 / 100) |
| STATUS | `00 00 64` | 100 | final packet, button pressed (1 × 100) |

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

Error frame `CMD 0x02` SUBCMD `0x99` dikirim slave pada kondisi berikut:

| Kondisi | Pemicu |
|---------|--------|
| SUBCMD tidak dikenal | Master kirim `CMD 0x02` dengan SUBCMD selain `0x01`–`0x04` |
| Measurement saat Standby | Master kirim `CMD 0x00` padahal state `Standby` |
| AS5600 tidak terdeteksi | Master kirim `CMD 0x00`, sensor tidak merespons I2C |
| AS5600 terputus saat streaming | Sensor terputus selama auto-send aktif |

Contoh — master kirim SUBCMD tidak dikenal:

```
D5 AA 04 88 06 02 FF 86 FC
```

Slave menjawab:

```
D5 AA 05 89 06 02 00 99 CA 3B
```

Contoh — master kirim `CMD 0x00` tetapi AS5600 tidak terkoneksi:

```
D5 AA 04 88 06 00 00 A6 BD
```

Slave menjawab error (tidak mengirim data measurement, auto-send tidak aktif):

```
D5 AA 05 89 06 02 00 99 CA 3B
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
         [Standby]  <──── CMD 02 SUBCMD 0x01
              │
              │ CMD 02 SUBCMD 0x02
              ▼
         [Operation] ──── CMD 00 ──► AS5600 OK  ──► Reply Measurement + auto-send aktif
              │                  │
              │                  └── AS5600 FAIL ──► Error (CMD 02 SUBCMD 0x99), auto-send tidak aktif
              │
              │ CMD 02 SUBCMD 0x03
              └──────────────────► Tare (tetap Operation)

         CMD 02 SUBCMD 0x04 ──► Restart (dari state apapun)

         Saat auto-send aktif + AS5600 terputus ──► Error (CMD 02 SUBCMD 0x99), auto-send berhenti
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
2. Kirim **Measurement** (`D5 AA 04 88 06 00 00 A6 BD`) → slave streaming terus: 3 byte jarak (`cm × 100`) + 3 byte angle (`deg × 100`) + STATUS `0x000000`. Tekan GPIO0 → final packet STATUS `0x000064`, slave berhenti
3. Kirim **Tare** (`D5 AA 04 88 06 02 03 C7 FC`) → ACK `D5 AA 05 89 06 02 00 03 A1 BB`
4. Kirim **Standby** (`D5 AA 04 88 06 02 01 06 7D`) → ACK `D5 AA 05 89 06 02 00 01 60 3A`
5. Kirim **Restart** (`D5 AA 04 88 06 02 04 05 BD`) → ACK `D5 AA 05 89 06 02 00 04 63 FA`, lalu reboot
