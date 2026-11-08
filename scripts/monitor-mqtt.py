#!/usr/bin/env python3
"""
CN360 Live MQTT Monitor
Subscribes to all 'elevator/v1/#' topics and displays incoming messages in real-time.
"""

import json
import os
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Error: paho-mqtt not installed. Run: pip install paho-mqtt")
    sys.exit(1)

# Load credentials from local-secrets.json if available
secrets_path = os.path.join(os.path.dirname(__file__), "..", "windows", "local-secrets.json")
broker_host = os.environ.get("ELEVATOR_MQTT_HOST", "127.0.0.1")
broker_port = 1883
mqtt_user = os.environ.get("ELEVATOR_MQTT_USER", "elevator")
mqtt_pass = os.environ.get("ELEVATOR_MQTT_PASS", "")  # never hard-code; read from local-secrets.json below

if os.path.exists(secrets_path):
    try:
        with open(secrets_path, "r") as f:
            sec = json.load(f)
            mqtt_user = sec.get("mqttUser", mqtt_user)
            mqtt_pass = sec.get("mqttPassword", mqtt_pass)
    except Exception:
        pass

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("=" * 65, flush=True)
        print(" Connected to MQTT Broker! Listening on: elevator/v1/#", flush=True)
        print(" Press Ctrl+C to stop.", flush=True)
        print("=" * 65, flush=True)
        client.subscribe("elevator/v1/#")
    else:
        print(f"Failed to connect, return code {rc}", flush=True)

def on_message(client, userdata, msg):
    topic = msg.topic
    payload_str = msg.payload.decode("utf-8", errors="replace")
    now_str = time.strftime("%H:%M:%S")

    try:
        data = json.loads(payload_str)
        if topic == "elevator/v1/mockup/state":
            phase = data.get("phase", "?")
            assigned = data.get("assigned", "-")
            floors = data.get("floors", {})
            lcd = " | ".join(data.get("lcd", []))
            print(f"[{now_str}] [STATE] Phase: {phase:<8} | Car: {assigned} | Floors: {floors} | LCD: [{lcd}]", flush=True)
        elif topic == "elevator/v1/central/heartbeat":
            pass
        elif topic == "elevator/v1/hall/request":
            print(f"\n[{now_str}] >>> [HALL REQ] {data}", flush=True)
        elif topic == "elevator/v1/hall/response":
            print(f"[{now_str}] <<< [HALL RES] {data}\n", flush=True)
        elif topic == "elevator/v1/mockup/request":
            print(f"[{now_str}] >>> [MOCKUP REQ] {data}", flush=True)
        elif topic == "elevator/v1/mockup/events":
            print(f"[{now_str}] *** [EVENT] Type: {data.get('type')} (source: {data.get('source')})", flush=True)
        else:
            print(f"[{now_str}] [{topic}] {payload_str}", flush=True)
    except Exception:
        print(f"[{now_str}] [{topic}] {payload_str}", flush=True)

def main():
    client = mqtt.Client(client_id="mqtt-live-monitor")
    if mqtt_user:
        client.username_pw_set(mqtt_user, mqtt_pass)

    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(broker_host, broker_port, 60)
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nMonitor stopped.")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
