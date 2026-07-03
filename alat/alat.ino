/*
   ESP32 IoT Multi Sensor
   MQTT JSON Publisher + Pump Control (Single Tank Sensor)

   Sensor:
   - TDS
   - DS18B20
   - Ultrasonik (Tangki)   -> JSN-SR04T v3.0 Trigger/Echo (pulseIn)
   - SHT30/SHT31

   Aktuator:
   - Pompa (MOSFET PWM)
   - Kondisi: tangki kering -> pompa mati (harus diisi ulang)
   - Bacaan error / nyentuh batas minimum sensor -> diabaikan, pakai nilai valid terakhir
*/

#include <WiFi.h>
#include <esp_wifi.h>
#include <PubSubClient.h>

#include <Wire.h>
#include <Adafruit_SHT31.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include <Adafruit_ADS1X15.h>

#include "secrets.h"

//////////////////////////////
// Node Identity (MAC asli ESP32, tanpa cloning)
//////////////////////////////

String nodeId;   // di-set di setup(), dipakai untuk hostname WiFi & MQTT client ID

//////////////////////////////
// MQTT
//////////////////////////////

WiFiClient espClient;
PubSubClient client(espClient);

//////////////////////////////
// PIN (susunan sesuai kode sebelumnya)
//////////////////////////////

#define ONE_WIRE_BUS 4
#define TRIG1        18   // Sensor Tangki TRIG
#define ECHO1        21   // Sensor Tangki ECHO
#define SDA_PIN      23
#define SCL_PIN      22
#define PUMP_PIN     27

//////////////////////////////
// PWM Pompa
//////////////////////////////

#define PWM_CHANNEL   0
#define PWM_FREQ      1000
#define PWM_RES       8

//////////////////////////////
// Sensor Range (JSN-SR04T-3.0)
//////////////////////////////

#define SENSOR_MIN_CM   21.0
#define SENSOR_MAX_CM   600.0

//////////////////////////////
// Kontrol Pompa
//////////////////////////////

#define PUMP_SPEED_ON   255   // speed pompa saat aktif (0-255)

// Hysteresis kering -> aman: tangki dianggap kering di atas ambang ini
#define TANK_OFF_CM     75.0

// Penjadwalan flush harian (mode AUTO saja): pulsa tetap 30 detik tiap 24 jam,
// dihitung dari uptime (bukan jam dinding, karena tidak ada RTC/NTP).
#define FLUSH_INTERVAL_MS   86400000UL   // 24 jam
#define FLUSH_DURATION_MS   30000UL      // 30 detik

//////////////////////////////

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

Adafruit_SHT31 sht31;

Adafruit_ADS1115 ads;

//////////////////////////////

unsigned long lastPublish = 0;

float lastTank1 = 75;   // default aman saat boot: anggap tangki kurang/kering
bool  pumpActive = false;
bool  pumpManualMode = false;   // true = dikontrol manual dari app, false = auto (sensor)

unsigned long lastFlushMillis  = 0;      // kapan flush harian terakhir SELESAI
unsigned long flushStartMillis = 0;
bool flushInProgress = false;

//////////////////////////////
// WiFi
//////////////////////////////

