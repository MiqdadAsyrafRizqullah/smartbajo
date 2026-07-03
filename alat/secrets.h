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
#define TOPIC_WATER_TEMP  "smartbajo/sensor/waterTemp"
#define TOPIC_TDS         "smartbajo/sensor/tds"
#define TOPIC_TANK1       "smartbajo/sensor/tank1"
#define TOPIC_TANK2       "smartbajo/sensor/tank2"
#define TOPIC_AIR_TEMP    "smartbajo/sensor/airTemp"
#define TOPIC_HUMIDITY    "smartbajo/sensor/humidity"
// ============== MQTT Topic (status/atribut) ==============
#define TOPIC_WATER_TEMP_STATUS  "smartbajo/sensor/waterTemp/status"
#define TOPIC_AIR_TEMP_STATUS    "smartbajo/sensor/airTemp/status"
#define TOPIC_HUMIDITY_STATUS    "smartbajo/sensor/humidity/status"
#define TOPIC_TANK1_STATUS       "smartbajo/sensor/tank1/status"
#define TOPIC_TANK2_STATUS       "smartbajo/sensor/tank2/status"
#define TOPIC_PUMP_CMD     "smartbajo/pump/cmd"
#define TOPIC_PUMP_STATUS  "smartbajo/pump/status"
#define TOPIC_PUMP_MODE    "smartbajo/pump/mode"

ini iai secrets.h, respon padat