#include <WiFi.h>
#include <DHT.h>
#include <FirebaseESP32.h>
#include <time.h>

// WiFi
#define WIFI_SSID "Redmi Note 9"
#define WIFI_PASSWORD "22222222"

// Firebase
#define API_KEY "AIzaSyDXkr12B-R_LqH3sVQ1LsdyabwJVExUfnY"
#define DATABASE_URL "https://airquality-e6800-default-rtdb.asia-southeast1.firebasedatabase.app/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// DHT22
#define DHTPIN 23
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Sensor Pins
#define MQ7_PIN 34
#define MQ135_PIN 35
#define BUZZER_PIN 5

// ZH03B UART2
#define RXD2 16
#define TXD2 17

const float batas_kelembapan = 95.0;
#define NUM_SAMPLES 10
float suhu_samples[NUM_SAMPLES];
float hum_samples[NUM_SAMPLES];
int mq7_samples[NUM_SAMPLES];
int mq135_samples[NUM_SAMPLES];
int sample_index = 0;

// Data PM dari ZH03B
uint16_t pm1_0_std = 0;
uint16_t pm2_5_std = 0;
uint16_t pm10_std  = 0;

float average(float arr[], int len) {
  float sum = 0;
  for (int i = 0; i < len; i++) sum += arr[i];
  return sum / len;
}

int averageInt(int arr[], int len) {
  long sum = 0;
  for (int i = 0; i < len; i++) sum += arr[i];
  return sum / len;
}

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nTerhubung ke WiFi!");
}

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm *p_tm = localtime(&now);
  char buffer[30];
  sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
          p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday,
          p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec);
  return String(buffer);
}

String cleanTimestamp() {
  time_t now = time(nullptr);
  struct tm *p_tm = localtime(&now);
  char buffer[30];
  sprintf(buffer, "%04d_%02d_%02d_%02d_%02d_%02d",
          p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday,
          p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec);
  return String(buffer);
}

// Fungsi baca sensor ZH03B
void bacaZH03B() {
  while (Serial2.available() >= 32) {
    if (Serial2.peek() == 0x42) {
      uint8_t buffer[32];
      Serial2.readBytes(buffer, 32);
      if (buffer[0] == 0x42 && buffer[1] == 0x4D) {
        pm1_0_std = (buffer[4] << 8) | buffer[5];
        pm2_5_std = (buffer[6] << 8) | buffer[7];
        pm10_std  = (buffer[8] << 8) | buffer[9];
      }
    } else {
      Serial2.read(); // drop
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(1000);
  Serial.println("ZH03B sensor starting...");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  connectToWiFi();

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = "garuh123@gmail.com";
  auth.user.password = "garuh123@gmail.com";
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Menunggu autentikasi Firebase...");
  while (!Firebase.ready()) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nTerhubung ke Firebase!");
}

void loop() {
  // Baca sensor ZH03B dulu
  bacaZH03B();

  float suhu = dht.readTemperature();
  float kelembapan = dht.readHumidity();
  int mq7_adc = analogRead(MQ7_PIN);
  int mq135_adc = analogRead(MQ135_PIN);

  if (isnan(suhu) || isnan(kelembapan)) {
    Serial.println("Gagal membaca dari sensor DHT!");
    delay(2000);
    return;
  }

  suhu_samples[sample_index] = suhu;
  hum_samples[sample_index] = kelembapan;
  mq7_samples[sample_index] = mq7_adc;
  mq135_samples[sample_index] = mq135_adc;
  sample_index = (sample_index + 1) % NUM_SAMPLES;

  float suhu_avg = average(suhu_samples, NUM_SAMPLES);
  float hum_avg = average(hum_samples, NUM_SAMPLES);
  int mq7_avg = averageInt(mq7_samples, NUM_SAMPLES);
  int mq135_avg = averageInt(mq135_samples, NUM_SAMPLES);

  float co_ppm = map(mq7_avg, 0, 4095, 0, 600);
  float co2_ppm = map(mq135_avg, 0, 4095, 400, 2000);

  float suhu_bulat = roundf(suhu_avg * 10) / 10.0;
  float hum_bulat = roundf(hum_avg * 10) / 10.0;
  float co_bulat = roundf(co_ppm * 10) / 10.0;
  float co2_bulat = roundf(co2_ppm * 10) / 10.0;

  Serial.printf("PM1.0: %d, PM2.5: %d, PM10: %d \nSuhu: %.1f C, H: %.1f %%, CO: %.1f ppm, CO2: %.1f ppm\n",
                pm1_0_std, pm2_5_std, pm10_std,
                suhu_bulat, hum_bulat, co_bulat, co2_bulat);

  digitalWrite(BUZZER_PIN, hum_bulat > batas_kelembapan ? HIGH : LOW);

  String timestampKey = cleanTimestamp();
  String path = "/air_quality/" + timestampKey;

  Firebase.setFloat(fbdo, path + "/temperature", suhu_bulat);
  Firebase.setFloat(fbdo, path + "/humidity", hum_bulat);
  Firebase.setFloat(fbdo, path + "/co_ppm", co_bulat);
  Firebase.setFloat(fbdo, path + "/co2_ppm", co2_bulat);
  Firebase.setInt(fbdo, path + "/pm1_0", pm1_0_std);
  Firebase.setInt(fbdo, path + "/pm2_5", pm2_5_std);
  Firebase.setInt(fbdo, path + "/pm10", pm10_std);
  Firebase.setString(fbdo, path + "/timestamp", getTimestamp());

  delay(5000);
}
