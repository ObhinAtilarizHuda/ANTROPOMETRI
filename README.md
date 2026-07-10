# Magnetic Length Encoder RS485 — Firmware V0.1

Firmware pembaca panjang berbasis ESP32 + sensor magnetic encoder AS5600, berkomunikasi dengan master device/PLC via RS485.

Implementasi mengikuti dokumen [`protokol.txt`](protokol.txt).

---

## Hardware

| Komponen | Detail |
|----------|--------|
| MCU | ESP32 (dev / C3 / S3) — environment aktif: **ESP32-C3** |
| Sensor | AS5600 (I2C magnetic encoder) |
| Komunikasi | RS485 half-duplex, 230400 baud |
| Input | 1× push button (gesture: klik / tahan 5 detik) |
| Output | 1× active buzzer (feedback bunyi) |
| Library | robtillaart/AS5600 (`^0.6.3`) |

**Pin Assignment** (didefinisikan di [`include/config.h`](include/config.h)):

| Fungsi | ESP32dev | ESP32-C3 |
|--------|----------|----------|
| RS485 RX (RO) | GPIO 19 | GPIO 19 |
| RS485 TX (DI) | GPIO 18 | GPIO 18 |
| RS485 EN (DE+RE) | Auto (hardware) | Auto (hardware) |
| I2C SDA | GPIO 21 | GPIO 8 |
| I2C SCL | GPIO 22 | GPIO 10 |
| AS5600 DIR | GPIO 4 | GPIO 4 |
| Tombol | GPIO 3 | GPIO 3 |
| Buzzer (active) | GPIO 5 | GPIO 5 |

> **AS5600 DIR** di-set **HIGH** saat boot → arah hitung **counter-clockwise (CCW)**. Ubah ke `AS5600_CLOCK_WISE` di [`main.cpp`](src/main.cpp) (DIR = LOW) bila arah terbalik. Kalibrasi `getDistance()` hanya valid untuk derajat naik positif.

---

## Mode Operasi

Dipilih saat compile lewat `#define RS485_MODE` di [`config.h`](include/config.h):

| Nilai | Mode | Perilaku |
|-------|------|----------|
| `1` | **RS485** (operasi normal) | Berkomunikasi dengan master via RS485. Tombol & buzzer mengikuti flow handshake. |
| `0` | **Test sensor** | Tidak ada RS485. Membaca AS5600 dan mencetak `Degree \| Degree Adjust \| Distance` ke Serial Monitor. Klik tombol = Tare. |

---

## Tombol & Buzzer

Tombol di-polling dengan debounce (modul [`button.cpp`](src/button.cpp)). Gesture berbeda per mode:

| Mode | Gesture | Aksi |
|------|---------|------|
| RS485 | **Klik sekali** | Kirim 1 paket measurement ke master (hanya saat sudah di-arm CMD `0x00`) |
| RS485 | **Tahan 5 detik** | Tare lokal + lapor ke master (inisiatif user, kapan saja) |
| Test | **Klik sekali** | Tare lokal |

> Di mode RS485, measurement tetap harus di-arm master (CMD `0x00`) dulu — klik tanpa arm diabaikan. **Tare berbeda:** user boleh menahan tombol 5 detik kapan saja untuk tare + lapor ke master, walau master tidak meminta.

Buzzer (active, GPIO5) dikontrol non-blocking (modul [`buzzer.cpp`](src/buzzer.cpp)):

| Event | Pola bunyi |
|-------|-----------|
| Measurement terkirim ke master (klik) | 1× beep 200 ms (`MEASURE_BEEP_MS`) |
| Tare (perintah master / tahan 5 detik) | **3× beep** 200 ms (`TARE_BEEP_COUNT` × `TARE_BEEP_MS`) |
| Tare di mode Test (klik) | 1× beep 200 ms (`TARE_BEEP_MS`) |

Parameter timing tombol & buzzer ada di [`config.h`](include/config.h): `TARE_HOLD_MS` (5000), `BTN_DEBOUNCE_MS` (30), `MEASURE_BEEP_MS`, `TARE_BEEP_MS`, `TARE_BEEP_COUNT`.

---

## Arsitektur Software

