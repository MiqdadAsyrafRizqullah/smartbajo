  /*
    ESP32 DevKit - Multi-Sensor IoT Node  (v2)
    -------------------------------------------
    Sensor:
      - SHT3x          : suhu & kelembapan udara   (I2C, 0x44/0x45)
      - DS18B20        : suhu air                  (OneWire, GPIO 4)
      - JSN-SR04T x5   : level air / jarak         (ultrasonic, 5 pasang Trig/Echo)
      - ADS1115        : ADC 16-bit                (I2C, 0x48)
      - TDS Meter v1   : kualitas air (ppm)        (analog via ADS1115 ch0)

    Aktuator:
      - MOSFET LR7843 module 5-pin : kontrol pompa air
        * Pompa HIDUP  : salah satu sensor JSN membaca jarak >= 35 cm
        * Pompa MATI   : SEMUA sensor JSN membaca jarak < 30 cm

    Koneksi:
      - WiFi R.NET (captive portal MikroTik) -> auto-login via HTTP POST voucher
      - MQTT over TLS -> HiveMQ Cloud (port 8883)
      - InfluxDB Cloud Serverless (HTTPS / Line Protocol)

    OTA:
      - ArduinoOTA via WiFi (password: ota_smartbajo)
      - Port 3232 (default). Upload di PlatformIO dengan upload_protocol = espota

    Framework : Arduino (PlatformIO)
    ============================================================
    CATATAN PIN JSN-SR04T:
      Echo JSN-SR04T bertegangan 5V. Gunakan voltage divider
      (R1=1kΩ seri ke Echo, R2=2kΩ ke GND) sebelum masuk GPIO ESP32,
      ATAU gunakan level-shifter, agar GPIO 3.3V tidak rusak.
  */
 #include <Arduino.h>
 #include <WiFi.h>
 //include <WiFiClientSecure.h>
 #include <HTTPClient.h>
 #include <PubSubClient.h>
 #include <ArduinoOTA.h>
 #include <Wire.h>
 #include <Adafruit_SHT31.h>
 #include <NewPing.h>
 #include <OneWire.h>
 #include <DallasTemperature.h>
 #include <Adafruit_ADS1X15.h>
 #include <ArduinoJson.h>
 #include <InfluxDbClient.h>
 #include <InfluxDbCloud.h>
 #include <WiFiClient.h>
  #include "secrets.h"

  // =====================================================================
  //  KONFIGURASI PIN
  // =====================================================================

  // I2C (SHT3x + ADS1115) -> pin default ESP32
  #define I2C_SDA_PIN 21
  #define I2C_SCL_PIN 22
#define TRIG1 5
#define ECHO1 18

#define TRIG2 19
#define ECHO2 21
  // DS18B20 (OneWire) - pasang pull-up 4.7kΩ antara DATA dan VCC
  #define ONEWIRE_PIN 4

  // ---- MOSFET LR7843 (pompa) ----
  // Hubungkan pin GATE modul LR7843 ke GPIO ini.
  // HIGH = pompa HIDUP, LOW = pompa MATI.
  #define PUMP_PIN 19

  // Channel ADS1115 untuk TDS Meter v1
  #define TDS_ADS_CHANNEL 0

  // =====================================================================
  //  KONFIGURASI APLIKASI
  // =====================================================================
  #define PUBLISH_INTERVAL_MS 5000UL
  #define WIFI_RECONNECT_DELAY 500UL
  #define MQTT_RECONNECT_DELAY 2000UL

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
#define MQTT_TOPIC_STATUS  "esp32/status"


  // ---- InfluxDB ----
  #define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
  #define INFLUXDB_TOKEN "LoBd_3ful6J9nCpMHLeiVzXDCVyQO1cO28545-h0Gl695Ndls19mmz3ANfJZXXTvwzvAkhTliNCX_OWRHabD0Q=="
  #define INFLUXDB_ORG "miqdadwajo@gmail.com"
  #define INFLUXDB_BUCKET "smartbajo"
  #define TZ_INFO "WITA-8"

  // OTA password (ubah sesuai kebutuhan)
  #define OTA_PASSWORD "ota_smartbajo"
  #define OTA_HOSTNAME "esp32-smartnode-01"

  // =====================================================================
  //  KONFIGURASI HOTSPOT LOGIN (R.NET / MikroTik Captive Portal)
  // =====================================================================
  // Isi VOUCHER_CODE dengan kode voucher R.NET (format: XXX-XXX)
  // Pada MikroTik Hotspot, kode voucher dipakai sebagai username DAN password.
  #define VOUCHER_CODE "30RSmartBajo" // <-- GANTI dengan voucher aktif

  // IP gateway diambil otomatis dari DHCP saat runtime — tidak perlu hardcode
  #define HOTSPOT_LOGIN_PATH "/login" // path form login MikroTik (default)

  // Interval re-login jika sesi voucher expired (ms). Default 55 menit.
  #define HOTSPOT_RELOGIN_INTERVAL_MS (55UL * 60UL * 1000UL)

  // =====================================================================
  //  OBJEK GLOBAL
  // =====================================================================
  WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

  Adafruit_SHT31 sht3x = Adafruit_SHT31();
  OneWire oneWire(ONEWIRE_PIN);
  DallasTemperature ds18b20(&oneWire);
  NewPing sonar1(TRIG1,ECHO1,400);