void connectWiFi()
{
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(300);

  int bestIndex = -1;
  int bestRSSI  = -999;

  while (bestIndex == -1)
  {
    Serial.print("Scanning WiFi...");
    int n = WiFi.scanNetworks();
    Serial.println(" Done");

    for (int i = 0; i < n; i++)
    {
      if (String(WiFi.SSID(i)) == WIFI_SSID)
      {
        Serial.printf("  Found: %s CH:%d RSSI:%d\n",
          WiFi.BSSIDstr(i).c_str(), WiFi.channel(i), WiFi.RSSI(i));

        if (WiFi.RSSI(i) > bestRSSI)
        {
          bestRSSI  = WiFi.RSSI(i);
          bestIndex = i;
        }
      }
    }

    if (bestIndex == -1)
    {
      Serial.println("SSID not found, retry in 3s...");
      WiFi.scanDelete();
      delay(3000);
    }
  }

  uint8_t* bestBSSID = WiFi.BSSID(bestIndex);
  int      bestCH    = WiFi.channel(bestIndex);

  Serial.printf("Best: %s CH:%d RSSI:%d\n",
    WiFi.BSSIDstr(bestIndex).c_str(), bestCH, bestRSSI);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, bestCH, bestBSSID);
  WiFi.scanDelete();

  Serial.print("Connecting WiFi");
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - t > 15000)
    {
      Serial.println("\nTimeout, rescan...");
      WiFi.disconnect(true);
      delay(500);
      connectWiFi();
      return;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

//////////////////////////////
// WiFi Maintain (non-blocking, dipakai di loop())
//////////////////////////////

unsigned long lastWifiAttempt = 0;
#define WIFI_RETRY_MS   10000UL

// connectWiFi() sengaja tidak dipanggil lagi dari sini karena isinya blocking
// (scan + tunggu sampai ketemu). Di runtime cukup WiFi.begin() ulang pakai
// kredensial tersimpan, non-blocking, dicoba tiap WIFI_RETRY_MS.
void maintainWiFi()
{
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWifiAttempt < WIFI_RETRY_MS) return;
  lastWifiAttempt = millis();

  Serial.println("[WiFi] Terputus, coba reconnect...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

//////////////////////////////
// MQTT Callback - Kontrol Manual Pompa
//////////////////////////////

// Payload topic TOPIC_PUMP_CMD (dari app/dashboard):
//   "ON"   -> paksa pompa nyala (manual), selama tangki tidak kering
//   "OFF"  -> paksa pompa mati (manual)
//   "AUTO" -> balik ke kontrol otomatis berdasar sensor
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();

  if (String(topic) != TOPIC_PUMP_CMD) return;

  Serial.printf("[MQTT] %s -> %s\n", topic, msg.c_str());

  if (msg == "ON")
  {
    pumpManualMode = true;
    pumpActive = true;
  }
  else if (msg == "OFF")
  {
    pumpManualMode = true;
    pumpActive = false;
  }
  else if (msg == "AUTO")
  {
    pumpManualMode = false;
  }
}

//////////////////////////////
// MQTT Maintain (non-blocking)
//////////////////////////////

unsigned long lastMqttAttempt = 0;
#define MQTT_RETRY_MS   5000UL

// Dipanggil tiap loop(). Tidak pernah block -- kalau WiFi/MQTT putus,
// cuma coba connect ulang tiap MQTT_RETRY_MS, sisanya langsung lewat.
void mqttMaintain()
{
  if (WiFi.status() != WL_CONNECTED) return;

  if (client.connected())
  {
    client.loop();
    return;
  }

  if (millis() - lastMqttAttempt < MQTT_RETRY_MS) return;
  lastMqttAttempt = millis();

  String clientId = nodeId;
  Serial.print("Connecting MQTT...");

  if (client.connect(clientId.c_str()))
  {
    Serial.println(" Connected as " + clientId);
    client.subscribe(TOPIC_PUMP_CMD);
  }
  else
  {
    Serial.print(" Failed, rc=");
    Serial.println(client.state());
  }
}

//////////////////////////////
// Sensor Validasi
//////////////////////////////

// raw == 0 (timeout) atau menyentuh/lewat batas MIN/MAX sensor dianggap error -> diabaikan, pakai nilai valid terakhir.
float readValidDistance(float raw, float &lastValue)
{
  if (raw <= SENSOR_MIN_CM || raw > SENSOR_MAX_CM || raw == 0)
    return lastValue;
  lastValue = raw;
  return raw;
}

//////////////////////////////
// JSN-SR04T GPIO Trigger/Echo (mode v3.0, terbukti jalan di ESP8266)
//////////////////////////////

// Baca jarak (cm) dari sensor trigger/echo. Return 0 kalau timeout,
// biar ditangani sebagai "invalid" oleh readValidDistance().
float readUltrasonicCM(int trigPin, int echoPin)
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(20);
  digitalWrite(trigPin, LOW);

  unsigned long pulse = pulseIn(echoPin, HIGH, 60000UL);   // timeout 60ms
  if (pulse == 0) return 0.0;

  // Kecepatan suara 343 m/s
  return pulse * 0.0343F / 2.0F;
}

//////////////////////////////
// Klasifikasi Atribut Sensor
//////////////////////////////

