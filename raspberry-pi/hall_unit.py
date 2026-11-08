#!/usr/bin/env python3
"""
CN360 Hall Unit (แผงหน้าลิฟต์) for Raspberry Pi
- LCD 16x2 I2C (Standard SDA/SCL pins)
- 4x4 Numpad using modern gpiod (libgpiod):
    ROW_PINS = [13, 6, 5, 22]   (R1 -> GPIO13, R2 -> GPIO6, R3 -> GPIO5, R4 -> GPIO22)
    COL_PINS = [17, 26, 24, 23]  (C1 -> GPIO17, C2 -> GPIO26, C3 -> GPIO24, C4 -> GPIO23)
- Connects via MQTT to Central Control Unit (Windows PC)
"""

import argparse
import json
import os
import sys
import time
import threading

# ----------------- I2C LCD Driver -----------------
try:
    import smbus2 as smbus
    HAVE_SMBUS = True
except ImportError:
    try:
        import smbus
        HAVE_SMBUS = True
    except ImportError:
        HAVE_SMBUS = False

try:
    import paho.mqtt.client as mqtt
    HAVE_MQTT = True
except ImportError:
    HAVE_MQTT = False

# ----------------- Network Configuration -----------------
# Never hard-code the MQTT password. Order of precedence:
#   command-line --broker/--user/--password  >  environment ELEVATOR_MQTT_HOST / _USER / _PASS
#   >  ../windows/local-secrets.json (same file the broker setup creates)  >  127.0.0.1 / elevator / ""
def _load_defaults():
    host, user, password = "127.0.0.1", "elevator", ""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "windows", "local-secrets.json")
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                sec = json.load(f)
            host = sec.get("mqttHost", host)
            user = sec.get("mqttUser", user)
            password = sec.get("mqttPassword", password)
        except Exception:
            pass
    return (os.environ.get("ELEVATOR_MQTT_HOST", host),
            os.environ.get("ELEVATOR_MQTT_USER", user),
            os.environ.get("ELEVATOR_MQTT_PASS", password))

DEFAULT_CENTRAL_IP, DEFAULT_MQTT_USER, DEFAULT_MQTT_PASS = _load_defaults()
DEFAULT_MQTT_PORT = 1883

ROW_PINS = [13, 6, 5, 22]
COL_PINS = [17, 26, 24, 23]

KEYMAP = [
    ['1', '2', '3', 'A'],
    ['4', '5', '6', 'B'],
    ['7', '8', '9', 'C'],
    ['*', '0', '#', 'D']
]

LCD_BACKLIGHT = 0x08
ENABLE = 0b00000100

class I2cLcd:
    def __init__(self, bus_num=1, address=0x27):
        self.bus = smbus.SMBus(bus_num) if HAVE_SMBUS else None
        self.address = address
        self.last_lines = ["", ""]
        if self.bus:
            # Auto detect address if 0x27 fails
            for addr in [address, 0x3F]:
                try:
                    self.address = addr
                    self.init_lcd()
                    break
                except Exception:
                    continue

    def write_byte(self, data):
        if self.bus:
            self.bus.write_byte(self.address, data | LCD_BACKLIGHT)

    def toggle_enable(self, data):
        time.sleep(0.0005)
        self.bus.write_byte(self.address, (data | ENABLE | LCD_BACKLIGHT))
        time.sleep(0.0005)
        self.bus.write_byte(self.address, (data & ~ENABLE | LCD_BACKLIGHT))
        time.sleep(0.0005)

    def send_nibble(self, nibble, mode):
        byte = (nibble << 4) | mode
        self.toggle_enable(byte)

    def send(self, value, mode):
        if not self.bus:
            return
        self.send_nibble(value >> 4, mode)
        self.send_nibble(value & 0x0F, mode)

    def command(self, cmd):
        self.send(cmd, 0)

    def write_char(self, char_val):
        self.send(char_val, 1)

    def init_lcd(self):
        time.sleep(0.05)
        self.send_nibble(0x03, 0)
        time.sleep(0.005)
        self.send_nibble(0x03, 0)
        time.sleep(0.001)
        self.send_nibble(0x03, 0)
        self.send_nibble(0x02, 0)

        self.command(0x28)
        self.command(0x0C)
        self.command(0x06)
        self.command(0x01)
        time.sleep(0.005)

    def clear(self):
        if self.bus:
            self.command(0x01)
            time.sleep(0.002)

    def set_cursor(self, row, col):
        if self.bus:
            row_offsets = [0x00, 0x40]
            self.command(0x80 | (col + row_offsets[row]))

    def write_string(self, text, row=0, col=0):
        if self.bus:
            self.set_cursor(row, col)
            for char in text[:16]:
                self.write_char(ord(char))

    def render(self, line1, line2):
        l1 = (line1[:16]).ljust(16)
        l2 = (line2[:16]).ljust(16)
        if l1 != self.last_lines[0] or l2 != self.last_lines[1]:
            self.last_lines = [l1, l2]
            if self.bus:
                try:
                    self.write_string(l1, row=0, col=0)
                    self.write_string(l2, row=1, col=0)
                except Exception as e:
                    print(f"[LCD] Write error: {e}")
            # Console mirror
            print(f"┌────────────────┐")
            print(f"│{l1}│")
            print(f"│{l2}│")
            print(f"└────────────────┘")

