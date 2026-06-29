#include <WiFi.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

// ============== WiFi ==============
// Silakan disesuaikan dengan WiFi di lokasi pengujian
#define WIFI_SSID "R.NET"
#define WIFI_PASSWORD "30RSmartBajo"

// ============== InfluxDB Cloud Serverless ==============
// URL Server InfluxDB (Otomatis dari pembuatan akun)
#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"

// Token / API Key yang baru saja dibuat
#define INFLUXDB_TOKEN "LoBd_3ful6J9nCpMHLeiVzXDCVyQO1cO28545-h0Gl695Ndls19mmz3ANfJZXXTvwzvAkhTliNCX_OWRHabD0Q=="

// Nama Organisasi (Biasanya email akun atau nama yang diketik saat daftar)
#define INFLUXDB_ORG "miqdadwajo@gmail.com" 

// Nama Bucket (Wadah data) yang dibuat tadi
#define INFLUXDB_BUCKET "smartbajo"

// Zona Waktu (Penting untuk validasi sertifikat keamanan server)
#define TZ_INFO "WITA-8" // Waktu Indonesia Tengah (Bajo/Wajo)

// Inisialisasi Klien InfluxDB dengan sertifikat Root bawaan
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Membuat objek Point "monitoring" (mirip seperti nama tabel di SQL)
Point sensorData("monitoring");

// Timer untuk interval pengiriman data
unsigned long lastMsg = 0;
const long interval = 5000; // Kirim data setiap 5 detik (Ubah jika terlalu cepat)

void setupWiFi() {
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  
  // 1. Hubungkan WiFi
  setupWiFi();

  // 2. Sinkronisasi waktu dari server NTP (Wajib agar sertifikat SSL/TLS valid)
  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  // 3. Validasi koneksi InfluxDB
  if (client.validateConnection()) {
    Serial.print("Berhasil terhubung ke Gudang InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("Gagal terhubung ke InfluxDB: ");
    Serial.println(client.getLastErrorMessage());
  }
}

void loop() {
  // Pengiriman data non-blocking setiap X detik
  unsigned long now = millis();
  if (now - lastMsg > interval) {
    lastMsg = now;

    // Simulasi pembacaan sensor fisik (Kahar: Ganti dengan variabel sensor asli DS18B20 & Probe EC)
    float suhuAir = random(2900, 3200) / 100.0;     // 29.00 - 32.00 °C
    float ec = random(20000, 30000) / 100.0;        // 200.00 - 300.00 µS/cm
    float tds = ec * 0.5;                           // Konversi EC ke PPM
    float suhuUdara = random(2800, 3200) / 100.0;   // 28.00 - 32.00 °C
    float kelembaban = random(6000, 8500) / 100.0;  // 60.00 - 85.00 %RH

    // Menghapus data/nilai lama dari Point (Wajib sebelum mengisi baru)
    sensorData.clearFields();
    
    // Memasukkan Nilai Sensor ke Database (Field)
    sensorData.addField("suhu_air", suhuAir);
    sensorData.addField("ec", ec);
    sensorData.addField("tds", tds);
    sensorData.addField("suhu_udara", suhuUdara);
    sensorData.addField("kelembaban", kelembaban);
    
    // Menambahkan Tag (Label) untuk memudahkan filter data di Web
    sensorData.clearTags();
    sensorData.addTag("device", "ESP32-Kahar");
    sensorData.addTag("lokasi", "Bajo");

    // Mulai Eksekusi Mengirim ke InfluxDB
    Serial.println("\n[HTTP] Mengirim data ke InfluxDB Cloud...");
    
    if (client.writePoint(sensorData)) {
      // Jika berhasil tembus ke Cloud
      Serial.println("Pengiriman Berhasil! ✅");
      
      // Tampilkan hasil di Serial Monitor agar Kahar bisa memantau
      Serial.println("--------------------------------------------------");
      Serial.print("[DS18B20] Suhu Air   : "); Serial.print(suhuAir); Serial.println(" °C");
      Serial.print("[TDS]     EC         : "); Serial.print(ec); Serial.println(" µS/cm");
      Serial.print("[TDS]     TDS        : "); Serial.print(tds); Serial.println(" ppm");
      Serial.print("[SHT31]   Suhu Udara : "); Serial.print(suhuUdara); Serial.println(" °C");
      Serial.print("[SHT31]   Kelembaban : "); Serial.print(kelembaban); Serial.println(" %RH");
      Serial.println("--------------------------------------------------");
    } else {
      // Jika gagal, tampilkan pesan error dari InfluxDB
      Serial.print("Pengiriman Gagal! ❌ -> ");
      Serial.println(client.getLastErrorMessage());
    }
  }
}
