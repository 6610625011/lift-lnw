# ชุดติดตั้ง Raspberry Pi + ลำดับการใช้งานแบบใหม่

ตรงกับแผนภาพ: ปุ่ม/LCD/LED ↔ ESP32 ↔ Wi-Fi/MQTT ↔ Mosquitto (Pi) ↔ Node-RED (Pi) ↔ Dashboard (มือถือ/คอม)

## ลำดับใช้งานใหม่ (เฟิร์มแวร์ + Dashboard)

ผู้ใช้เลือกได้ว่าตอนนี้ยืนอยู่ชั้นไหน (ช่อง "ตอนนี้คุณอยู่ชั้น" บน Dashboard ค่าเริ่มต้น 1) ปุ่มจริงบนบอร์ดใช้ชั้นล่าสุดที่เลือก

1. กดปุ่มที่บอร์ด (GPIO27) หรือปุ่ม "เรียกลิฟต์" บน Dashboard → ESP32 เลือกลิฟต์ที่ใกล้ชั้น 1 ที่สุด
2. LCD แสดง `GO TO LIFT C` / `NOW:03 HALL:01` และ Dashboard แสดงแถบ "ไปที่ลิฟต์ C" พร้อมลิฟต์ C เลื่อนลงมาในแผนผัง 20 ชั้น
3. ลิฟต์ถึงชั้น 1 → LCD แสดง `LIFT C OPEN` / `SELECT FLOOR` และปุ่มชั้น 1–20 บน Dashboard ("แผงปุ่มในลิฟต์") เปิดให้กด
4. กดเลือกชั้น (เช่น 16) → LCD แสดง `LIFT C TO F16` / `DOORS CLOSING` → 2 วินาทีต่อมาลิฟต์ขึ้นไปชั้น 16 ทีละชั้น (2 วินาที/ชั้น) → `LIFT C ARRIVED`
5. ถ้าไม่เลือกชั้นภายใน 30 วินาที ระบบยกเลิกงานและกลับสถานะว่าง

ตำแหน่งเริ่มต้นใหม่: A=5, B=14, C=3, D=9 (ครั้งแรกจึงได้ลิฟต์ C)

### MQTT ที่เพิ่ม/เปลี่ยน (topic เดิม `elevator/v1/mockup/…`)

| topic | ข้อความ |
|---|---|
| `request` เรียกลิฟต์ | `{"action":"verify","floor":10 (ไม่ใส่ = ชั้นล่าสุด),"request_id":…,"boot_id":…,"issued_uptime_ms":…}` |
| `request` เลือกชั้น | `{"action":"select","floor":16,"request_id":…,"boot_id":…,"issued_uptime_ms":…}` |
| `state` | `destination` = 0 จนกว่าจะเลือกชั้น, `hall_floor` = ชั้นที่ผู้ใช้ยืน |
| `events` | เพิ่มชนิด `selected` |

## ติดตั้งบน Raspberry Pi

ต้องการ Raspberry Pi OS (Bookworm) ต่อเครือข่ายเดียวกับ ESP32 (Wi-Fi 2.4 GHz หรือสาย LAN ที่ router เดียวกัน)

```bash
git clone <โปรเจกต์นี้> elevator && cd elevator   # หรือคัดลอกโฟลเดอร์มา
bash raspberry-pi/setup-pi.sh
```

สคริปต์ติดตั้ง Mosquitto + Node.js 20+ + Node-RED (จากโฟลเดอร์ `windows/node-red` ที่มีอยู่) สุ่มรหัสผ่าน MQTT ใหม่ สร้าง service `mosquitto` และ `elevator-nodered` ที่เริ่มเองตอนบูต แล้วพิมพ์ค่าที่ต้องใส่ใน `include/secrets.h` บนคอม จากนั้น:

1. คัดลอก `include/secrets.example.h` เป็น `include/secrets.h` ใส่ Wi-Fi + `mqttHost` (IP ของ Pi) + รหัสผ่านตามที่สคริปต์พิมพ์
2. `pio run --target upload` ลงบอร์ด
3. เปิด `http://<IP ของ Pi>:1880/dashboard/elevators` จากมือถือ/คอมที่อยู่เครือข่ายเดียวกัน

