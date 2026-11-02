# Windows + ESP32 elevator mockup

> **หมายเหตุ (2 พ.ย. 2026):** เอกสารนี้เขียนก่อนอัปเดตล่าสุด ส่วนที่กล่าวถึงเส้นทางตายตัวชั้น 4→16, ปุ่มเรียกยืนยันชั้นเดียว และการใช้ Windows hotspot ถูกแทนที่แล้ว ดู [README.md](../README.md) สำหรับพฤติกรรมปัจจุบัน


The Windows computer permanently replaces the Raspberry Pi in this version.

**Handoff status, 2026-10-29:** the real ESP32/MQTT/dashboard journey has succeeded, including pickup and arrival, but intermittent Wi-Fi transmission gaps remain unresolved. A later strict continuity test did not complete successfully. Prefer-2.4-GHz settings do not prove the issue is fixed; the computer was observed roaming back to 5 GHz. Do not present this version as fully stable. See the [Thai presentation guide](TEAM_PRESENTATION_GUIDE_TH.md).

```text
Button / LCD / LED <-> ESP32 -- Elevator-Mockup hotspot / MQTT --> Mosquitto on Windows
                            <-- requests -----          |
                                                  Node-RED
                                                      |
                                           Browser dashboard on Windows
```

## Use it

1. Keep the ESP32 powered through its USB cable. Existing LCD/LED/button wiring is unchanged.
2. Keep Windows connected to Wi-Fi and double-click `Start Mockup.cmd`. It enables the private `Elevator-Mockup` 2.4 GHz hotspot and starts the two servers in the background. Leave this computer awake with its hotspot enabled.
3. Open http://127.0.0.1:1880/dashboard/elevators.
4. Wait for the green ESP32 connected indicator and four current floor values.
5. Press the physical verification button or **ยืนยัน / เรียกลิฟต์** in the dashboard. At the initial positions C is selected, reaches hall floor 4, boards for 10 seconds, then travels to 16. Current floors update on both displays.
6. Press again at arrival or idle to create another passenger request from floor 4. Positions persist until ESP32 reset. Initially the first four jobs normally select C, A, D, B.

This version handles one active passenger journey at a time. It has fixed origin/destination, no real motors, RFID, overload sensing, fire input or physical safety controller.

## Installed components

- Existing Node.js 24.15.0.
- Node-RED 5.0.9 and FlowFuse Dashboard 1.34.1 installed locally in `windows/node-red` (not globally).
- Official Mosquitto 2.1.2 Windows binaries extracted from the verified installer to `windows/mosquitto`. Runs as a normal background process rather than a Windows service.
- Firmware: board `esp32dev`, hd44780 1.3.2, PubSubClient 2.8.0, ArduinoJson 7.4.3.

`Start Mockup.cmd` is idempotent: it starts servers when their ports are free. If another program occupies 1880 or 1883, stop that conflicting application before starting the mockup. It does not register automatic login startup. After a Windows restart run this shortcut again.

## Network settings

The working configuration uses the computer's **`Elevator-Mockup` private hotspot at 2.4 GHz**, with Mosquitto at **192.168.137.1:1883**. `Start Mockup.cmd` enables this hotspot automatically. Its randomly generated Wi-Fi password is stored locally in `windows/local-secrets.json` and already uploaded to the ESP32.

The original attempt joined `ENGR_Wifi`, but MQTT connections to the computer failed even with the matching Windows firewall rule. The private hotspot provides a working direct Wi-Fi path. The ESP32 no longer needs to join the campus SSID. The exact cause of the campus-network failure was not established.

Private local files:

- `include/secrets.h`: ESP32 Wi-Fi and broker settings, excluded by `.gitignore`.
- `windows/local-secrets.json`: generated MQTT credentials and Node-RED encryption secret, excluded by `.gitignore`.
- `windows/broker/passwords`: hashed broker password, excluded by `.gitignore`.
- `windows/node-red/flows_cred.json`: Node-RED broker credentials, excluded by `.gitignore`.

Changing the computer's upstream Wi-Fi does not normally change the private hotspot address. If the hotspot SSID, password or 192.168.137.1 address changes, update `include/secrets.h` and the corresponding values in `windows/local-secrets.json`, then upload through PlatformIO again. The firewall rule permits only local address 192.168.137.1 and remote subnet 192.168.137.0/24 for the project broker. Do not configure router port forwarding.

The firewall rule has already been installed. If it is removed, enable the hotspot first, double-click `Allow MQTT WiFi.cmd` and accept the Windows permission dialog. It creates `SimpleElevator-MQTT-WiFi`, allowing only the project Mosquitto program on TCP 1883 over the private hotspot. Node-RED binds to 127.0.0.1:1880, so no firewall opening is needed for the editor or dashboard.