NewPing sonar2(TRIG2,ECHO2,400);
  Adafruit_ADS1115 ads;

  InfluxDBClient influxClient(INFLUXDB_URL, INFLUXDB_ORG,
                              INFLUXDB_BUCKET, INFLUXDB_TOKEN,
                              InfluxDbCloud2CACert);
  Point sensorData("monitoring");

  bool sht3xReady = false;
  bool ds18b20Ready = false;
  bool adsReady = false;
  bool pumpState = false; // status pompa saat ini

  unsigned long lastPublish = 0;
  unsigned long lastHotspotLogin = 0; // timestamp login hotspot terakhir

  // ADS1115 GAIN_TWOTHIRDS -> range +/-6.144V, 1 bit = 0.1875 mV
  const float ADS_VOLTS_PER_BIT = 0.1875F / 1000.0F;

  // =====================================================================
  //  DEBUG HELPER
  // =====================================================================
  const char *wifiStatusStr(wl_status_t s)
  {
    switch (s)
    {
    case WL_NO_SHIELD:
      return "NO_SHIELD";
    case WL_IDLE_STATUS:
      return "IDLE";
    case WL_NO_SSID_AVAIL:
      return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "SCAN_COMPLETED";
    case WL_CONNECTED:
      return "CONNECTED";
    case WL_CONNECT_FAILED:
      return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "DISCONNECTED";
    default:
      return "UNKNOWN";
    }
  }

  // =====================================================================
  //  WIFI
  // =====================================================================
  void connectWiFi()
  {
    if (WiFi.status() == WL_CONNECTED)
      return;

    Serial.println("\n[WiFi:DBG] ====== connectWiFi() START ======");
    Serial.printf("[WiFi:DBG] Status awal: %s (%d)\n",
                  wifiStatusStr(WiFi.status()), WiFi.status());
    Serial.printf("[WiFi:DBG] Target SSID : \"%s\"\n", WIFI_SSID);
    Serial.printf("[WiFi:DBG] Password    : \"%s\" (len=%d)\n",
                  strlen(WIFI_PASSWORD) == 0 ? "(kosong/open)" : "***", strlen(WIFI_PASSWORD));
    Serial.printf("[WiFi:DBG] MAC Address : %s\n", WiFi.macAddress().c_str());

    WiFi.disconnect(true);
    delay(300);
    WiFi.mode(WIFI_STA);
    Serial.println("[WiFi:DBG] Mode WIFI_STA OK, memulai WiFi.begin()...");

   uint8_t rnetBSSID[] = {
    0x72,0x11,0x41,0x2E,0x6A,0x3B
};

WiFi.begin(WIFI_SSID, NULL, 9, rnetBSSID);

    // Timeout diperpanjang 90 detik — sinyal lemah + DHCP MikroTik bisa lambat
    unsigned long start = millis();
    wl_status_t lastStatus = WL_IDLE_STATUS;
    int dotCount = 0;

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(WIFI_RECONNECT_DELAY);
      wl_status_t cur = WiFi.status();

      // Cetak setiap perubahan status
      if (cur != lastStatus)
      {
        Serial.printf("\n[WiFi:DBG] Status berubah: %s -> %s  (t=%lus)\n",
                      wifiStatusStr(lastStatus), wifiStatusStr(cur),
                      (millis() - start) / 1000);
        lastStatus = cur;

        // Jika sudah pasti gagal, jangan tunggu timeout
        if (cur == WL_NO_SSID_AVAIL)
        {
          Serial.println("[WiFi:DBG] SSID tidak ditemukan! Cek nama SSID di secrets.h");
          Serial.println("[WiFi:DBG] Restart dalam 3 detik...");
          delay(3000);
          ESP.restart();
        }
        if (cur == WL_CONNECT_FAILED)
        {
          Serial.println("[WiFi:DBG] Auth GAGAL (password salah?)");
          Serial.println("[WiFi:DBG] Restart dalam 3 detik...");
          delay(3000);
          ESP.restart();
        }
      }

      // Progress dot setiap 500ms
      Serial.print(".");
      dotCount++;
      if (dotCount % 20 == 0)
      {
        Serial.printf("  [%lus / RSSI: %d dBm]\n",
                      (millis() - start) / 1000, WiFi.RSSI());
      }

      if (millis() - start > 90000UL)
      {
        Serial.printf("\n[WiFi:DBG] TIMEOUT 90 detik!\n");
        Serial.printf("[WiFi:DBG] Status terakhir : %s\n", wifiStatusStr(WiFi.status()));
        Serial.printf("[WiFi:DBG] RSSI terakhir   : %d dBm\n", WiFi.RSSI());
        Serial.printf("[WiFi:DBG] Channel         : %d\n", WiFi.channel());
        Serial.println("[WiFi:DBG] Kemungkinan penyebab:");
        Serial.println("  1. Sinyal terlalu lemah (RSSI < -85 dBm)");
        Serial.println("  2. AP R.NET penuh / tidak merespons");
        Serial.println("  3. DHCP MikroTik sangat lambat");
        Serial.println("  4. MAC address di-blacklist");
        Serial.println("[WiFi:DBG] Restart ESP32...");
        ESP.restart();
      }
    }

    Serial.printf("\n[WiFi:DBG] ====== CONNECTED dalam %lus ======\n",
                  (millis() - start) / 1000);
    Serial.printf("[WiFi:DBG] IP       : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi:DBG] Gateway  : %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("[WiFi:DBG] Subnet   : %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("[WiFi:DBG] DNS      : %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("[WiFi:DBG] RSSI     : %d dBm\n", WiFi.RSSI());
    Serial.printf("[WiFi:DBG] Channel  : %d\n", WiFi.channel());
    Serial.printf("[WiFi:DBG] BSSID    : %s\n", WiFi.BSSIDstr().c_str());
  }

  // =====================================================================
  //  HOTSPOT LOGIN (MikroTik Captive Portal - R.NET)
  // =====================================================================
  /*
    Alur login MikroTik Hotspot:
    1. ESP32 konek ke SSID R.NET (tanpa password / WPA terbuka)
    2. MikroTik memberi IP via DHCP tapi memblokir traffic ke internet
    3. ESP32 kirim HTTP POST ke http://<gateway>/login dengan body:
        username=XXX-XXX&password=XXX-XXX
    4. MikroTik membuka akses internet untuk MAC address ESP32 tersebut

    Fungsi ini dipanggil:
    - Sekali saat boot (setelah connectWiFi)
    - Periodik tiap HOTSPOT_RELOGIN_INTERVAL_MS (default 55 menit)
      untuk jaga-jaga kalau sesi expired sebelum voucher habis
  */
  bool loginHotspot()
  {
    Serial.println("\n[Hotspot:DBG] ====== loginHotspot() START ======");

    // Cek WiFi masih konek
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("[Hotspot:DBG] WiFi TIDAK terhubung! Batalkan login.");
      return false;
    }

    // Gateway diambil otomatis dari DHCP
    String gateway = WiFi.gatewayIP().toString();
    Serial.printf("[Hotspot:DBG] Gateway : %s\n", gateway.c_str());

    if (gateway == "0.0.0.0" || gateway.length() == 0)
    {
      Serial.println("[Hotspot:DBG] Gateway 0.0.0.0 — DHCP belum assign. Tunggu 2 detik...");
      delay(2000);
      gateway = WiFi.gatewayIP().toString();
      Serial.printf("[Hotspot:DBG] Gateway (retry): %s\n", gateway.c_str());
      if (gateway == "0.0.0.0")
      {
        Serial.println("[Hotspot:DBG] Gateway masih 0.0.0.0, abort.");
        return false;
      }
    }

    // Cek konektivitas ke gateway dulu (ping via HTTP GET /)
    Serial.printf("[Hotspot:DBG] Voucher : \"%s\"\n", VOUCHER_CODE);
    String loginUrl = "http://" + gateway + HOTSPOT_LOGIN_PATH;
    Serial.printf("[Hotspot:DBG] Login URL: %s\n", loginUrl.c_str());

    // --- Step 1: GET halaman login ---
    Serial.println("[Hotspot:DBG] STEP 1 — GET login page...");
    String dst = "";
    {
      HTTPClient http;
      http.begin(loginUrl);
      http.addHeader("User-Agent", "Mozilla/5.0 (ESP32)");
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.setTimeout(12000);

      unsigned long t0 = millis();
      int code = http.GET();
      Serial.printf("[Hotspot:DBG] GET selesai dalam %lums, HTTP %d\n",
                    millis() - t0, code);

      if (code > 0)
      {
        String body = http.getString();
        Serial.printf("[Hotspot:DBG] Response len: %d bytes\n", body.length());

        // Cari dst field
        int idx = body.indexOf("name=\"dst\" value=\"");
        if (idx != -1)
        {
          int s = idx + 18;
          dst = body.substring(s, body.indexOf("\"", s));
          Serial.println("[Hotspot:DBG] dst field: \"" + dst + "\"");
        }
        else
        {
          Serial.println("[Hotspot:DBG] dst field TIDAK ditemukan di response.");
        }

        // Tampilkan 500 karakter pertama response untuk diagnosis
        Serial.println("[Hotspot:DBG] --- Cuplikan Response (500 char) ---");
        Serial.println(body.substring(0, 500));
        Serial.println("[Hotspot:DBG] --- End Response ---");
      }
      else
      {
        Serial.printf("[Hotspot:DBG] GET GAGAL: %s (code=%d)\n",
                      http.errorToString(code).c_str(), code);
        Serial.println("[Hotspot:DBG] Kemungkinan: gateway tidak bisa dijangkau / bukan MikroTik");
        http.end();
        return false;
      }
      http.end();
    }

    delay(500);

    // --- Step 2: POST login ---
    Serial.println("\n[Hotspot:DBG] STEP 2 — POST credentials...");
    String postBody = "username=";
    postBody += VOUCHER_CODE;
    postBody += "&password=";
    postBody += VOUCHER_CODE;
    postBody += "&dst=";
    postBody += dst;
    postBody += "&popup=true";

    Serial.printf("[Hotspot:DBG] POST body: %s\n", postBody.c_str());

    HTTPClient http;
    http.begin(loginUrl);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("User-Agent", "Mozilla/5.0 (ESP32)");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(12000);

    unsigned long t0 = millis();
    int postCode = http.POST(postBody);
    Serial.printf("[Hotspot:DBG] POST selesai dalam %lums, HTTP %d\n",
                  millis() - t0, postCode);

    bool success = false;
    if (postCode > 0)
    {
      String resp = http.getString();
      Serial.printf("[Hotspot:DBG] Response len: %d bytes\n", resp.length());
      Serial.println("[Hotspot:DBG] --- Cuplikan Response POST (500 char) ---");
      Serial.println(resp.substring(0, 500));
      Serial.println("[Hotspot:DBG] --- End ---");

      if (postCode == 200 || postCode == 302)
      {
        bool hasInvalid = resp.indexOf("invalid") != -1;
        bool hasError = resp.indexOf("Error") != -1;
        bool hasLogged = resp.indexOf("logged") != -1 || resp.indexOf("You are logged") != -1;

        Serial.printf("[Hotspot:DBG] Flags — invalid:%d  Error:%d  logged:%d\n",
                      hasInvalid, hasError, hasLogged);

        if (!hasInvalid && !hasError)
        {
          Serial.println("[Hotspot:DBG] LOGIN BERHASIL! Internet terbuka.");

          // Verifikasi internet: coba resolve DNS sederhana
          Serial.println("[Hotspot:DBG] Verifikasi internet via HTTP HEAD ke http://clients3.google.com/generate_204 ...");
          HTTPClient testHttp;
          testHttp.begin("http://clients3.google.com/generate_204");
          testHttp.setTimeout(8000);
          int testCode = testHttp.GET();
          testHttp.end();
          Serial.printf("[Hotspot:DBG] Test HTTP: %d (204=internet OK, lainnya=mungkin masih portal)\n", testCode);

          success = true;
        }
        else
        {
          Serial.println("[Hotspot:DBG] LOGIN GAGAL — response mengandung kata 'invalid' atau 'Error'.");
          Serial.println("[Hotspot:DBG] Cek: apakah VOUCHER_CODE sudah benar?");
        }
      }
      else
      {
        Serial.printf("[Hotspot:DBG] HTTP code tidak terduga: %d\n", postCode);
      }
    }
    else
    {
      Serial.printf("[Hotspot:DBG] POST GAGAL: %s\n", http.errorToString(postCode).c_str());
    }
    http.end();

    Serial.printf("[Hotspot:DBG] ====== loginHotspot() END — %s ======\n\n",
                  success ? "SUKSES" : "GAGAL");
    return success;
  }

  // Wrapper: login hotspot + retry hingga berhasil (max 5x)
  void ensureHotspotLogin()
  {
    Serial.println("[Hotspot:DBG] ensureHotspotLogin() dipanggil.");
    const int MAX_RETRY = 5;
    for (int attempt = 1; attempt <= MAX_RETRY; attempt++)
    {
      Serial.printf("[Hotspot:DBG] Percobaan %d/%d...\n", attempt, MAX_RETRY);
      if (loginHotspot())
      {
        lastHotspotLogin = millis();
        Serial.printf("[Hotspot:DBG] Berhasil pada percobaan ke-%d\n", attempt);
        return;
      }
      if (attempt < MAX_RETRY)
      {
        Serial.printf("[Hotspot:DBG] Tunggu 5 detik sebelum retry...\n");
        delay(5000);
      }
    }
    Serial.println("[Hotspot:DBG] SEMUA percobaan GAGAL.");
    Serial.println("[Hotspot:DBG] Periksa: voucher aktif? Gateway benar? Path /login?");
  }

  // =====================================================================
  //  MQTT CALLBACK — terima perintah pompa dari dashboard
  // =====================================================================
  void mqttCallback(char* topic, byte* payload, unsigned int length)
  {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    Serial.printf("[MQTT] Perintah diterima: topic=%s msg=%s\n", topic, msg.c_str());

    if (String(topic) == TOPIC_PUMP_CMD)
    {
      if (msg == "ON")
      {
        pumpState = true;
        digitalWrite(PUMP_PIN, HIGH);
        mqttClient.publish(TOPIC_PUMP_STATUS, "ON", true);
        Serial.println("[PUMP] Manual ON dari dashboard");
      }
      else if (msg == "OFF")
      {
        pumpState = false;
        digitalWrite(PUMP_PIN, LOW);
        mqttClient.publish(TOPIC_PUMP_STATUS, "OFF", true);
        Serial.println("[PUMP] Manual OFF dari dashboard");
      }
      else
      {
        Serial.printf("[PUMP] Perintah tidak dikenal: %s (gunakan ON/OFF)\n", msg.c_str());
      }
    }
  }

  void connectMQTT()
  {
    Serial.println("\n[MQTT:DBG] ====== connectMQTT() START ======");
    Serial.printf("[MQTT:DBG] Host   : %s\n", MQTT_HOST);
    Serial.printf("[MQTT:DBG] Port : %d\n", MQTT_PORT);
    Serial.printf("[MQTT:DBG] Client : %s\n", MQTT_CLIENT_ID);
    Serial.printf("[MQTT:DBG] User   : %s\n", MQTT_USERNAME);

    int attempt = 0;
    while (!mqttClient.connected())
    {
      attempt++;
      Serial.printf("[MQTT:DBG] Percobaan #%d ...\n", attempt);
      unsigned long t0 = millis();

      bool ok = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD,
                                  MQTT_TOPIC_STATUS, 1, true, "offline");
      Serial.printf("[MQTT:DBG] connect() selesai dalam %lums\n", millis() - t0);

      if (ok)
      {
        Serial.println("[MQTT:DBG] CONNECTED!");
        mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);
        mqttClient.subscribe(TOPIC_PUMP_CMD);
        Serial.println("[MQTT:DBG] Subscribe: " TOPIC_PUMP_CMD);
      }
      else
      {
        int rc = mqttClient.state();
        Serial.printf("[MQTT:DBG] GAGAL rc=%d — ", rc);
        switch (rc)
        {
        case -4:
          Serial.println("TIMEOUT (server tidak merespons)");
          break;
        case -3:
          Serial.println("CONNECTION_LOST");
          break;
        case -2:
          Serial.println("CONNECT_FAILED");
          break;
        case -1:
          Serial.println("DISCONNECTED");
          break;
        case 1:
          Serial.println("BAD_PROTOCOL");
          break;
        case 2:
          Serial.println("BAD_CLIENT_ID");
          break;
        case 3:
          Serial.println("UNAVAILABLE");
          break;
        case 4:
          Serial.println("BAD_CREDENTIALS (cek MQTT_USERNAME/PASSWORD)");
          break;
        case 5:
          Serial.println("UNAUTHORIZED");
          break;
        default:
          Serial.println("UNKNOWN");
          break;
        }
        Serial.printf("[MQTT:DBG] WiFi status: %s\n", wifiStatusStr(WiFi.status()));
        Serial.printf("[MQTT:DBG] Retry dalam %lu ms...\n", MQTT_RECONNECT_DELAY);
        delay(MQTT_RECONNECT_DELAY);
      }
    }
  }

  // =====================================================================
  //  OTA
  // =====================================================================
  void setupOTA()
  {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]()
                      {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      Serial.println("[OTA] Mulai update: " + type); });
    ArduinoOTA.onEnd([]()
                    { Serial.println("\n[OTA] Selesai. Restart..."); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          { Serial.printf("[OTA] Progress: %u%%\r", progress * 100 / total); });
    ArduinoOTA.onError([](ota_error_t err)
                      {
      Serial.printf("[OTA] Error[%u]: ", err);
      if      (err == OTA_AUTH_ERROR)    Serial.println("Auth Failed");
      else if (err == OTA_BEGIN_ERROR)   Serial.println("Begin Failed");
      else if (err == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (err == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (err == OTA_END_ERROR)     Serial.println("End Failed"); });

    ArduinoOTA.begin();
    Serial.printf("[OTA] Siap. Hostname: %s\n", OTA_HOSTNAME);
  }

  // =====================================================================
  //  SENSOR - TDS
  // =====================================================================
  float readTdsPpm(float waterTempC)
  {
    int16_t raw = ads.readADC_SingleEnded(TDS_ADS_CHANNEL);
    float voltage = raw * ADS_VOLTS_PER_BIT;
    float compCoeff = 1.0F + 0.02F * (waterTempC - 25.0F);
    float compVolt = voltage / compCoeff;
    float tds = (133.42F * compVolt * compVolt * compVolt - 255.86F * compVolt * compVolt + 857.39F * compVolt) * 0.5F;
    return (tds < 0) ? 0 : tds;
  }

  // =====================================================================
  //  SETUP
  // =====================================================================
  void setupPins()
  {
    pinMode(PUMP_PIN, OUTPUT);
    digitalWrite(PUMP_PIN, LOW); // pompa MATI saat boot
  }

  void setupSensors()
  {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // SHT3x
    sht3xReady = sht3x.begin(0x44);
    if (!sht3xReady)
    {
      Serial.println("[SHT3x] Tidak terdeteksi di 0x44, mencoba 0x45...");
      sht3xReady = sht3x.begin(0x45);
    }
    Serial.printf("[SHT3x]   %s\n", sht3xReady ? "OK" : "GAGAL");

    // DS18B20
    ds18b20.begin();
    ds18b20Ready = (ds18b20.getDeviceCount() > 0);
    Serial.printf("[DS18B20] %s (%d device)\n",
                  ds18b20Ready ? "OK" : "GAGAL", ds18b20.getDeviceCount());

    // ADS1115
    adsReady = ads.begin(0x48);
    if (adsReady)
      ads.setGain(GAIN_TWOTHIRDS);
    Serial.printf("[ADS1115] %s\n", adsReady ? "OK" : "GAGAL");
  }

  void setupInfluxDB()
  {
    timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

    if (influxClient.validateConnection())
      Serial.printf("[InfluxDB] Terhubung ke: %s\n", influxClient.getServerUrl().c_str());
    else
      Serial.printf("[InfluxDB] Gagal: %s\n", influxClient.getLastErrorMessage().c_str());
  }

  // Scan dan cetak semua SSID — pastikan nama SSID target terdeteksi
  void scanWiFi()
  {
    Serial.println("[WiFi] Scanning jaringan...");
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    if (n == 0)
    {
      Serial.println("[WiFi] Tidak ada jaringan ditemukan!");
      return;
    }
    Serial.printf("[WiFi] %d jaringan ditemukan:\n", n);
    bool targetFound = false;
    for (int i = 0; i < n; i++)
    {
      bool isTarget = (WiFi.SSID(i) == WIFI_SSID);
      if (isTarget)
        targetFound = true;
      Serial.printf("  %s [%d] SSID: \"%s\"  RSSI: %d dBm  %s\n",
                    isTarget ? ">>>" : "   ",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "ENCRYPTED");
    }
    Serial.printf("[WiFi] Target kita: \"%s\"\n", WIFI_SSID);
    if (!targetFound)
    {
      Serial.println("[WiFi:DBG] PERINGATAN: SSID target TIDAK DITEMUKAN dalam scan!");
      Serial.println("[WiFi:DBG] Cek: apakah nama SSID sudah benar? (case-sensitive)");
    }
    else
    {
      // Cek kekuatan sinyal target
      for (int i = 0; i < n; i++)
      {
        if (WiFi.SSID(i) == WIFI_SSID)
        {
          int rssi = WiFi.RSSI(i);
          Serial.printf("[WiFi:DBG] RSSI target: %d dBm — ", rssi);
          if (rssi >= -60)
            Serial.println("SANGAT KUAT (OK)");
          else if (rssi >= -70)
            Serial.println("KUAT (OK)");
          else if (rssi >= -80)
            Serial.println("SEDANG (mungkin OK)");
          else if (rssi >= -85)
            Serial.println("LEMAH (risiko timeout)");
          else
            Serial.println("SANGAT LEMAH (kemungkinan besar gagal konek!)");
        }
      }
    }
    WiFi.scanDelete();
  }

  void setup()
  {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ESP32 Multi-Sensor IoT Node v2 ===");

    setupPins();
    setupSensors();
    scanWiFi(); // <-- lihat Serial Monitor: apakah R.NET muncul & ditandai ">>>"?
    connectWiFi();

    // Login ke captive portal R.NET (MikroTik Hotspot) sebelum konek cloud
    ensureHotspotLogin();

    // HiveMQ Cloud - TLS tanpa verifikasi sertifikat (cukup untuk dev/skripsi)
    // Untuk produksi: gunakan wifiClientSecure.setCACert(hivemq_root_ca)
    //wifiClientSecure.setInsecure();
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setBufferSize(768);
    mqttClient.setCallback(mqttCallback);
    connectMQTT();

    setupInfluxDB();
    setupOTA();
  }

  // =====================================================================
  //  LOOP - PUBLISH
  // =====================================================================
  void publishSensorData()
  {
    // --- SHT3x ---
    float tAir = 0, hAir = 0;
    if (sht3xReady)
    {
      tAir = sht3x.readTemperature();
      hAir = sht3x.readHumidity();
      if (!isnan(tAir)) {
        tAir = roundf(tAir * 100) / 100.0;
        mqttClient.publish(TOPIC_AIR_TEMP, String(tAir).c_str(), true);
      }
      if (!isnan(hAir)) {
        hAir = roundf(hAir * 100) / 100.0;
        mqttClient.publish(TOPIC_HUMIDITY, String(hAir).c_str(), true);
      }
    }

    // --- DS18B20 ---
    float waterTemp = 25.0F;
    if (ds18b20Ready)
    {
      ds18b20.requestTemperatures();
      float t = ds18b20.getTempCByIndex(0);
      if (t != DEVICE_DISCONNECTED_C)
      {
        waterTemp = roundf(t * 100) / 100.0;
        mqttClient.publish(TOPIC_WATER_TEMP, String(waterTemp).c_str(), true);
      }
    }

    // --- TDS via ADS1115 ---
    float tds = 0;
    if (adsReady)
    {
      tds = readTdsPpm(waterTemp);
      tds = roundf(tds * 100) / 100.0;
      mqttClient.publish(TOPIC_TDS, String(tds).c_str(), true);
    }

    // --- JSN-SR04T (tidak dipakai) ---
    // Publish dummy values to keep topics active
    mqttClient.publish(TOPIC_TANK1, "0", true);
    mqttClient.publish(TOPIC_TANK2, "0", true);
    
    // Publish pump status
    mqttClient.publish(TOPIC_PUMP_STATUS, pumpState ? "ON" : "OFF", true);

    Serial.println("[MQTT] Data published to individual topics");

    // ---- InfluxDB publish ----
    sensorData.clearFields();
    sensorData.clearTags();
    sensorData.addTag("device", MQTT_CLIENT_ID);
    sensorData.addTag("lokasi", "SmartBajo");

    if (!isnan(tAir) && tAir != 0) sensorData.addField("suhu_udara", tAir);
    if (!isnan(hAir) && hAir != 0) sensorData.addField("kelembaban", hAir);
    if (waterTemp != 25.0F) sensorData.addField("suhu_air", waterTemp);
    if (adsReady) {
      sensorData.addField("tds", tds);
      sensorData.addField("ec", tds * 2.0F); 
    }
    sensorData.addField("pompa_aktif", (int)pumpState);
    sensorData.addField("uptime_s", (long)(millis() / 1000));

    if (influxClient.writePoint(sensorData))
      Serial.println("[InfluxDB] Kirim OK");
    else
      Serial.printf("[InfluxDB] Gagal: %s\n", influxClient.getLastErrorMessage().c_str());
  }

  void loop()
  {
    // OTA harus di-handle setiap iterasi loop
    ArduinoOTA.handle();

    connectWiFi();

    // Re-login hotspot secara periodik agar sesi tidak expired
    if (millis() - lastHotspotLogin >= HOTSPOT_RELOGIN_INTERVAL_MS)
    {
      Serial.println("[Hotspot] Interval re-login tercapai, refresh sesi...");
      ensureHotspotLogin();
    }

    if (!mqttClient.connected())
      connectMQTT();
    mqttClient.loop();

    unsigned long now = millis();
    if (now - lastPublish >= PUBLISH_INTERVAL_MS)
    {
      lastPublish = now;
      publishSensorData();
    }
  }