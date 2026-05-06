# Magnetic Encoder RS485 — Firmware V0.1

Firmware untuk sensor jarak berbasis encoder magnetik AS5600, berkomunikasi dengan Mainboard via RS485 half-duplex.

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

> `RS485_EN = -1` artinya arah RS485 ditangani otomatis oleh hardware (tidak perlu pin kontrol manual).

---

## Arsitektur Software

```
src/
├── main.cpp      — setup/loop: inisialisasi, dispatch paket masuk
├── rs485.cpp     — driver RS485: read, send, parsing header, CRC check
├── crc.cpp       — CRC16-Modbus
├── encoder.cpp   — baca AS5600, kalkulasi jarak (piecewise-linear)
└── command.cpp   — handler perintah: single, mode, tare, restart
```

---

## Status Saat Ini

`handleSingle()` dan `handleMulti()` menggunakan **nilai dummy** sementara sambil menunggu integrasi sensor AS5600 via I2C selesai. Blok dummy ditandai dengan komentar `// ===== DUMMY VALUE(S) =====` di `command.cpp` dan dapat dikomentari saat sensor sudah siap.

| Handler | Nilai dummy |
|---------|-------------|
| `handleSingle()` | `distance = 123.4 cm` |
| `handleMulti()` | `W=100.00`, `TL=25.50`, `TR=25.75`, `BL=26.00`, `BR=26.25` |

---

## Protokol RS485

### Format Paket Umum

```
[D5] [AA] [LEN] [SRC] [DST] [CMD] [DATA...] [CRCH] [CRCL]
```

| Field | Ukuran | Keterangan |
|-------|--------|------------|
| D5 AA | 2 byte | Header tetap |
| LEN | 1 byte | Jumlah byte: SRC + DST + CMD + DATA |
| SRC | 1 byte | Alamat pengirim |
| DST | 1 byte | Alamat tujuan |
| CMD | 1 byte | Tipe perintah |
| DATA | n byte | Data payload |
| CRCH CRCL | 2 byte | CRC16-Modbus (big-endian) |

**CRC16-Modbus** dihitung atas seluruh byte mulai `D5` sampai byte DATA terakhir (tidak termasuk CRC itu sendiri).

### Alamat Perangkat

Encoder ini mendukung **dua skema alamat** (lama & baru) — slave akan merespons jika `targetID` cocok dengan salah satunya.

| Skema | Master (SRC) | Slave (DST) | Sumber reply (SRC) | Catatan |
|-------|--------------|-------------|--------------------|---------|
| Lama (single-measure) | `0x03` | `0x06` | `0x89` | Reply 1 nilai jarak |
| Baru (multi-measure) | `0x88` | `0x01` | `0x89` | Reply 5 nilai (W, TL, TR, BL, BR) |

---

## Perintah Master → Encoder

### 1. Read Single (Baca Jarak Sekali)

```
D5 AA 03 03 06 00 22 F8
```

| Byte | Nilai | Keterangan |
|------|-------|------------|
| LEN | 03 | 3 byte payload |
| SRC | 03 | Master |
| DST | 06 | Encoder |
| CMD | 00 | Read single |

**Syarat:** State harus `Operation`. Jika `Standby`, encoder membalas Error.

---

### 1b. Read Multi (Baca 5 Nilai Sekaligus) — protokol baru

```
D5 AA 04 88 01 00 00 [CRC_H] [CRC_L]
```

| Byte | Nilai | Keterangan |
|------|-------|------------|
| LEN | 04 | 4 byte payload |
| SRC | 88 | Master baru |
| DST | 01 | Encoder (alamat baru) |
| CMD | 00 | Read |
| DATA1 | 00 | Reserved |

**Syarat:** State harus `Operation`. Jika `Standby`, encoder membalas Error.

---

### 2. Set Mode Standby

```
D5 AA 05 03 06 02 01 00 EF 63
```

| Byte | Nilai | Keterangan |
|------|-------|------------|
| CMD | 02 | Mode control |
| DATA1 | 01 | Standby |
| DATA2 | 00 | - |

---

### 3. Set Mode Operation

```
D5 AA 05 03 06 02 02 00 1F 63
```

| DATA1 | 02 | Operation |

---

### 4. Tare (Reset Titik Nol)

```
D5 AA 05 03 06 02 03 00 8F 62
```

| DATA1 | 03 | Tare — set posisi encoder saat ini sebagai nol |

---

### 5. Restart

```
D5 AA 05 03 06 02 04 00 BF 60
```

| DATA1 | 04 | Restart — encoder reboot via ESP.restart() |

---

## Respons Encoder → Master

Format response: `D5 AA 05 89 06 [CMD] [DATA1] [DATA2] [CRCH] [CRCL]`