```
src/
├── main.cpp      — setup/loop: inisialisasi, dispatch paket, gesture & buzzer
├── rs485.cpp     — driver RS485: read, send, parsing, CRC check
├── crc.cpp       — CRC16-Modbus (poly 0xA001, init 0xFFFF)
├── encoder.cpp   — baca AS5600, kalkulasi (sumber data sensor)
├── button.cpp    — deteksi gesture tombol (klik / tahan 5 detik)
├── buzzer.cpp    — kontrol active buzzer non-blocking (beep tunggal & pola)
└── command.cpp   — handler perintah: measurement, control, tare handshake
```

---

## Status Saat Ini

Measurement `CMD 0x00` membaca posisi kumulatif AS5600, menghitung delta dari titik tare, mengubahnya ke derajat (angle adjust), lalu mengonversinya ke panjang melalui fungsi kalibrasi `getDistance()` di [`encoder.cpp`](src/encoder.cpp). Reply mengirim **tiga nilai**: jarak (`cm × 100`), sudut adjust (`deg × 100`), dan status tombol (`× 100`) — masing-masing 3 byte big-endian. Nilai dibulatkan ke 1 angka di belakang koma sebelum dikirim (misal 8.28 → 8.3 → encoded 830).

`getDistance()` memakai kalibrasi piecewise-linear (6 segmen). Hasil di luar rentang kalibrasi di-clamp: nilai negatif (di sekitar titik tare) dibulatkan ke **0 cm**, dan nilai di atas ujung rentang kalibrasi ditahan di **150 cm** (bukan ekstrapolasi maupun jatuh ke 0).

Setelah menerima CMD `0x00`, slave **tidak langsung mengirim apa-apa ke master** — slave masuk mode "armed": membaca AS5600 terus-menerus dan mencetak perubahan ke Serial monitor untuk debugging, sambil menunggu **klik tombol**. Saat tombol diklik, slave mengirim **satu paket measurement** ke master dengan STATUS `0x000064` lalu kembali idle, dan buzzer beep 1× 200 ms. Mode armed dibatalkan tanpa kirim paket jika: master mengirim Cancel (CMD `0x02` SUBCMD `0x05`), masuk Standby, atau timeout 60 detik tanpa tombol (slave kirim Error frame).

Tare `CMD 0x02` SUBCMD `0x03` dieksekusi **langsung**: begitu master meminta tare, slave menjalankan tare, mengirim ACK Tare ke master, dan membunyikan buzzer **3× 200 ms**. Selain itu, user dapat memicu tare **kapan saja** dengan **menahan tombol 5 detik** — slave melakukan tare lalu mengirim frame Tare ke master sebagai laporan (walaupun master tidak memintanya).

| CMD | Handler | Keterangan |
|-----|---------|-------------|
| `0x00` Measurement | `handleMeasurement()` → `loopMeasurement()` | Armed → klik → kirim jarak (cm×100) + angle adjust (deg×100) |
| `0x02` Control | `handleMode()` | Standby / Operation / Tare (langsung) / Restart / Cancel |
| `0x03` Error | `sendError()` (dipanggil dari berbagai handler) | Slave-initiated: `ERR_TIMEOUT` / `ERR_CHECKSUM` / `ERR_SENSOR_NO_RESPONSE` / `ERR_TARE_INVALID` |

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
| Command | 1 byte | `0x00` measurement, `0x02` control, `0x03` error report (slave → master saja) |
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
| STATUS | 3 byte | Status tombol (`× 100`) — selalu `0x000064` karena slave hanya mengirim saat tombol diklik |

**Decoding di sisi master:**

```c
int32_t length_cm_x100  = (int32_t)((DIST_H  << 16) | (DIST_M  << 8) | DIST_L);
float   length_cm       = length_cm_x100 / 100.0f;  // 1 decimal place

int32_t angle_deg_x100  = (int32_t)((ANGLE_H << 16) | (ANGLE_M << 8) | ANGLE_L);
float   angle_deg       = angle_deg_x100 / 100.0f;  // 1 decimal place

int32_t status_x100     = (int32_t)((STAT_H  << 16) | (STAT_M  << 8) | STAT_L);
// status_x100 == 100 → tombol diklik (single send, slave-triggered)
```

Contoh: panjang = 8.3 cm → encoded = 830 (`0x00033E`) → bytes = `00 03 3E`

Contoh: angle adjust = 220.1° → encoded = 22010 (`0x0055FA`) → bytes = `00 55 FA`