ในโหมด Pi ตัวแก้ flow ของ Node-RED ถูกปิด (ไม่มีรหัสผ่าน จึงไม่เปิดให้คนในเครือข่ายแก้) หากต้องการแก้ flow ให้แก้ไฟล์ต้นทางแล้วรัน `node scripts/generate-flows.cjs` และ `sudo systemctl restart elevator-nodered`

## ทดสอบกับบอร์ดจริงโดยไม่ต้องใช้เบราว์เซอร์

เมื่อบอร์ดออนไลน์และว่าง รันบน Pi:

```bash
MQTT_HOST=127.0.0.1 node scripts/test-live-select.cjs 16
```

สคริปต์ส่งคำเรียก → รอบอร์ดมอบหมายลิฟต์และเปิดประตู → ส่งเลือกชั้น → รอถึงชั้นปลายทาง แล้วพิมพ์ PASS/FAIL พร้อมจอ LCD

## สถานะการทดสอบ (ตามจริง)

| รายการ | สถานะ |
|---|---|
| ลอจิกเฟิร์มแวร์ (เรียกชั้น 1, เลือกชั้น, หมดเวลา, ค่าผิด) | ผ่าน host test `test/sim_test.cpp` (คอมไพล์ด้วย MSVC) |
| เฟิร์มแวร์ build สำหรับ ESP32 | ผ่าน `pio run` (ยังไม่ได้อัปโหลดลงบอร์ด) |
| ฟังก์ชัน Node-RED ฝั่งเรียก/เลือกชั้น | ผ่าน `node test/dashboard_test.cjs` |
| Dashboard แผงเลือกชั้น + แอนิเมชัน | ทดสอบกับตัวจำลองในเบราว์เซอร์ (`windows/node-red/preview.html`) |
| ESP32 จริง ↔ MQTT ↔ Dashboard (เลือกชั้นจากเว็บ) | **ยังไม่ได้ทดสอบ** |
| `setup-pi.sh` บน Raspberry Pi จริง | **ยังไม่ได้ทดสอบ** (ตรวจ syntax แล้วเท่านั้น) |
| Wi-Fi ขาดช่วง | ปัญหาเดิมยังไม่ถูกแก้ |

ข้อจำกัดที่ยังมี: ปุ่มจริงมีปุ่มเดียว จึงใช้เป็นปุ่มเรียกลิฟต์เท่านั้น การเลือกชั้นทำผ่านแผงปุ่มบน Dashboard (มือถือทำหน้าที่เป็นปุ่มในลิฟต์) ถ้าต้องการปุ่มเลือกชั้นบนบอร์ดต้องเพิ่มฮาร์ดแวร์ (เช่น ปุ่มขึ้น/ลง + ปุ่มยืนยัน)

## รันทั้งระบบบนคอมนี้ (localhost) ก่อนมี Pi/ESP32

ดับเบิลคลิก `Start Local Simulation.cmd` (หรือ `scripts/start-local-sim.ps1 -Speed 4` เพื่อเร็วขึ้น 4 เท่า) จะรันครบสายจริง:

`fake-esp32.cjs` (ตัวแทนบอร์ด พูด MQTT โปรโตคอลเดียวกับเฟิร์มแวร์) → `dev-broker.cjs` (broker aedes แทน Mosquitto, ตั้งรหัสผ่านและจำกัด topic) → Node-RED จริง → http://127.0.0.1:1880/dashboard/elevators

หยุดด้วย `scripts/stop-local-sim.ps1` ทดสอบเส้นทางทั้งหมดด้วย `node scripts/test-live-select.cjs 16` (ผ่านแล้วบนเครื่องนี้) ส่วนที่ยังจำลองคือตัวบอร์ด (ปุ่ม LCD LED Wi-Fi) และ broker (Mosquitto จริงอยู่บน Pi)

## ต้องใช้ Docker บน Pi ไหม

ไม่จำเป็น `setup-pi.sh` ติดตั้ง Mosquitto และ Node-RED ตรงบน Pi (apt + systemd) ซึ่งเบาและตรงกว่าสำหรับโปรเจกต์นี้ Docker ใช้ได้ถ้าอยากแยกสภาพแวดล้อมหรือย้ายเครื่องง่าย แต่เพิ่มความซับซ้อนและใช้ RAM มากขึ้น