// Kelembaban udara (%RH)
const char* classifyHumidity(float rh)
{
  return (rh < 60.0) ? "Kering" : "Lembab";
}

// Suhu udara (°C)
const char* classifyAirTemp(float t)
{
  return (t < 28.0) ? "Sejuk" : "Panas";
}

// Suhu air (°C)
const char* classifyWaterTemp(float t)
{
  if (t < 24.0) return "Dingin";
  if (t <= 30.0) return "Normal";
  return "Panas";
}

// Status tangki: Terisi / Kering (pakai ambang TANK_OFF_CM yang sama dgn kontrol pompa)
const char* classifyTank1(float cm)
{
  return (cm >= TANK_OFF_CM) ? "Kering" : "Terisi";
}

//////////////////////////////
// Kontrol Pompa (on/off + hysteresis)
//////////////////////////////

// Tangki kering (>= TANK_OFF_CM) -> pompa mati mutlak & balik ke mode AUTO,
// berlaku juga saat sedang manual (safety tidak bisa dioverride dari app).
// Mode AUTO defaultnya OFF -- yang boleh menyalakan pompa cuma jadwal flush
// (updateScheduledFlush) atau perintah manual dari app.
void updatePumpState(float tank1)
{
  if (tank1 >= TANK_OFF_CM)
  {
    pumpActive = false;
    pumpManualMode = false;   // paksa balik ke auto, safety tetap jalan
    return;
  }

  if (pumpManualMode) return;   // ikuti perintah manual dari app

  pumpActive = false;   // auto normal: default OFF
}

//////////////////////////////
// Penjadwalan Flush Harian (mode AUTO)
//////////////////////////////

// Jamin pompa mengalirkan air minimal FLUSH_DURATION_MS setiap FLUSH_INTERVAL_MS,
// walau level air sedang di zona "Cukup" yang biasanya tidak memicu pompa nyala.
// Tidak berjalan saat manual mode, dan otomatis batal/ditunda kalau tangki kering
// (safety kering tetap prioritas, dijalankan oleh updatePumpState()).
void updateScheduledFlush(float tank1)
{
  if (pumpManualMode)
  {
    flushInProgress = false;
    return;
  }

  if (tank1 >= TANK_OFF_CM)
  {
    // tangki kering saat harusnya flush -> batalkan, coba lagi di siklus interval berikutnya
    flushInProgress = false;
    return;
  }

  if (!flushInProgress)
  {
    if (millis() - lastFlushMillis >= FLUSH_INTERVAL_MS)
    {
      flushInProgress  = true;
      flushStartMillis = millis();
      Serial.println("[FLUSH] Jadwal harian mulai, alirkan air minimal 30 detik...");
    }
    return;
  }

  // Flush sedang berlangsung -> paksa pompa nyala, timpa hasil hysteresis di atas
  pumpActive = true;

  if (millis() - flushStartMillis >= FLUSH_DURATION_MS)
  {
    flushInProgress = false;
    lastFlushMillis = millis();
    // Flush = pulsa tetap 30 detik: paksa mati di sini, apa pun kondisi tangki.
    // Auto normal baru boleh menyalakan lagi di siklus berikutnya (hysteresis biasa).
    pumpActive = false;
    Serial.println("[FLUSH] Selesai, pompa dipaksa OFF.");
  }
}

//////////////////////////////
// Setup
//////////////////////////////

void setup()
{
  Serial.begin(115200);
  delay(500);

  Wire.begin(SDA_PIN, SCL_PIN);

  ds18b20.begin();

  sht31.begin(0x44);

  ads.begin();

  WiFi.mode(WIFI_STA);

  // Nama node diset manual di secrets.h (mis. NODE_NAME "esp32-iot-1"),
  // supaya gampang dibedain kalau ada beberapa unit ESP32.
  nodeId = MQTT_CLIENT_ID;
  Serial.println("[INFO] Node ID: " + nodeId);

  WiFi.setHostname(nodeId.c_str());

  connectWiFi();

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(mqttCallback);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(PUMP_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);

  // Setup pin trigger/echo JSN-SR04T (sensor tangki)
  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  digitalWrite(TRIG1, LOW);

  Serial.println("[INFO] Setup selesai, mulai loop utama.\n");
}