`scripts/enable-hotspot.ps1` uses the Windows tethering API. It saves the former hotspot SSID/password/band under `hotspotPrevious` in the private local secrets file before configuring this project. It leaves an already-running hotspot on; if that hotspot has a different name, switch it off and run Start Mockup again. Windows manages the shared-network address; check it after unusual network changes. This setup does not disable hotspot power saving, so if Windows turns it off, run Start Mockup again.

This laptop's MediaTek adapter originally preferred 5 GHz for its upstream Wi-Fi. With the upstream on 5 GHz and hotspot on 2.4 GHz, the real test showed long MQTT transmission gaps. Setup changed **Wi-Fi adapter → Preferred Band** to **Prefer 2.4GHz band**, and saved the former value in `wifiAdapterPreviousBand` in the private secrets file. This preference affects the computer's normal Wi-Fi selection too. The optional `scripts/restore-wifi-preference.ps1`, run as Administrator, restores it; restoring 5 GHz may bring back the hotspot issue. The hotspot startup script also uses the Windows per-session `TetheringOverStation` preference.

MQTT here is a local demonstration connection on port 1883 with password authentication, without TLS. The password is unique to this mockup; do not reuse another account password. For demonstrations on untrusted/shared Wi-Fi use an isolated private hotspot or add TLS before carrying sensitive data.

## Flows and messages

The editable flow is `windows/node-red/flows.json`. The visible functions are also kept as `monitor-function.js` and `request-function.js`; the Vue dashboard template is `dashboard.html`. After editing these source files, run `node scripts/generate-flows.cjs` with Node-RED stopped, then start it again. Normal flow-editor edits take effect with Deploy and do not automatically update the separate source templates.

| Topic | Direction | Retained | Contents |
|---|---|---|---|
| `elevator/v1/mockup/state` | ESP32 → Node-RED | yes | All four floors, assigned car, phase, origin/destination, LCD rows, boot ID and uptime |
| `elevator/v1/mockup/events` | ESP32 → Node-RED | no | Assignment, pickup, travel, arrival and request responses |
| `elevator/v1/mockup/request` | Node-RED → ESP32 | no | Verification request with unique ID, board boot ID and issuing uptime |
| `elevator/v1/mockup/availability` | ESP32/broker → Node-RED | yes | Online birth / offline last will |

The ESP32 publishes on changes and once a second. The dashboard marks data offline after 3.5 seconds without a fresh, non-retained state. Retained values alone never prove liveness. Commands expire after 5 seconds, must match the current boot ID and are deduplicated using a bounded 32-ID cache. The dashboard blocks commands while offline, while a trip is active or while awaiting a reply. Replies report accepted, busy or expired. Events are transient and are not replayed after a connection loss.

Network I/O runs in its own FreeRTOS task with queues; the main Arduino loop exclusively owns the simulation and LCD. MQTT reconnects do not block the physical button or floor timer.

## Troubleshooting

- No ESP32 connection: inspect the PlatformIO Serial Monitor at 115200. `Wi-Fi connected` proves Wi-Fi association; `MQTT connected` proves the path to Windows and authentication.
- `MQTT connection failed (-2)`: confirm the `Elevator-Mockup` hotspot is on and Windows has address 192.168.137.1. Check the project firewall rule. Run Start Mockup again if Windows has turned the hotspot off.
- If floor updates freeze intermittently, confirm Windows is actually connected on 2.4 GHz; the laptop originally preferred 5 GHz. The ESP32 network diagnostic command `n` reports its worker age, connection status and free heap.
- MQTT connects but no floors: inspect `windows/logs/node-red.log`, and the MQTT nodes in the flow editor.
- Servers do not start: inspect `windows/logs/mosquitto-error.log` and `node-red-error.log`.
- LCD garbled: use the existing serial diagnostics (`i`, `o`, `t`), adjust contrast and check wiring/power. A successful memory test does not guarantee visual readability.
- After changing source flow files: stop only this project's Node-RED process, regenerate, then restart. Do not terminate unrelated Node processes.

## Validation commands

```text
pio run
pio run --target upload --upload-port COM3
node test/dashboard_test.cjs
node scripts/check-dashboard.cjs
node scripts/check-mqtt-auth.cjs
node scripts/test-live-journey.cjs
```

The C++ simulation tests remain in `test/sim_test.cpp`. `scripts/check-dashboard.cjs` requires the local Playwright test package and installed Microsoft Edge. The read-only endpoint http://127.0.0.1:1880/mockup/status exposes the last received state for checking this local installation.

`test-live-journey.cjs` requires the real board online and idle/arrived. It clicks the actual dashboard verification button, observes telemetry from the ESP32 through arrival, and checks busy, expired and duplicate commands. It does not inject simulated telemetry. Allow roughly one minute for the physical mockup journey.

## Official references

- [Node-RED on Windows](https://nodered.org/docs/getting-started/windows)
- [Mosquitto Windows downloads](https://mosquitto.org/download/)
- [Mosquitto listener and authentication settings](https://mosquitto.org/man/mosquitto-conf-5.html)
- [FlowFuse Dashboard](https://dashboard.flowfuse.com/getting-started.html)