# ----------------- Modern gpiod Keypad Scanner -----------------
class KeypadGpiod:
    def __init__(self, rows=ROW_PINS, cols=COL_PINS):
        self.rows = rows
        self.cols = cols
        self.backend = None
        self.setup()

    def setup(self):
        # 1. gpiod v1 (Standard on Debian Bookworm / Raspberry Pi OS)
        try:
            import gpiod
            if hasattr(gpiod, 'Chip'):
                chip_found = None
                for c in ['gpiochip4', 'gpiochip0', 4, 0]:
                    try:
                        cand = gpiod.Chip(f"gpiochip{c}" if isinstance(c, int) else c)
                        if cand.num_lines() > max(self.rows + self.cols):
                            chip_found = cand
                            break
                    except Exception:
                        continue

                if not chip_found:
                    for i in range(10):
                        try:
                            cand = gpiod.Chip(f"gpiochip{i}")
                            if cand.num_lines() > max(self.rows + self.cols):
                                chip_found = cand
                                break
                        except Exception:
                            continue

                if chip_found:
                    self.chip = chip_found
                    self.row_lines = self.chip.get_lines(self.rows)
                    req_row = gpiod.line_request()
                    req_row.consumer = "keypad_rows"
                    req_row.request_type = gpiod.line_request.DIRECTION_OUTPUT
                    self.row_lines.request(req_row, default_values=[1, 1, 1, 1])

                    self.col_lines = self.chip.get_lines(self.cols)
                    req_col = gpiod.line_request()
                    req_col.consumer = "keypad_cols"
                    req_col.request_type = gpiod.line_request.DIRECTION_INPUT
                    if hasattr(gpiod.line_request, 'FLAG_BIAS_PULL_UP'):
                        req_col.flags = gpiod.line_request.FLAG_BIAS_PULL_UP
                    self.col_lines.request(req_col)

                    self.backend = "gpiod_v1"
                    print(f"[Keypad] Active with gpiod on {chip_found.name()}")
                    return
        except Exception:
            pass

        # 2. gpiod v2 (gpiod >= 2.0.0)
        try:
            import gpiod
            from gpiod.line import Direction, Value, Bias
            if hasattr(gpiod, 'request_lines'):
                chip_path = '/dev/gpiochip4' if os.path.exists('/dev/gpiochip4') else '/dev/gpiochip0'
                settings = {}
                for r in self.rows:
                    settings[r] = gpiod.LineSettings(direction=Direction.OUTPUT, output_value=Value.ACTIVE)
                for c in self.cols:
                    settings[c] = gpiod.LineSettings(direction=Direction.INPUT, bias=Bias.PULL_UP)
                self.line_request = gpiod.request_lines(chip_path, consumer="keypad", config=settings)
                self.backend = "gpiod_v2"
                print(f"[Keypad] Active with gpiod v2 on {chip_path}")
                return
        except Exception:
            pass

        # 3. Fallback to gpiozero
        try:
            from gpiozero import DigitalOutputDevice, Button
            self.row_devs = [DigitalOutputDevice(pin, active_high=True, initial_value=True) for pin in self.rows]
            self.col_devs = [Button(pin, pull_up=True) for pin in self.cols]
            self.backend = "gpiozero"
            print("[Keypad] Active with gpiozero (gpiod backend)")
            return
        except Exception:
            pass

        print("[Keypad] Warning: No GPIO library found. Keypad scanner disabled.")

    def scan(self):
        if self.backend == "gpiod_v1":
            for r_idx in range(4):
                vals = [1, 1, 1, 1]
                vals[r_idx] = 0
                self.row_lines.set_values(vals)
                time.sleep(0.0005)

                col_vals = self.col_lines.get_values()
                for c_idx, val in enumerate(col_vals):
                    if val == 0:
                        self.row_lines.set_values([1, 1, 1, 1])
                        return KEYMAP[r_idx][c_idx]

            self.row_lines.set_values([1, 1, 1, 1])
            return None

        elif self.backend == "gpiod_v2":
            from gpiod.line import Value
            for r_idx, r_pin in enumerate(self.rows):
                for i, p in enumerate(self.rows):
                    self.line_request.set_value(p, Value.INACTIVE if i == r_idx else Value.ACTIVE)
                time.sleep(0.0005)

                for c_idx, c_pin in enumerate(self.cols):
                    if self.line_request.get_value(c_pin) == Value.INACTIVE:
                        for p in self.rows:
                            self.line_request.set_value(p, Value.ACTIVE)
                        return KEYMAP[r_idx][c_idx]

            for p in self.rows:
                self.line_request.set_value(p, Value.ACTIVE)
            return None

        elif self.backend == "gpiozero":
            for r_idx, r_dev in enumerate(self.row_devs):
                for i, dev in enumerate(self.row_devs):
                    if i == r_idx:
                        dev.off()
                    else:
                        dev.on()
                time.sleep(0.0005)

                for c_idx, c_dev in enumerate(self.col_devs):
                    if c_dev.is_pressed:
                        for dev in self.row_devs:
                            dev.on()
                        return KEYMAP[r_idx][c_idx]

            for dev in self.row_devs:
                dev.on()
            return None

        return None

    def cleanup(self):
        try:
            if self.backend == "gpiod_v1":
                self.row_lines.release()
                self.col_lines.release()
                self.chip.close()
            elif self.backend == "gpiod_v2":
                self.line_request.release()
            elif self.backend == "gpiozero":
                for d in self.row_devs + self.col_devs:
                    d.close()
        except Exception:
            pass

