#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

const char* ssid = "OPPO F11 PRO";
const char* password = "1234567890";
const char* mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1883;
const char* mqtt_topic_prediction = "irigasi/prediction";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000);

const int moistureSensorPin = A0;
const int relayPin = D1;
const int DHTPin = D2;

#define DHTTYPE DHT22
DHT dht(DHTPin, DHTTYPE);

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastReconnectAttempt = 0;
unsigned long lastDataSendTime = 0;

void setup() {
  Serial.begin(115200);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);  // Memastikan relay mati pada awal

  dht.begin();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  timeClient.begin();
}

void setup_wifi() {
  delay(10);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void controlIrrigation(String predictionToday, String predictionTomorrow) {
  timeClient.update();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  Serial.print("Current Hour: ");
  Serial.println(currentHour);
  Serial.print("Current Minute: ");
  Serial.println(currentMinute);

  // Jadwal irigasi pada jam 08:15 dan 15:15
    if ((currentHour == 8 && currentMinute == 15) || (currentHour == 15 && currentMinute == 15)) {
      if (predictionToday == "Clear") {
          activatePump(6000);  // Pompa aktif 6 detik
      } else if (predictionToday == "Partially cloudy") {
          activatePump(6000);  // Pompa aktif 6 detik
      } else if (predictionToday == "Rain, Partially cloudy") {
          activatePump(4000);  // Pompa aktif 4 detik
      } else if (predictionToday == "Rain, Overcast") {
          activatePump(2000); // Pompa aktif 2 detik
      } else {
          Serial.println("Tidak perlu irigasi (Curah hujan mencukupi)");
      }
  }
}
void activatePump(int duration) {
  digitalWrite(relayPin, LOW);  // Relay ON (pompa aktif)
  Serial.println("Irigasi Aktif (Pompa ON)");
  delay(duration);
  digitalWrite(relayPin, HIGH); // Relay OFF (pompa nonaktif)
  Serial.println("Irigasi Nonaktif (Pompa OFF)");
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(message);

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  String predictionToday = doc["prediction_today"] | "";
  String predictionTomorrow = doc["prediction_tomorrow"] | "";

  if (predictionToday.isEmpty() || predictionTomorrow.isEmpty()) {
    Serial.println("Data JSON tidak lengkap atau salah.");
    return;
  }

  Serial.println("Prediksi Cuaca Hari Ini: " + predictionToday);
  Serial.println("Prediksi Cuaca Besok: " + predictionTomorrow);

  controlIrrigation(predictionToday, predictionTomorrow);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("WemosClient")) {
      Serial.println("connected");
      client.subscribe(mqtt_topic_prediction);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    unsigned long currentReconnectAttempt = millis();
    if (currentReconnectAttempt - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = currentReconnectAttempt;
      reconnect();
    }
  } else {
    client.loop();
  }

  timeClient.update();

  unsigned long currentTime = millis();
  if (currentTime - lastDataSendTime > 60000) {  // Pengiriman data sensor setiap 60 detik
    lastDataSendTime = currentTime;

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int soilMoisture = analogRead(moistureSensorPin);

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Gagal membaca suhu atau kelembapan");
    } else {
      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.println(" °C");

      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");

      String sensorData = "{\"soil_moisture\": " + String(soilMoisture) +
                          ", \"temperature\": " + String(temperature) +
                          ", \"humidity\": " + String(humidity) + "}";
      Serial.print("Mengirim data sensor: ");
      Serial.println(sensorData);
      client.publish("irigasi/sensor_data", sensorData.c_str());
    }
  }

  // Pastikan WiFi tetap terhubung
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }
}
