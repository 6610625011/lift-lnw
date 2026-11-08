#!/usr/bin/env python3
"""
CN360 Central Control Unit (ระบบประมวลผลกลาง) on Windows PC
- Manages RFID Card Permissions Database
- Implements the exact Exam Cost Function Dispatcher (Question 3b)
- Heartbeat watchdog for Elevators & Hall Unit
- Coordinates with Raspberry Pi (Hall Unit) and ESP32 (Car Unit)
- Emergency Fire Alarm mode (returns all cars to Floor 1)
"""

import argparse
import json
import os
import sys
import time
import threading

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Error: paho-mqtt not installed. Run: pip install paho-mqtt")
    sys.exit(1)

# ----------------- Database of Card Permissions -----------------
CARDS_DATABASE = {
    "CARD-001": {
        "name": "General Staff",
        "allowed_floors": list(range(1, 21)),
        "role": "Staff"
    },
    "CARD-VIP": {
        "name": "Executive Management",
        "allowed_floors": list(range(1, 21)),
        "role": "VIP"
    },
    "CARD-GUEST": {
        "name": "Building Visitor",
        "allowed_floors": [1, 2, 3, 4, 5],
        "role": "Guest"
    }
}

# ----------------- Elevator State -----------------
class Elevator:
    def __init__(self, id_letter, initial_floor=1):
        self.id = id_letter
        self.floor = initial_floor
        self.direction = "IDLE"  # "UP", "DOWN", "IDLE"
        self.targets = []        # Scheduled stops
        self.load_percent = 0    # Current load percentage (0 - 100%)
        self.busy_other = False  # Carrying another passenger (must finish that stop first)
        self.last_heartbeat = time.time()
        self.is_communicating = True

    def update_heartbeat(self):
        self.last_heartbeat = time.time()
        self.is_communicating = True

    def check_health(self, now):
        # Exam rule: Heartbeat timeout > 3 seconds means communication lost
        if now - self.last_heartbeat > 3.0:
            self.is_communicating = False
        return self.is_communicating