| Response | Hex | Keterangan |
|----------|-----|------------|
| Standby ACK | `D5 AA 05 89 06 02 00 01 60 3A` | Konfirmasi mode Standby |
| Operation ACK | `D5 AA 05 89 06 02 00 02 61 7A` | Konfirmasi mode Operation |
| Tare ACK | `D5 AA 05 89 06 02 00 03 A1 BB` | Konfirmasi Tare |
| Restart ACK | `D5 AA 05 89 06 02 00 04 63 FA` | Konfirmasi sebelum restart |
| Error | `D5 AA 05 89 06 02 00 0E 64 7A` | Perintah tidak valid di state ini |

### Response Baca Jarak

```
D5 AA 05 89 06 00 [DIST_H] [DIST_L] [CRCH] [CRCL]
```

Nilai jarak dikirim dalam satuan **0.1 cm** (integer 16-bit, big-endian).

**Contoh:**
- Jarak 10.0 cm → `0x0064` → `D5 AA 05 89 06 00 00 64 ...`
- Jarak 100.0 cm → `0x03E8` → `D5 AA 05 89 06 00 03 E8 DE 5A`

### Response Multi-Measure (5 nilai)

```
D5 AA 12 89 01 00 [W_H W_M W_L] [TL_H TL_M TL_L] [TR_H TR_M TR_L] [BL_H BL_M BL_L] [BR_H BR_M BR_L] [CRC_H] [CRC_L]
```

| Field | Ukuran | Keterangan |
|-------|--------|------------|
| LEN | 1 byte | `0x12` (18) = SRC + DST + CMD + 15 byte data |
| SRC | 1 byte | `0x89` (encoder) |
| DST | 1 byte | `0x01` (master baru) |
| CMD | 1 byte | `0x00` |
| W   | 3 byte | Width — big-endian, **× 100** |
| TL  | 3 byte | Top Left  — big-endian, **× 100** |
| TR  | 3 byte | Top Right — big-endian, **× 100** |
| BL  | 3 byte | Bottom Left  — big-endian, **× 100** |
| BR  | 3 byte | Bottom Right — big-endian, **× 100** |
| CRC | 2 byte | CRC16-Modbus |

**Encoding nilai:** float dikalikan 100 (untuk membuang floating point), lalu disimpan sebagai integer 24-bit big-endian.

```
nilai_float × 100 → uint32 → 3 byte [HIGH | MID | LOW]
```

**Contoh decoding di sisi master:**
```
byte = [0x00, 0x27, 0x10]
raw  = (0x00 << 16) | (0x27 << 8) | 0x10 = 10000
nilai = 10000 / 100.0 = 100.00
```

**Contoh paket lengkap (nilai dummy saat ini):**

| Nilai | Float | × 100 | Hex (3 byte) |
|-------|-------|-------|--------------|
| W  | 100.00 | 10000 | `00 27 10` |
| TL | 25.50  | 2550  | `00 09 F6` |
| TR | 25.75  | 2575  | `00 0A 0F` |
| BL | 26.00  | 2600  | `00 0A 28` |
| BR | 26.25  | 2625  | `00 0A 41` |

```
D5 AA 12 89 01 00  00 27 10  00 09 F6  00 0A 0F  00 0A 28  00 0A 41  [CRCH] [CRCL]
```

Range nilai per field: `0` s/d `167772.15` (24-bit / 100).

---

## State Machine

```
         [Standby]  <──── CMD 02 data1=01
              │
              │ CMD 02 data1=02
              ▼
         [Operation] ──── CMD 00 ──► Reply jarak
              │
              │ CMD 02 data1=03
              └──────────────────► Tare (tetap Operation)

         CMD 02 data1=04 ──► Restart (dari state apapun)
```

State awal saat boot: **Operation**.

---

## Kalkulasi Jarak

Jarak dihitung dari rotasi kumulatif encoder AS5600 menggunakan regresi piecewise-linear yang dikalibrasi dari data pengukuran fisik.

```
delta    = posisi_kumulatif - posisi_saat_tare
derajat  = delta × (360 / 4096)
degAdj   = derajat + 0.44°   (offset kalibrasi)
jarak    = getDistance(degAdj)   [cm]
```

Fungsi `getDistance()` valid pada rentang **0.44° – 4356.74°** akumulatif (sekitar 12 putaran penuh).

---

## Simulasi dengan Docklight

File: `simulasiTesting/TestingSImulasi.ptp`

**Setting port:**
- Baud: 230400
- Data: 8N1
- Mode: RS485 half-duplex

**Urutan pengujian normal:**
1. Kirim **Operation** → tunggu ACK `...00 02...`
2. Kirim **Read Single** → terima respons jarak (saat ini dummy `123.4 cm`)
3. Kirim **Tare** → kirim **Read Single** lagi → jarak kembali ke ~0
4. Kirim **Standby** → kirim **Read Single** → encoder balas Error
5. Kirim **Restart** → encoder reboot

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

> Environment `esp32c3` dan `esp32s3` tersedia di `platformio.ini` namun dikomentari — aktifkan sesuai board yang digunakan.
