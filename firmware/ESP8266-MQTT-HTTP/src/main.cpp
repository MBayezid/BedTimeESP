#include <Arduino.h>
#include <ESP8266WiFi.h>

void setup() {
  Serial.begin(9600);
  delay(1000);
  
  Serial.println("\n\nStarting WiFi scan...");
  
  // Set WiFi to station mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  // Scan for networks
  int networksFound = WiFi.scanNetworks();
  
  Serial.println("Scan complete!");
  Serial.print("Networks found: ");
  Serial.println(networksFound);
  Serial.println();
  
  // Print network details
  for (int i = 0; i < networksFound; i++) {
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (Signal: ");
    Serial.print(WiFi.RSSI(i));
    Serial.print(" dBm)\n");
    // Serial.println((WiFi.encryptionType(i) == WiWIFI_AUTH_OPEN) ? "Open" : "Secured");
  }
  
  Serial.println();
}

void loop() {
  delay(5000);
}