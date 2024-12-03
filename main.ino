#include <WiFi.h>
#include <HTTPClient.h>

// API endpoint
const char* serverName = "http://192.168.8.193:8081/api/v1/smart_bin/update/1";

// Wi-Fi credentials
const char* ssid = "";     // Replace with your Wi-Fi SSID
const char* password = "";      // Replace with your Wi-Fi password

#define HALL_SENSOR_PIN 34 // GPIO pin for Hall sensor
#define TRIG_PIN 26        // GPIO pin for Ultrasonic Trigger
#define ECHO_PIN 27        // GPIO pin for Ultrasonic Echo
#define THRESHOLD 300      // Threshold for magnet detection

// Bin height (in cm)
const float binHeightMin = 21.0; // Minimum bin height
const float binHeightMax = 23.0; // Maximum bin height

int baseline = 0; // Variable to store the Hall sensor baseline value
WiFiServer server(80); // Web server on port 80
unsigned long previousMillis = 0;
const long interval = 500; // 500ms interval
unsigned long lastApiSendTime = 0; // Timestamp for the last API call
const unsigned long apiInterval = 60000; // 1 minute in milliseconds
bool magnetDetected = false; // Tracks if a magnet is detected


void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);

  // Initialize ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize Hall sensor
  pinMode(HALL_SENSOR_PIN, INPUT);

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // WiFi connected
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Start the web server
  server.begin();

  // Calibrate Hall sensor
  baseline = calibrateBaseline();
  Serial.print("Calibrated Baseline: ");
  Serial.println(baseline);
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    checkWiFiConnection();

    // Check Hall sensor for magnet detection
    int sensorValue = analogRead(HALL_SENSOR_PIN);
    Serial.printf("Hall Sensor Value: %d\n", sensorValue);

    String magnetStatus;
    float fillLevel = -1; // Default fill level if no magnet is detected
    float distance = calculateDistance(); // Get the distance from ultrasonic sensor

    if (sensorValue > baseline + THRESHOLD) {
      magnetDetected = true;
      magnetStatus = "North Pole close";
      fillLevel = calculateFillLevel(distance);
      //if (fillLevel >= 0) sendToApi(fillLevel); // Send valid data to API
    } else if (sensorValue < baseline - THRESHOLD) {
      magnetDetected = true;
      magnetStatus = "South Pole close";
      fillLevel = calculateFillLevel(distance);
      //if (fillLevel >= 0) sendToApi(fillLevel); // Send valid data to API
    } else {
      magnetDetected = false;
      magnetStatus = "No magnet detected";
    }

    // Serve web page
    WiFiClient client = server.available();
    if (client) {
      String request = client.readStringUntil('\r');
      client.flush();

      if (request.indexOf("/data") >= 0) {
        String jsonResponse = "{\"rawValue\": " + String(sensorValue) +
                              ", \"status\": \"" + magnetStatus + "\"" +
                              ", \"fillLevel\": " + String(fillLevel) +
                              ", \"distance\": " + String(distance) + "}";
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println();
        client.println(jsonResponse);
      } else {
        String html = "HTTP/1.1 200 OK\r\n";
        html += "Content-Type: text/html\r\n\r\n";
        html += R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Bin Monitoring</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 20px; }
    h1 { color: #333; }
    .data { margin: 10px 0; font-size: 1.2em; }
    .error { color: red; }
  </style>
</head>
<body>
  <h1>Bin Monitoring Dashboard</h1>
  <div class="data">Hall Sensor Value: <span id="raw">N/A</span></div>
  <div class="data">Magnet Status: <span id="status">N/A</span></div>
  <div class="data">Fill Level: <span id="fillLevel">N/A</span></div>
  <div class="data">Distance: <span id="distance">N/A</span></div>
  <div class="data">Bin Height Range: 21.0 cm - 23.0 cm</div>
  <script>
    async function updateData() {
      try {
        const response = await fetch('/data');
        if (!response.ok) throw new Error('Network response was not ok');
        const data = await response.json();
        document.getElementById('raw').textContent = data.rawValue;
        document.getElementById('status').textContent = data.status;
        document.getElementById('fillLevel').textContent = data.fillLevel >= 0 ? data.fillLevel + '%' : 'N/A';
        document.getElementById('distance').textContent = data.distance >= 0 ? data.distance + ' cm' : 'Error';
      } catch (error) {
        console.error('Error fetching data:', error);
        document.getElementById('raw').textContent = 'Error';
        document.getElementById('status').textContent = 'Error';
        document.getElementById('fillLevel').textContent = 'Error';
        document.getElementById('distance').textContent = 'Error';
      }
    }
    setInterval(updateData, 1000); // Update every second
  </script>
</body>
</html>
)rawliteral";
        client.println(html);
      }
      client.stop();
    }
  }
  if (magnetDetected && (currentMillis - lastApiSendTime >= apiInterval)) {
    lastApiSendTime = currentMillis; // Reset the timer
    float distance = calculateDistance();
    float fillLevel = calculateFillLevel(distance);

    if (fillLevel >= 0) {
      sendToApi(fillLevel); // Send valid data to API
      Serial.println("Magnet detected. Data sent to API.");
    } else {
      Serial.println("Skipping API request: Invalid fill level.");
    }
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting to Wi-Fi...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nReconnected to Wi-Fi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
}

float calculateDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 50000); // 30ms timeout
  if (duration <= 0) {
    Serial.println("Ultrasonic sensor error: No echo detected.");
    return -1;
  }

  float distance = duration * 0.034 / 2;
  if (distance < 0 || distance > binHeightMax) {
    Serial.println("Ultrasonic sensor error: Out-of-range distance.");
    return -1;
  }

  return distance;
}

float calculateFillLevel(float distance) {
  if (distance < 0 || distance > binHeightMax) {
    return -1; // Invalid distance
  }

  // Clamp the distance to the bin height range
  float clampedDistance = constrain(distance, binHeightMin, binHeightMax);

  // Calculate the fill level based on the range
  float fillLevel = ((binHeightMax - distance) / (binHeightMax)) * 100.0;

  // Ensure the fill level is within 0-100%
  return constrain(fillLevel, 0, 100);
}

void sendToApi(float fillLevel) {
  if (fillLevel < 0 || fillLevel > 100) {
    Serial.println("Invalid fill level. Skipping API request.");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected.");
    return;
  }

  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{\"fillLevel\": " + String(fillLevel, 2) + 
                       ", \"longitude\": 79.8612, \"latitude\": 6.9271}";
  Serial.printf("Sending payload: %s\n", jsonPayload.c_str());

  int httpResponseCode = http.PUT(jsonPayload);

  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
  } else {
    Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

int calibrateBaseline() {
  long total = 0;
  const int samples = 100;

  Serial.println("Calibrating Hall sensor...");
  for (int i = 0; i < samples; i++) {
    total += analogRead(HALL_SENSOR_PIN);
    delay(10);
  }
  Serial.println("Calibration complete.");
  return total / samples;
}