//////////////////////////////
// Loop
//////////////////////////////

void loop()
{
  // Non-blocking: sensor & pompa di bawah tetap jalan walau WiFi/MQTT putus.
  maintainWiFi();
  mqttMaintain();

  if (millis() - lastPublish > 5000)
  {
    lastPublish = millis();

    ///////////////////////////
    // DS18B20
    ///////////////////////////

    ds18b20.requestTemperatures();
    float waterTemp = ds18b20.getTempCByIndex(0);

    ///////////////////////////
    // SHT31
    ///////////////////////////

    float airTemp  = sht31.readTemperature();
    float humidity = sht31.readHumidity();

    ///////////////////////////
    // Ultrasonik JSN-SR04T Trigger/Echo (dengan validasi)
    ///////////////////////////

    float rawTank1 = readUltrasonicCM(TRIG1, ECHO1);
    float tank1 = readValidDistance(rawTank1, lastTank1);

    ///////////////////////////
    // TDS (via ADS1115 A0)
    ///////////////////////////

    int16_t raw     = ads.readADC_SingleEnded(0);
    float   voltage = ads.computeVolts(raw);
    float   tds     = voltage * 500.0;

    ///////////////////////////
    // Kontrol Pompa (on/off + hysteresis)
    ///////////////////////////

    updatePumpState(tank1);
    updateScheduledFlush(tank1);
    int pumpSpeed = pumpActive ? PUMP_SPEED_ON : 0;
    ledcWrite(PWM_CHANNEL, pumpSpeed);
    Serial.printf("[PUMP] active=%d speed=%d (tank1=%.1f)\n", pumpActive, pumpSpeed, tank1);

    ///////////////////////////
    // Publish per Topic
    ///////////////////////////

    char buf[32];

    snprintf(buf, sizeof(buf), "%.2f", waterTemp);
    client.publish(TOPIC_WATER_TEMP, buf);
    Serial.printf("[%s] %s\n", TOPIC_WATER_TEMP, buf);

    snprintf(buf, sizeof(buf), "%.2f", tds);
    client.publish(TOPIC_TDS, buf);
    Serial.printf("[%s] %s\n", TOPIC_TDS, buf);

    snprintf(buf, sizeof(buf), "%.2f", airTemp);
    client.publish(TOPIC_AIR_TEMP, buf);
    Serial.printf("[%s] %s\n", TOPIC_AIR_TEMP, buf);

    snprintf(buf, sizeof(buf), "%.2f", humidity);
    client.publish(TOPIC_HUMIDITY, buf);
    Serial.printf("[%s] %s\n", TOPIC_HUMIDITY, buf);

    ///////////////////////////
    // Publish Atribut/Status
    ///////////////////////////

    const char* waterStatus = classifyWaterTemp(waterTemp);
    client.publish(TOPIC_WATER_TEMP_STATUS, waterStatus);
    Serial.printf("[%s] %s\n", TOPIC_WATER_TEMP_STATUS, waterStatus);

    const char* airStatus = classifyAirTemp(airTemp);
    client.publish(TOPIC_AIR_TEMP_STATUS, airStatus);
    Serial.printf("[%s] %s\n", TOPIC_AIR_TEMP_STATUS, airStatus);

    const char* humidityStatus = classifyHumidity(humidity);
    client.publish(TOPIC_HUMIDITY_STATUS, humidityStatus);
    Serial.printf("[%s] %s\n", TOPIC_HUMIDITY_STATUS, humidityStatus);

    const char* tank1Status = classifyTank1(tank1);
    client.publish(TOPIC_TANK1_STATUS, tank1Status);
    Serial.printf("[%s] %s\n", TOPIC_TANK1_STATUS, tank1Status);

    client.publish(TOPIC_PUMP_STATUS, pumpActive ? "ON" : "OFF");
    Serial.printf("[%s] %s\n", TOPIC_PUMP_STATUS, pumpActive ? "ON" : "OFF");

    client.publish(TOPIC_PUMP_MODE, pumpManualMode ? "MANUAL" : "AUTO");
    Serial.printf("[%s] %s\n", TOPIC_PUMP_MODE, pumpManualMode ? "MANUAL" : "AUTO");
  }
}