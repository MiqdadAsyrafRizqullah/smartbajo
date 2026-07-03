#pragma once
// ============== WiFi ==============
#define WIFI_SSID     "Bay"
#define WIFI_PASSWORD "jeruksonggi"
// ============== MQTT (broker publik, tanpa TLS) ==============
#define MQTT_HOST      "broker.hivemq.com"
#define MQTT_PORT      1883
#define MQTT_USERNAME  ""
#define MQTT_PASSWORD  ""
#define MQTT_CLIENT_ID "esp32-iot"
// ============== MQTT Topic (per sensor) ==============
#define TOPIC_WATER_TEMP  "esp32/sensor/waterTemp"
#define TOPIC_TDS         "esp32/sensor/tds"
#define TOPIC_TANK1       "esp32/sensor/tank1"
#define TOPIC_TANK2       "esp32/sensor/tank2"
#define TOPIC_AIR_TEMP    "esp32/sensor/airTemp"
#define TOPIC_HUMIDITY    "esp32/sensor/humidity"
// ============== MQTT Topic (status/atribut) ==============
#define TOPIC_WATER_TEMP_STATUS  "esp32/sensor/waterTemp/status"
#define TOPIC_AIR_TEMP_STATUS    "esp32/sensor/airTemp/status"
#define TOPIC_HUMIDITY_STATUS    "esp32/sensor/humidity/status"
#define TOPIC_TANK1_STATUS       "esp32/sensor/tank1/status"
#define TOPIC_TANK2_STATUS       "esp32/sensor/tank2/status"
#define TOPIC_PUMP_CMD     "esp32/pump/cmd"
#define TOPIC_PUMP_STATUS  "esp32/pump/status"
#define TOPIC_PUMP_MODE    "esp32/pump/mode"

ini iai secrets.h, respon padat