**Syarat:**
- State harus `Operation`. Jika `Standby` → `ERR_CHECKSUM`.
- Reserved byte setelah CMD harus `0x00`. Jika bukan → `ERR_CHECKSUM`.
- AS5600 harus terkoneksi. Jika tidak → `ERR_SENSOR_NO_RESPONSE`.
- Posisi saat ini harus sudah ter-tare (distance `< TARE_ZERO_TOL_CM`, default **0.5 cm**). Jika belum tare (alat diputar tapi belum di-tare ulang) → `ERR_TARE_INVALID`.

Semua kondisi gagal di atas dibalas lewat **frame error CMD `0x03`** (lihat [CMD 0x03 — Error Reporting](#cmd-0x03--error-reporting) di bawah), dan slave **tidak masuk mode armed**.

Selama mode armed aktif (`measurementRequested = true`), jika sensor terputus saat polling encoder, slave mengirim `ERR_SENSOR_NO_RESPONSE` dan **membatalkan mode armed**. Jika 60 detik tanpa klik tombol, slave mengirim `ERR_TIMEOUT`. Master harus mengirim ulang CMD `0x00` untuk mencoba lagi.

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
| `0x03` | Tare | **Langsung** — tare lalu balas ACK Tare |
| `0x04` | Restart | Reboot ESP32 |
| `0x05` | Cancel | Batalkan armed mode (measurement / tare) tanpa kirim hasil |

Untuk `CMD 0x02`, byte parameter setelah `CMD` diisi dengan `SUBCMD`.

**Response (ACK):** `D5 AA 05 89 06 02 00 [SUBCMD echo] [CRC_H] [CRC_L]`

Byte `00` setelah CMD adalah reserved, lalu diikuti echo SUBCMD.

| ACK | Frame | Catatan |
|-----|-------|---------|
| Standby | `D5 AA 05 89 06 02 00 01 60 3A` | dikirim langsung |
| Operation | `D5 AA 05 89 06 02 00 02 61 7A` | dikirim langsung |
| Tare | `D5 AA 05 89 06 02 00 03 A1 BB` | dikirim langsung setelah tare; frame sama juga dikirim saat user tahan tombol 5 detik |
| Restart | `D5 AA 05 89 06 02 00 04 63 FA` | dikirim sebelum reboot |
| Cancel | `D5 AA 05 89 06 02 00 05 A3 3B` | dikirim langsung |

> **Tare bisa slave-initiated:** selain sebagai ACK atas perintah master, frame Tare yang sama juga dikirim slave atas inisiatif user (tahan tombol 5 detik) walaupun master tidak memintanya.

> SUBCMD selain `0x01`–`0x05` dibalas `ERR_CHECKSUM` lewat frame CMD `0x03` (lihat bawah), bukan lagi lewat CMD `0x02`.

---

### CMD `0x03` — Error Reporting

Frame ini **hanya dikirim slave → master** (slave-initiated), tidak pernah diminta master. Formatnya sama seperti ACK control, hanya CMD-nya `0x03`:

```
D5 AA 05 89 06 03 00 [CODE] [CRC_H] [CRC_L]
```

| Field | Ukuran | Keterangan |
|-------|--------|------------|
| Length | 1 byte | `0x05` (5 bytes: REQ+ADDR+CMD+reserved+CODE) |
| Reserved | 1 byte | `0x00` (tetap, sebelum CODE) |
| CODE | 1 byte | Kode error, lihat tabel di bawah |

| CODE | Nama | Kondisi pemicu |
|------|------|-----------------|
| `0x01` | `ERR_TIMEOUT` | Mode armed measurement aktif tapi tombol tidak diklik dalam 60 detik |
| `0x02` | `ERR_CHECKSUM` | CRC mismatch pada frame ke address kita, atau data request tidak valid (reserved byte salah, SUBCMD tidak dikenal, `cmdType` tidak dikenal, measurement diminta saat `Standby`) |
| `0x03` | `ERR_SENSOR_NO_RESPONSE` | AS5600 tidak terkoneksi saat master minta measurement (CMD `0x00`), atau terputus selama mode armed |
| `0x04` | `ERR_TARE_INVALID` | Master minta measurement (CMD `0x00`) tapi posisi belum ter-tare — distance saat ini `≥ 0.5 cm` (`TARE_ZERO_TOL_CM` di [`config.h`](include/config.h)) |

Contoh frame lengkap (CRC terverifikasi):

| Error | Frame |
|-------|-------|
| `ERR_TIMEOUT` | `D5 AA 05 89 06 03 00 01 A0 6B` |
| `ERR_CHECKSUM` | `D5 AA 05 89 06 03 00 02 A1 2B` |
| `ERR_SENSOR_NO_RESPONSE` | `D5 AA 05 89 06 03 00 03 61 EA` |
| `ERR_TARE_INVALID` | `D5 AA 05 89 06 03 00 04 A3 AB` |

---

## Contoh Frame Lengkap

CRC ditulis big-endian sebagai `[CRC_H] [CRC_L]`. Nilai measurement bergantung posisi AS5600. Contoh di bawah memakai skenario jarak/panjang = 8.3 cm.

### Measurement

Master mengirim:

```
D5 AA 04 88 06 00 00 A6 BD
```

Slave **tidak menjawab langsung** — masuk mode armed dan menunggu **klik tombol**. Selama menunggu, perubahan jarak dicetak ke Serial monitor untuk debugging (tidak dikirim via RS485).

Saat tombol diklik, slave mengirim satu paket (lalu buzzer beep 1×):

```
D5 AA 0C 89 06 00 00 03 3E 00 55 FA 00 00 64 [CRC_H] [CRC_L]
```

| Field | Hex | Decimal | Arti |
|-------|-----|---------|------|
| DIST | `00 03 3E` | 830 | 8.3 cm (= 830 / 100) |
| ANGLE | `00 55 FA` | 22010 | 220.1° (= 22010 / 100) |
| STATUS | `00 00 64` | 100 | tombol diklik (slave-triggered single send) |

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

Master mengirim request:

```
D5 AA 04 88 06 02 03 C7 FC
```

Slave **langsung** melakukan tare, membunyikan buzzer 3×, lalu mengirim ACK:

```
D5 AA 05 89 06 02 00 03 A1 BB
```

**Tare inisiatif user:** user dapat menahan tombol **5 detik** kapan saja (tanpa perintah master). Slave melakukan tare, beep 3×, lalu mengirim frame Tare yang sama (`D5 AA 05 89 06 02 00 03 A1 BB`) ke master sebagai laporan.

### Restart

Master mengirim:

```
D5 AA 04 88 06 02 04 05 BD
```

Slave menjawab sebelum reboot:

```
D5 AA 05 89 06 02 00 04 63 FA
```

### Cancel

Membatalkan armed mode (measurement maupun tare) yang sedang aktif. Setelah menerima Cancel, slave membatalkan armed dan hanya membalas ACK Cancel — tidak ada paket measurement / ACK Tare yang dikirim.

Master mengirim:

```
D5 AA 04 88 06 02 05 C5 7C
```

Slave menjawab:

```
D5 AA 05 89 06 02 00 05 A3 3B
```

Cancel tetap di-ACK meskipun tidak ada armed mode aktif (no-op).

### Error (CMD `0x03`)

Frame error **slave-initiated** — dikirim kapan saja slave mendeteksi kondisi gagal, tidak diminta lewat request khusus dari master. Lihat [CMD 0x03 — Error Reporting](#cmd-0x03--error-reporting) untuk daftar lengkap kode.

Contoh — master minta measurement tapi AS5600 tidak terdeteksi:

Master mengirim:

```
D5 AA 04 88 06 00 00 A6 BD
```

Slave menjawab (`ERR_SENSOR_NO_RESPONSE`):

```
D5 AA 05 89 06 03 00 03 61 EA
```

Contoh — master minta measurement tapi alat belum di-tare (distance masih ≥ 0.5 cm):

Slave menjawab (`ERR_TARE_INVALID`):

```
D5 AA 05 89 06 03 00 04 A3 AB
```

Contoh — master kirim `CMD 0x02` dengan SUBCMD tidak dikenal:

Master mengirim:

```
D5 AA 04 88 06 02 FF 86 FC
```

Slave menjawab (`ERR_CHECKSUM`):

```
D5 AA 05 89 06 03 00 02 A1 2B
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
         [Operation]
              │
              ├── CMD 00 ──► reserved OK, AS5600 OK, sudah tare ──► Armed measurement (Serial monitor), tunggu klik
              │      │                                              │
              │      │                                              ├── klik ──► Kirim 1 paket STATUS 0x000064 + beep 1×, idle
              │      │                                              ├── 60 detik tanpa tombol ──► ERR_TIMEOUT (CMD 03)
              │      │                                              └── AS5600 FAIL/terputus ──► ERR_SENSOR_NO_RESPONSE (CMD 03)
              │      ├── reserved ≠ 0x00 ──► ERR_CHECKSUM (CMD 03)
              │      ├── AS5600 tidak terkoneksi ──► ERR_SENSOR_NO_RESPONSE (CMD 03)
              │      ├── belum tare (dist ≥ 0.5 cm) ──► ERR_TARE_INVALID (CMD 03)
              │      └── state == Standby ──► ERR_CHECKSUM (CMD 03)
              │
              └── CMD 02 SUBCMD 0x03 ──► Tare langsung + ACK Tare + beep 3×

         Tahan tombol 5 detik (kapan saja) ──► Tare + frame Tare ke master + beep 3×
         CMD 02 SUBCMD 0x04 ──► Restart (dari state apapun)
         CMD 02 SUBCMD 0x05 ──► Cancel armed measurement, hanya ACK Cancel
         CMD 02 SUBCMD tidak dikenal ──► ERR_CHECKSUM (CMD 03)
         Frame ke address kita dengan CRC mismatch ──► ERR_CHECKSUM (CMD 03)
```

State awal saat boot: **Operation**.

---

## Build & Flash

```bash
# Build untuk ESP32-C3 (environment aktif)
pio run -e esp32c3

# Build & flash (upload port: COM15)
pio run -e esp32c3 --target upload

# Monitor serial (115200)
pio device monitor
```

> Environment `esp32dev` dan `esp32s3` tersedia di [`platformio.ini`](platformio.ini) namun dikomentari — aktifkan sesuai board yang digunakan.

---

## Simulasi dengan Docklight

File template: [`simulasiTesting/TestingSImulasi.ptp`](simulasiTesting/TestingSImulasi.ptp)

**Setting port:** Baud 230400, 8N1, RS485 half-duplex. Pastikan `RS485_MODE` di [`config.h`](include/config.h) bernilai `1`.

**Urutan pengujian normal:**

1. Kirim **Operation** (`D5 AA 04 88 06 02 02 07 3D`) → ACK `D5 AA 05 89 06 02 00 02 61 7A`
2. Kirim **Measurement** (`D5 AA 04 88 06 00 00 A6 BD`) → slave **tidak balas**, masuk armed (monitor di Serial). **Klik** tombol → slave kirim 1 paket measurement STATUS `0x000064` + buzzer beep 1×. Alternatif: kirim **Cancel** (`D5 AA 04 88 06 02 05 C5 7C`) → ACK Cancel, armed dibatalkan
3. Kirim **Tare** (`D5 AA 04 88 06 02 03 C7 FC`) → slave **langsung** tare, buzzer beep 3×, lalu kirim ACK `D5 AA 05 89 06 02 00 03 A1 BB`. (Atau **tahan tombol 5 detik** kapan saja → slave tare + kirim frame Tare yang sama tanpa diminta master)
4. Kirim **Standby** (`D5 AA 04 88 06 02 01 06 7D`) → ACK `D5 AA 05 89 06 02 00 01 60 3A`
5. Kirim **Restart** (`D5 AA 04 88 06 02 04 05 BD`) → ACK `D5 AA 05 89 06 02 00 04 63 FA`, lalu reboot

**Menguji Error (CMD `0x03`):**

- Kirim **Measurement** tanpa tare dulu (putar encoder dulu, jangan tare) → slave balas `ERR_TARE_INVALID` (`D5 AA 05 89 06 03 00 04 A3 AB`)
- Cabut AS5600 lalu kirim **Measurement** → slave balas `ERR_SENSOR_NO_RESPONSE` (`D5 AA 05 89 06 03 00 03 61 EA`)
- Kirim **Measurement** lalu jangan klik tombol 60 detik → slave balas `ERR_TIMEOUT` (`D5 AA 05 89 06 03 00 01 A0 6B`)
- Kirim `CMD 0x02` dengan SUBCMD tidak dikenal (mis. `D5 AA 04 88 06 02 FF 86 FC`) → slave balas `ERR_CHECKSUM` (`D5 AA 05 89 06 03 00 02 A1 2B`)
