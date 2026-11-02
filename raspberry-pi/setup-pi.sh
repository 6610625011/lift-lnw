#!/usr/bin/env bash
# Raspberry Pi (Raspberry Pi OS / Debian) setup for the elevator mockup:
#   Mosquitto broker + Node-RED dashboard on the SAME Pi, ESP32 joins the same Wi-Fi/LAN.
# Run from the project root:   bash raspberry-pi/setup-pi.sh
# Idempotent. Needs sudo. NOT yet tested on a real Pi (written on Windows).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SECRETS="$ROOT/windows/local-secrets.json"
RED="$ROOT/windows/node-red"
PI_IP="$(hostname -I | awk '{print $1}')"
RUN_USER="${SUDO_USER:-$USER}"

echo "== 1/6 packages"
sudo apt-get update -y
sudo apt-get install -y mosquitto mosquitto-clients openssl curl ca-certificates

echo "== 2/6 Node.js >= 20"
need_node=1
if command -v node >/dev/null 2>&1; then
  [ "$(node -p 'process.versions.node.split(".")[0]')" -ge 20 ] && need_node=0
fi
if [ "$need_node" = 1 ]; then
  curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
  sudo apt-get install -y nodejs
fi
node -v

echo "== 3/6 secrets ($SECRETS)"
if [ ! -f "$SECRETS" ]; then
  cat > "$SECRETS" <<JSON
{
  "mqttHost": "$PI_IP",
  "mqttUser": "elevator",
  "mqttPassword": "$(openssl rand -hex 16)",
  "credentialSecret": "$(openssl rand -hex 32)",
  "networkMode": "existing-wifi"
}
JSON
  chmod 600 "$SECRETS"
  echo "created new random credentials"
fi
MQTT_USER="$(node -p "require('$SECRETS').mqttUser")"
MQTT_PASS="$(node -p "require('$SECRETS').mqttPassword")"

echo "== 4/6 Mosquitto"
sudo mosquitto_passwd -b -c /etc/mosquitto/elevator.passwd "$MQTT_USER" "$MQTT_PASS"
sudo chown mosquitto: /etc/mosquitto/elevator.passwd 2>/dev/null || true
sudo chmod 640 /etc/mosquitto/elevator.passwd
printf 'user %s\ntopic readwrite elevator/v1/mockup/#\n' "$MQTT_USER" | sudo tee /etc/mosquitto/elevator.acl >/dev/null
sudo tee /etc/mosquitto/conf.d/elevator.conf >/dev/null <<CONF
# Local-network demo broker (password + ACL, no TLS). Do not expose to the internet.
listener 1883 0.0.0.0
allow_anonymous false
password_file /etc/mosquitto/elevator.passwd
acl_file /etc/mosquitto/elevator.acl
CONF
sudo systemctl enable --now mosquitto
sudo systemctl restart mosquitto

echo "== 5/6 Node-RED (project copy in windows/node-red, used as-is)"
( cd "$RED" && npm ci )
( cd "$ROOT" && node scripts/generate-flows.cjs )
sudo tee /etc/systemd/system/elevator-nodered.service >/dev/null <<UNIT
[Unit]
Description=Elevator mockup Node-RED dashboard
After=network-online.target mosquitto.service
Wants=network-online.target

[Service]
User=$RUN_USER
WorkingDirectory=$ROOT
Environment=NR_HOST=0.0.0.0
ExecStart=$(command -v node) $RED/node_modules/node-red/red.js --userDir $RED --settings $RED/settings.js
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
UNIT
sudo systemctl daemon-reload
sudo systemctl enable --now elevator-nodered
sudo systemctl restart elevator-nodered

echo "== 6/6 done"
cat <<MSG

Dashboard (any device on the same network): http://$PI_IP:1880/dashboard/elevators
Put these in include/secrets.h on the PC, then upload the ESP32 firmware:
  mqttHost     = "$PI_IP"
  mqttUser     = "$MQTT_USER"
  mqttPassword = "$MQTT_PASS"
  wifiSsid / wifiPassword = the SAME Wi-Fi network the Pi is on (2.4 GHz)
Check:  systemctl status mosquitto elevator-nodered
Live:   mosquitto_sub -h localhost -u $MQTT_USER -P '<password>' -t 'elevator/v1/mockup/#' -v
Test:   node scripts/test-live-select.cjs 16
MSG
