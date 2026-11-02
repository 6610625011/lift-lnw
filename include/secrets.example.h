#pragma once
// Copy to secrets.h and configure the same reachable Wi-Fi network as Windows.
namespace Secrets {
constexpr char wifiSsid[] = "YOUR_2_4_GHZ_WIFI";
constexpr char wifiPassword[] = "YOUR_WIFI_PASSWORD";
constexpr char mqttHost[] = "192.168.1.100";
constexpr char mqttUser[] = "elevator";
constexpr char mqttPassword[] = "MATCH_WINDOWS_LOCAL_SECRETS";
}
