#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// WiFi credentials
const char* ssid = "";       // Replace with your WiFi SSID
const char* password = "";     // Replace with your WiFi Password

// API endpoint
const char* serverName = "http://192.168.8.101:8081/api/v1/smart_bin/key/{smartbinapikey}";

// Ultrasonic sensor pins
#define TRIG_PIN 16  // TRIG pin (GPIO16)
#define ECHO_PIN 4   // ECHO pin (GPIO4)

// Bin height (in cm)
const float binHeight = 50.0;

void setup() {
  // Initialize Serial Monitor
  Serial.begin(74880);
  delay(100);

  // Initialize ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // WiFi connected
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Measure fill level
  float fillLevel = calculateFillLevel();

  // Log the fill level
  Serial.print("Fill Level: ");
  Serial.print(fillLevel);
  Serial.println("%");

  // Send the fill level via PUT request
  sendPutRequest(fillLevel);

  // Wait before the next measurement
  delay(10000); // 10-second interval
}

float calculateFillLevel() {
  // Trigger the ultrasonic sensor
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Measure the duration of the HIGH pulse on ECHO pin
  long duration = pulseIn(ECHO_PIN, HIGH);

  // Calculate the distance (in cm)
  float distance = duration * 0.034 / 2;

  // Calculate the fill level as a percentage
  float fillLevel = ((binHeight - distance) / binHeight) * 100.0;

  // Ensure the fill level is within 0-100%
  if (fillLevel < 0) fillLevel = 0;
  if (fillLevel > 100) fillLevel = 100;

  return fillLevel;
}

void sendPutRequest(float fillLevel) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;            // Create a WiFi client
    HTTPClient http;              // Create an HTTP client

    // Specify the URL
    http.begin(client, serverName);
    http.addHeader("Content-Type", "application/json"); // Specify content type

    // JSON payload with calculated fill level
    String jsonPayload = "{\"fillLevel\": " + String(fillLevel, 2) + 
                         ", \"longitude\": 79.8612, \"latitude\": 6.9271}";

    Serial.print("Sending payload: ");
    Serial.println(jsonPayload);

    // Send PUT request
    int httpResponseCode = http.sendRequest("PUT", jsonPayload);

    // Print response
    if (httpResponseCode > 0) {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      String response = http.getString();
      Serial.println("Response:");
      Serial.println(response);
    } else {
      Serial.print("Error in sending PUT request: ");
      Serial.println(http.errorToString(httpResponseCode));
    }

    // End the HTTP connection
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}