# ----------------- Central Controller -----------------
class CentralController:
    def __init__(self, broker="127.0.0.1", port=1883, user="elevator", password=""):
        self.broker = broker
        self.port = port
        self.user = user
        self.password = password

        # Elevators A, B, C, D (Initial floors matching config.h: 5, 14, 3, 9)
        self.elevators = {
            'A': Elevator('A', 5),
            'B': Elevator('B', 14),
            'C': Elevator('C', 3),
            'D': Elevator('D', 9)
        }

        self.fire_alarm_active = False
        self.running = True
        self.last_state = None
        self.pending_trip = None

        self.setup_mqtt()

    def setup_mqtt(self):
        self.client = mqtt.Client(client_id="central-control-unit")
        if self.user:
            self.client.username_pw_set(self.user, self.password)

        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message

        try:
            self.client.connect_async(self.broker, self.port, keepalive=10)
            self.client.loop_start()
            print(f"[Central] Connecting to MQTT broker at {self.broker}:{self.port}...")
        except Exception as e:
            print(f"[Central] Connection error: {e}")

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print("[Central] Connected to MQTT Broker successfully!")
            client.subscribe("elevator/v1/hall/request")
            client.subscribe("elevator/v1/mockup/state")
            client.subscribe("elevator/v1/mockup/events")
        else:
            print(f"[Central] Failed to connect, return code {rc}")

    def on_message(self, client, userdata, msg):
        topic = msg.topic
        payload_str = msg.payload.decode("utf-8")

        # 1. Update live states from ESP32 Car Unit
        if topic == "elevator/v1/mockup/state":
            try:
                state = json.loads(payload_str)
                self.last_state = state
                floors = state.get("floors", {})
                for car_id, fl in floors.items():
                    if car_id in self.elevators:
                        self.elevators[car_id].floor = fl
                        self.elevators[car_id].update_heartbeat()

                # Infer motion directions
                assigned = state.get("assigned")
                phase = state.get("phase", "idle")
                hall_fl = state.get("hall_floor", 1)
                dest_fl = state.get("destination", 0)

                if assigned in self.elevators:
                    car = self.elevators[assigned]
                    if phase == "coming":
                        car.direction = "UP" if hall_fl > car.floor else "DOWN" if hall_fl < car.floor else "IDLE"
                    elif phase == "riding":
                        car.direction = "UP" if dest_fl > car.floor else "DOWN" if dest_fl < car.floor else "IDLE"
                    elif phase in ["idle", "arrived"]:
                        car.direction = "IDLE"
                        car.targets = []

                # Cars busy with other passengers: the ESP32 publishes where each one is heading
                # ("targets", 0 = free). Without this the dispatcher thinks every other car is idle.
                targets = state.get("targets") or {}
                active = phase not in ("idle", "arrived")
                for car_id, car in self.elevators.items():
                    t = targets.get(car_id, 0)
                    if not isinstance(t, int):
                        t = 0
                    car.busy_other = t > 0
                    if car_id == assigned and active:
                        if t > 0 and phase == "coming":
                            car.targets = [t, hall_fl]   # finish the other trip, then come to us
                        continue
                    if t > 0:
                        car.targets = [t]
                        car.direction = "UP" if t > car.floor else "DOWN" if t < car.floor else "IDLE"
                    else:
                        car.targets = []
                        car.direction = "IDLE"

                # Automatic passenger boarding logic for destination dispatch
                if self.pending_trip:
                    trip_car = self.pending_trip.get("car")
                    to_fl = self.pending_trip.get("to_floor")
                    boot_id = state.get("boot_id", "")
                    uptime_ms = state.get("uptime_ms", 0)
                    boarded = state.get("boarded", False)
                    current_dest = state.get("destination", 0)

                    if phase == "boarding":
                        if not boarded and self.pending_trip.get("stage") == "calling":
                            self.pending_trip["stage"] = "entering"
                            enter_req = {
                                "action": "enter",
                                "boot_id": boot_id,
                                "issued_uptime_ms": uptime_ms,
                                "request_id": f"enter-{int(time.time()*1000)}"
                            }
                            self.client.publish("elevator/v1/mockup/request", json.dumps(enter_req), qos=1)
                            print(f"[Central] Car {trip_car} arrived at pickup floor! Passenger entering...")

                        elif boarded and current_dest == 0 and self.pending_trip.get("stage") == "entering":
                            self.pending_trip["stage"] = "riding"
                            sel_req = {
                                "action": "select",
                                "floor": to_fl,
                                "boot_id": boot_id,
                                "issued_uptime_ms": uptime_ms,
                                "request_id": f"sel-{int(time.time()*1000)}"
                            }
                            self.client.publish("elevator/v1/mockup/request", json.dumps(sel_req), qos=1)
                            print(f"[Central] Car {trip_car} boarded! Dispatched to destination Floor {to_fl}.")

                    elif phase == "riding":
                        self.pending_trip["stage"] = "riding"
                    elif phase == "arrived":
                        print(f"[Central] Trip completed! Car {trip_car} arrived at destination Floor {to_fl}.")
                        self.pending_trip = None
                    elif phase == "idle" and self.pending_trip.get("stage") != "calling":
                        self.pending_trip = None
            except Exception:
                pass

        # 2. Requests from Raspberry Pi Hall Unit
        elif topic == "elevator/v1/hall/request":
            try:
                req = json.loads(payload_str)
                self.handle_hall_request(req)
            except Exception as e:
                print(f"[Central] Error handling hall request: {e}")

    def handle_hall_request(self, req):
        action = req.get("action")
        from_floor = req.get("from_floor", 1)
        card_id = req.get("card_id", "CARD-001")

        if self.fire_alarm_active:
            self.reply_hall({
                "status": "rejected",
                "from_floor": from_floor,
                "reason": "FIRE ALARM!"
            })
            return

        # Action: Card Authentication
        if action == "auth_card":
            card = CARDS_DATABASE.get(card_id)
            if card:
                print(f"[Auth] Card {card_id} verified ({card['name']}). Access granted.")
                self.reply_hall({
                    "status": "auth_ok",
                    "from_floor": from_floor,
                    "card_id": card_id,
                    "allowed_floors": card["allowed_floors"]
                })
            else:
                print(f"[Auth] Card {card_id} not found in database! Access denied.")
                self.reply_hall({
                    "status": "auth_denied",
                    "from_floor": from_floor,
                    "reason": "INVALID CARD"
                })

        # Action: Destination Dispatch
        elif action == "destination_dispatch":
            to_floor = req.get("to_floor")
            card = CARDS_DATABASE.get(card_id)

            # Check if card is permitted for target floor
            if card and to_floor not in card["allowed_floors"]:
                print(f"[Auth] Card {card_id} has NO access to Floor {to_floor}!")
                self.reply_hall({
                    "status": "rejected",
                    "from_floor": from_floor,
                    "reason": f"NO ACCESS FL {to_floor}"
                })
                return

            print(f"\n[Dispatch] Request from Floor {from_floor} to Floor {to_floor}")
            best_car, best_eta, breakdown = self.calculate_dispatch(from_floor, to_floor)

            if best_car:
                print(f"[Dispatch] Selected Elevator: >> {best_car} << (ETA: {best_eta}s)")
                # Reply to Hall Unit (Raspberry Pi)
                self.reply_hall({
                    "status": "assigned",
                    "from_floor": from_floor,
                    "to_floor": to_floor,
                    "elevator": best_car,
                    "eta": best_eta
                })
                # Send dispatch command to Car Unit (ESP32 / simulation)
                self.dispatch_car(best_car, from_floor, to_floor)
            else:
                print("[Dispatch] No elevator currently eligible to take request.")
                self.reply_hall({
                    "status": "rejected",
                    "from_floor": from_floor,
                    "reason": "ALL BUSY/FAULT"
                })

    def reply_hall(self, payload):
        self.client.publish("elevator/v1/hall/response", json.dumps(payload), qos=1)

    def dispatch_car(self, car_id, from_floor, to_floor):
        # Update internal targets
        car = self.elevators[car_id]
        if from_floor not in car.targets:
            car.targets.append(from_floor)
        if to_floor not in car.targets:
            car.targets.append(to_floor)

        self.pending_trip = {
            "from_floor": from_floor,
            "to_floor": to_floor,
            "car": car_id,
            "stage": "calling"
        }

        # Notify ESP32 via existing topic so ESP32 moves
        esp32_req = {
            "action": "verify",
            "car": car_id,          # the ESP32 must send exactly this car (single dispatcher)
            "floor": from_floor,
            "destination": to_floor,
            "request_id": f"disp-{int(time.time()*1000)}"
        }
        if self.last_state:
            esp32_req["boot_id"] = self.last_state.get("boot_id", "")
            esp32_req["issued_uptime_ms"] = self.last_state.get("uptime_ms", 0)

        self.client.publish("elevator/v1/mockup/request", json.dumps(esp32_req), qos=1)
        print(f"[Central] Dispatched Car {car_id} to Floor {from_floor} (Target Destination: Floor {to_floor})")

    def calculate_dispatch(self, hall_floor, dest_floor):
        """
        Implements exact Cost Function from CN360 Exam (Question 3b):
        - Disqualify if load > 80%
        - Disqualify if heartbeat > 3s
        - 2 seconds per floor, 10 seconds per stop
        - Can pick up if moving in same direction and >= 1 floor ahead
        """
        now = time.time()
        best_car = None
        min_cost = float('inf')
        breakdown = {}

        print("--- Dispatch Evaluation (Cost Function) ---")
        for car_id, car in sorted(self.elevators.items()):
            # Rule 1: Weight load check
            if car.load_percent > 80:
                print(f"  Lift {car_id}: Disqualified (Load {car.load_percent}% > 80%)")
                breakdown[car_id] = "Disqualified: Load > 80%"
                continue

            # Rule 2: Heartbeat communication check (<= 3s)
            time_since_hb = now - car.last_heartbeat
            if time_since_hb > 3.0:
                print(f"  Lift {car_id}: Disqualified (Heartbeat lost {time_since_hb:.1f}s > 3s)")
                breakdown[car_id] = "Disqualified: Heartbeat Timeout"
                continue

            # Calculate arrival cost to hall_floor
            cost = 0
            cur = car.floor
            direction = car.direction

            if direction == "IDLE" or not car.targets:
                # Direct travel from current floor to hall_floor
                dist = abs(hall_floor - cur)
                cost = dist * 2  # 2s per floor

            elif car.busy_other:
                # This mock-up's cars finish the other passenger's stop before coming,
                # so use the "finish existing route first" rule below.
                temp_cost = 0
                temp_pos = cur
                for stop in car.targets:
                    temp_cost += abs(stop - temp_pos) * 2 + 10  # 2s/floor + 10s stop
                    temp_pos = stop
                cost = temp_cost + abs(hall_floor - temp_pos) * 2

            elif direction == "UP" and hall_floor >= cur + 1 and dest_floor > hall_floor:
                # Can stop and pick up on the way
                cost = (hall_floor - cur) * 2

            elif direction == "DOWN" and hall_floor <= cur - 1 and dest_floor < hall_floor:
                # Can stop and pick up on the way
                cost = (cur - hall_floor) * 2

            else:
                # Must finish existing route first, then travel to hall_floor
                temp_cost = 0
                temp_pos = cur
                for stop in car.targets:
                    temp_cost += abs(stop - temp_pos) * 2 + 10  # 2s/floor + 10s stop
                    temp_pos = stop
                temp_cost += abs(hall_floor - temp_pos) * 2
                cost = temp_cost

            print(f"  Lift {car_id}: Floor {cur}, Load {car.load_percent}%, ETA Cost = {cost}s")
            breakdown[car_id] = f"{cost}s"

            if cost < min_cost:
                min_cost = cost
                best_car = car_id

        return best_car, min_cost if best_car else 0, breakdown

    def trigger_fire_alarm(self):
        """Exam Requirement: Return all cars to Floor 1 and reject new calls."""
        print("\n" + "!" * 50)
        print("EMERGENCY FIRE ALARM ACTIVATED!")
        print("All elevators returning to Floor 1 immediately.")
        print("!" * 50)
        self.fire_alarm_active = True

        for car_id in self.elevators:
            req = {
                "action": "fire_alarm",
                "elevator": car_id,
                "target_floor": 1
            }
            self.client.publish("elevator/v1/mockup/request", json.dumps(req), qos=1)

    def publish_heartbeat(self):
        """Exam requirement: Central sends heartbeat every 1 second."""
        hb = {
            "source": "central",
            "timestamp": time.time(),
            "status": "healthy" if not self.fire_alarm_active else "fire_alarm"
        }
        self.client.publish("elevator/v1/central/heartbeat", json.dumps(hb))

    def run_heartbeat_loop(self):
        while self.running:
            self.publish_heartbeat()
            # Also check elevator heartbeats
            now = time.time()
            for car in self.elevators.values():
                car.check_health(now)
            time.sleep(1.0)

    def run(self):
        hb_thread = threading.Thread(target=self.run_heartbeat_loop, daemon=True)
        hb_thread.start()

        print("=" * 60)
        print("CN360 Central Control Unit is RUNNING")
        print(f"Broker: {self.broker}:{self.port}")
        print("Commands:")
        print("  's' : Show status of all Elevators A-D")
        print("  'f' : Trigger Emergency Fire Alarm")
        print("  'q' : Quit")
        print("=" * 60)

        interactive = sys.stdin is not None and sys.stdin.isatty()
        if not interactive:
            print("No keyboard attached: running headless (stop with Ctrl+C / systemctl stop).")
            try:
                while self.running:
                    time.sleep(1.0)
            except KeyboardInterrupt:
                self.running = False

        while self.running:
            try:
                cmd = input().strip().lower()
                if cmd == 's':
                    now = time.time()
                    print("\n--- Elevator Status ---")
                    for cid, car in sorted(self.elevators.items()):
                        alive = "OK" if (now - car.last_heartbeat <= 3.0) else "TIMEOUT"
                        print(f"  Lift {cid}: Floor {car.floor:02d} | Dir: {car.direction:4s} | Load: {car.load_percent}% | Comm: {alive}")
                elif cmd == 'f':
                    self.trigger_fire_alarm()
                elif cmd == 'q':
                    self.running = False
            except EOFError:
                # stdin closed (systemd service / hidden window): keep running without commands.
                print("stdin closed: continuing headless.")
                try:
                    while self.running:
                        time.sleep(1.0)
                except KeyboardInterrupt:
                    self.running = False
            except KeyboardInterrupt:
                self.running = False

        self.client.loop_stop()
        self.client.disconnect()
        print("Central Controller stopped.")

def main():
    # Attempt to load broker info from local-secrets.json
    secrets_path = os.path.join(os.path.dirname(__file__), "local-secrets.json")
    user = "elevator"
    password = ""
    if os.path.exists(secrets_path):
        try:
            with open(secrets_path, "r") as f:
                sec = json.load(f)
                user = sec.get("mqttUser", user)
                password = sec.get("mqttPassword", password)
        except Exception:
            pass

    parser = argparse.ArgumentParser(description="CN360 Central Control Unit")
    parser.add_argument("--broker", type=str, default="127.0.0.1", help="MQTT Broker host")
    parser.add_argument("--port", type=int, default=1883, help="MQTT Broker port")
    parser.add_argument("--user", type=str, default=user, help="MQTT username")
    parser.add_argument("--password", type=str, default=password, help="MQTT password")
    args = parser.parse_args()

    controller = CentralController(broker=args.broker, port=args.port, user=args.user, password=args.password)
    controller.run()

if __name__ == "__main__":
    main()