# ----------------- Hall Unit State Machine -----------------
class HallUnit:
    def __init__(self, hall_floor=1, broker="127.0.0.1", port=1883, user="elevator", password=""):
        self.hall_floor = hall_floor
        self.broker = broker
        self.port = port
        self.user = user
        self.password = password

        self.lcd = I2cLcd(bus_num=1, address=0x27)
        self.keypad = KeypadGpiod()

        self.state = "IDLE"
        self.card_id = "CARD-001"
        self.input_buffer = ""
        self.timeout_start = 0
        self.assigned_info = None
        self.assigned_time = 0

        self.last_central_heartbeat = time.time()
        self.is_connected = False
        self.running = True

        self.setup_mqtt()

    def setup_mqtt(self):
        if not HAVE_MQTT:
            print("[MQTT] Error: paho-mqtt not installed. Run: pip install paho-mqtt")
            return

        self.client = mqtt.Client(client_id=f"hall-unit-fl{self.hall_floor:02d}")
        if self.user:
            self.client.username_pw_set(self.user, self.password)

        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message

        try:
            self.client.connect_async(self.broker, self.port, keepalive=10)
            self.client.loop_start()
            print(f"[MQTT] Connecting to Central at {self.broker}:{self.port}...")
        except Exception as e:
            print(f"[MQTT] Connection error: {e}")

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print("[MQTT] Connected to Windows Central Control Unit!")
            self.is_connected = True
            self.last_central_heartbeat = time.time()
            client.subscribe("elevator/v1/hall/response")
            client.subscribe("elevator/v1/central/heartbeat")
        else:
            print(f"[MQTT] Failed to connect, return code: {rc}")
            self.is_connected = False

    def on_disconnect(self, client, userdata, rc):
        print("[MQTT] Disconnected from Central.")
        self.is_connected = False

    def on_message(self, client, userdata, msg):
        topic = msg.topic
        payload_str = msg.payload.decode("utf-8")

        if topic == "elevator/v1/central/heartbeat":
            self.last_central_heartbeat = time.time()

        elif topic == "elevator/v1/hall/response":
            try:
                data = json.loads(payload_str)
                if data.get("from_floor") == self.hall_floor or data.get("for") == "all":
                    self.handle_central_response(data)
            except Exception as e:
                print(f"[MQTT] Error parsing response: {e}")

    def handle_central_response(self, data):
        status = data.get("status")
        if status == "auth_ok":
            print(f"[Central] Auth OK for card {data.get('card_id')}")
            self.state = "SELECT_FLOOR"
            self.timeout_start = time.time()
            self.input_buffer = ""

        elif status == "auth_denied":
            print(f"[Central] Auth Denied: {data.get('reason')}")
            self.lcd.render("ACCESS DENIED!", data.get("reason", "NO PERMISSION")[:16])
            time.sleep(2.5)
            self.state = "IDLE"

        elif status == "assigned":
            car = data.get("elevator", "A")
            eta = data.get("eta", 0)
            dest = data.get("to_floor", 0)
            print(f"[Central] Assigned Elevator {car} (ETA: {eta}s to Floor {dest})")
            self.assigned_info = {"car": car, "eta": eta, "dest": dest}
            self.assigned_time = time.time()
            self.state = "ASSIGNED"

        elif status == "rejected":
            reason = data.get("reason", "BUSY")
            self.lcd.render("CANNOT ASSIGN", reason[:16])
            time.sleep(2.5)
            self.state = "IDLE"

    def send_auth_request(self, card_id):
        if not self.is_connected:
            self.state = "SELECT_FLOOR"
            self.timeout_start = time.time()
            self.input_buffer = ""
            return

        payload = {
            "action": "auth_card",
            "from_floor": self.hall_floor,
            "card_id": card_id,
            "timestamp": time.time()
        }
        self.client.publish("elevator/v1/hall/request", json.dumps(payload), qos=1)

    def send_destination_request(self, dest_floor):
        payload = {
            "action": "destination_dispatch",
            "from_floor": self.hall_floor,
            "to_floor": dest_floor,
            "card_id": self.card_id,
            "timestamp": time.time()
        }
        if self.is_connected:
            self.client.publish("elevator/v1/hall/request", json.dumps(payload), qos=1)
        self.state = "DISPATCHING"

    def run(self):
        print("=" * 55)
        print(f"CN360 Hall Unit running on Raspberry Pi (Floor {self.hall_floor})")
        print(f"Connecting to Central: {self.broker}:{self.port}")
        print("Press '*' to simulate RFID card tap.")
        print("=" * 55)

        last_key = None

        while self.running:
            now = time.time()

            # --- Check Communication Fault (Exam requirement: within 3s) ---
            if self.is_connected and (now - self.last_central_heartbeat > 3.0):
                self.state = "COMM_FAULT"

            # --- Scan Numpad (gpiod) ---
            raw_key = self.keypad.scan()
            key_pressed = None
            if raw_key and raw_key != last_key:
                key_pressed = raw_key
                last_key = raw_key
            elif not raw_key:
                last_key = None

            # --- State Machine ---
            if self.state == "COMM_FAULT":
                self.lcd.render("! SYSTEM ERROR !", "COMMUNICATION LOST")
                if now - self.last_central_heartbeat <= 2.5:
                    self.state = "IDLE"

            elif self.state == "IDLE":
                self.lcd.render(f"HALL FL {self.hall_floor:02d} READY", "TAP CARD (KEY *)")
                if key_pressed == '*':
                    print("[Hall] '*' pressed -> Simulated RFID Card Tapped!")
                    self.state = "AUTHENTICATING"
                    self.lcd.render("CARD TAPPED...", "CHECKING ACCESS")
                    time.sleep(0.5)
                    self.send_auth_request(self.card_id)

            elif self.state == "AUTHENTICATING":
                pass

            elif self.state == "SELECT_FLOOR":
                elapsed = now - self.timeout_start
                remaining = max(0, int(10 - elapsed))

                if remaining <= 0:
                    print("[Hall] 10s selection timeout expired!")
                    self.lcd.render("TIME EXPIRED!", "CALL CANCELLED")
                    time.sleep(2.0)
                    self.state = "IDLE"
                    self.input_buffer = ""
                    continue

                disp_target = self.input_buffer if self.input_buffer else "__"
                self.lcd.render(f"DEST FL: {disp_target.ljust(4)} [{remaining:02d}s]", "#=OK  *=CLEAR")

                if key_pressed:
                    if key_pressed in '0123456789':
                        if len(self.input_buffer) < 2:
                            self.input_buffer += key_pressed
                    elif key_pressed == '*':
                        self.input_buffer = ""
                    elif key_pressed == '#':
                        if self.input_buffer:
                            dest_floor = int(self.input_buffer)
                            if 1 <= dest_floor <= 20 and dest_floor != self.hall_floor:
                                print(f"[Hall] Floor {dest_floor} selected. Dispatching...")
                                self.send_destination_request(dest_floor)
                            else:
                                self.lcd.render("INVALID FLOOR!", f"1-20 (NOT {self.hall_floor})")
                                time.sleep(1.5)
                                self.input_buffer = ""

            elif self.state == "DISPATCHING":
                self.lcd.render("DISPATCHING LIFT", "PLEASE WAIT...")

            elif self.state == "ASSIGNED":
                if self.assigned_info:
                    car = self.assigned_info["car"]
                    eta = self.assigned_info["eta"]
                    dest = self.assigned_info["dest"]
                    self.lcd.render(f">> GO TO LIFT {car} <<", f"ETA:{eta:02d}s -> FL {dest:02d}")

                    if now - self.assigned_time > 8.0:
                        self.state = "IDLE"
                        self.assigned_info = None

            time.sleep(0.05)

    def stop(self):
        self.running = False
        self.keypad.cleanup()
        if HAVE_MQTT:
            self.client.loop_stop()
            self.client.disconnect()

def main():
    parser = argparse.ArgumentParser(description="CN360 Hall Unit on Raspberry Pi (gpiod)")
    parser.add_argument("--floor", type=int, default=1, help="Floor where this Hall Unit is located (default: 1)")
    parser.add_argument("--broker", type=str, default=DEFAULT_CENTRAL_IP, help="Windows PC Central broker IP")
    parser.add_argument("--port", type=int, default=DEFAULT_MQTT_PORT, help="MQTT Broker port")
    parser.add_argument("--user", type=str, default=DEFAULT_MQTT_USER, help="MQTT username")
    parser.add_argument("--password", type=str, default=DEFAULT_MQTT_PASS, help="MQTT password")
    args = parser.parse_args()
    if not args.password:
        print("MQTT password missing: pass --password <mqttPassword> or set ELEVATOR_MQTT_PASS "
              "(the value printed by raspberry-pi/setup-pi.sh / stored in windows/local-secrets.json).")
        sys.exit(2)

    unit = HallUnit(hall_floor=args.floor, broker=args.broker, port=args.port, user=args.user, password=args.password)
    try:
        unit.run()
    except KeyboardInterrupt:
        print("\nStopping Hall Unit...")
    finally:
        unit.stop()

if __name__ == "__main__":
    main()
