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

**Pin Assignment (default ESP32dev):**

| Fungsi | GPIO |
|--------|------|
| RS485 RX (RO) | 13 |
| RS485 TX (DI) | 14 |
| RS485 EN (DE+RE) | 27 |
| I2C SDA | 8 |
| I2C SCL | 9 |

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

| Perangkat | Alamat |
|-----------|--------|
| Mainboard (Master) | `0x03` |
| Encoder ini (Slave) | `0x06` |
| Encoder (sumber reply) | `0x89` |

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

File: `simulasiTesting/sim.ptp`

**Setting port:**
- Baud: 230400
- Data: 8N1
- Mode: RS485 half-duplex

**Urutan pengujian normal:**
1. Kirim **Operation** → tunggu ACK `...00 02...`
2. Kirim **Read Single** → terima respons jarak
3. Kirim **Tare** → kirim **Read Single** lagi → jarak kembali ke ~0
4. Kirim **Standby** → kirim **Read Single** → encoder balas Error
5. Kirim **Restart** → encoder reboot

---

## Build & Flash

```bash
# Build untuk ESP32dev
pio run -e esp32dev

# Build & flash
pio run -e esp32dev --target upload

# Monitor serial (115200)
pio device monitor

# Ganti board jika perlu
pio run -e esp32c3
pio run -e esp32s3
```
