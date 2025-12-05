/*
 * ESP-01 WiFi Relay Controller
 * * This code is an adaptation of the original sketch, specifically modified 
 * for the pin constraints of an ESP-01 module.
 *
 * --- ESP-01 PIN ASSIGNMENTS ---
 * GPIO 0: (Available) - Must be HIGH at boot. Left free to ensure stable boot.
 * GPIO 1: (TX) - Used for Serial.println() debugging.
 * GPIO 2: (LED) - Connected to the onboard blue LED (Active LOW).
 * GPIO 3: (RX) - Re-purposed for the Relay.
 *
 * --- MODIFICATIONS ---
 * 1. RELAY_PORT: Changed from D0 (GPIO 16) to 3 (RX pin).
 * 2. LED_BUILTIN: Changed from D4 to 2 (onboard ESP-01 LED).
 * 3. Serial Input: Removed. The checkRS232() and serialEvent() functions
 * have been deleted because the RX pin (GPIO 3) is now used for the relay.
 * Serial debugging output (TX) will still function.
 */

#include <ESP8266WiFi.h>
#include <HardwareSerial.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>

// --- Configuration Constants ---
#define RELAY_PORT 3   // Using GPIO 3 (RX Pin).
#define LED_BUILTIN 2  // Using GPIO 2 (Onboard blue LED on ESP-01)

// EEPROM Address Definitions (must be unique)
const int SSID_ADDR = 0;          // len = 40
const int PASSWORD_ADDR = 45;     // len = 30
const int IP_ADDR = 80;           // len = 15
const int SUBNET_ADDR = 100;      // len = 15
const int GATEWAY_ADDR = 120;     // len = 15
const int DNS_ADDR = 140;         // len = 15
const int IP_TYPE_ADDR = 160;     // len = 1 ('1' static, '2' DHCP)
const int POWER_ON_ADDR = 165;    // len = 1 ('1' ON, '2' OFF)
const int MDNS_ADDR = 170;        // len = 20
const int EEPROM_SIZE = 256;      // Total EEPROM size

// --- Global Variables ---
WiFiServer server(80);
bool relayStatus = false;
// String serialInputString = ""; // Removed, serial input is disabled
// bool serialStringComplete = false; // Removed, serial input is disabled
int wifiScanCount = 0;
int32_t ssidVolume[50]; // RSSI values

// EEPROM Data Structure
struct Config
{
  char ssid[41];
  char password[31];
  char ip[16];
  char subnet[16];
  char defaultgw[16];
  char dns[16];
  char ipType[2];
  char powerOn[2];
  char mDNS_name[21];
};

Config settings;

// --- Function Prototypes ---
void readValuesFromEeprom();
void saveValuesToEeprom();
String urldecode(String str);
unsigned char h2int(char c);
int scanWiFi();
bool startWiFiClient();
void startWifiAccessPoint();
// void checkRS232(); // Removed
// void serialEvent(); // Removed

// HTTP Handlers
void handleClient(WiFiClient client);
void answerRequest(String httpRequest, WiFiClient client);
void sendStartPage(WiFiClient client);
void sendSettingsPage(WiFiClient client);
void sendSavedPage(WiFiClient client);
void saveSettings(String inputData);
String getParamValue(const String& data, const String& key);
void sendStyle(WiFiClient client);
void sendFavicon(WiFiClient client);

// --- Setup ---
void setup()
{
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nESP-01 Relay Booting...");

  // Pin Initialization
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Turn LED OFF (it's Active LOW)

  pinMode(RELAY_PORT, OUTPUT);
  digitalWrite(RELAY_PORT, HIGH); // Start with relay OFF (HIGH is typically OFF for common relay modules)

  // EEPROM Initialization and Reading
  EEPROM.begin(EEPROM_SIZE);
  readValuesFromEeprom();

  // Set initial relay state based on EEPROM
  if (strcmp(settings.powerOn, "1") == 0)
  {
    digitalWrite(RELAY_PORT, LOW); // ON
    relayStatus = true;
  }

  // WiFi Setup
  Serial.println("\nScanning WiFi networks...");
  wifiScanCount = scanWiFi();

  if (!startWiFiClient())
  {
    startWifiAccessPoint();
  }

  // mDNS Setup
  MDNS.begin(settings.mDNS_name);
  MDNS.addService("http", "tcp", 80);

  // Start Web Server
  server.begin();
  Serial.println("HTTP Server started.");
  Serial.println("--- Setup Complete ---");
}

// --- Loop ---
void loop()
{
  // Check if we need to switch from AP to Client mode
  if (WiFi.getMode() == WIFI_AP && WiFi.status() != WL_CONNECTED)
  {
    startWiFiClient(); // Attempt to reconnect
  }

  MDNS.update();

  // Serial command input has been removed
  // checkRS232(); 

  // Handle incoming WiFi client requests
  WiFiClient client = server.available();
  if (client)
  {
    handleClient(client);
  }
}

// --- EEPROM Functions ---

void readValuesFromEeprom()
{
  EEPROM.get(SSID_ADDR, settings);
  // Check for uninitialized EEPROM
  if (settings.mDNS_name[0] == 0xFF)
  {
    // Default settings if EEPROM is empty/uninitialized
    Serial.println("EEPROM empty, loading defaults.");
    strncpy(settings.ssid, "YourSSID", 40);
    strncpy(settings.password, "YourPassword", 30);
    strncpy(settings.ipType, "2", 1); // DHCP default
    strncpy(settings.powerOn, "2", 1); // Power OFF default
    strncpy(settings.mDNS_name, "myrelaycard", 20);
    
    // Save defaults and re-read
    saveValuesToEeprom();
  }
}

void saveValuesToEeprom()
{
  EEPROM.put(SSID_ADDR, settings);
  EEPROM.commit();
  Serial.println("Settings saved to EEPROM.");
  // Re-read to ensure settings struct is updated
  readValuesFromEeprom(); 
}


// --- Client Handling Functions ---

void handleClient(WiFiClient client)
{
  digitalWrite(LED_BUILTIN, LOW); // Indicate activity (LED ON)
  
  // Wait for request line to be available
  long timeout = millis();
  while (!client.available() && (millis() - timeout < 2000))
  {
    delay(1);
  }

  String currentLine = "";
  String httpRequest = "";

  if (client.available())
  {
    // Read the first line (the HTTP Request line)
    httpRequest = client.readStringUntil('\n');
    httpRequest.trim(); // Remove any leftover carriage return/line feed

    // Read and discard remaining header lines until an empty line is found
    while (client.connected())
    {
      currentLine = client.readStringUntil('\n');
      if (currentLine.length() == 0 || currentLine == "\r")
      {
        break; // End of HTTP header
      }
    }
    
    // Process the request
    answerRequest(httpRequest, client);
  }
  
  // Cleanup connection
  delay(1);
  client.stop();
  digitalWrite(LED_BUILTIN, HIGH); // Turn LED off
}

void answerRequest(String httpRequest, WiFiClient client)
{
  int httpMethod = 0; // 1: Content sent, 5: Settings saved (needs restart)
  
  if (httpRequest.startsWith("GET / "))
  {
    sendStartPage(client);
    httpMethod = 1;
  }
  else if (httpRequest.startsWith("GET /ON "))
  {
    digitalWrite(RELAY_PORT, LOW);
    relayStatus = true;
    sendStartPage(client);
    httpMethod = 1;
  }
  else if (httpRequest.startsWith("GET /OFF "))
  {
    digitalWrite(RELAY_PORT, HIGH);
    relayStatus = false;
    sendStartPage(client);
    httpMethod = 1;
  }
  else if (httpRequest.startsWith("GET /settings "))
  {
    wifiScanCount = scanWiFi();
    sendSettingsPage(client);
    httpMethod = 1;
  }
  else if (httpRequest.startsWith("GET /save?"))
  {
    saveSettings(httpRequest);
    sendSavedPage(client);
    httpMethod = 5;
  }

  if (httpMethod == 5)
  {
    Serial.println("Settings saved. Restarting...");
    delay(1000);
    ESP.restart();
  }
}

// --- Settings and EEPROM Functions ---

// Simple parameter parser for the GET request
String getParamValue(const String& data, const String& key)
{
    // Find key=
    int start = data.indexOf(key + "=");
    if (start == -1) return "";

    // Find the value start
    start += key.length() + 1;
    
    // Find the value end (either & or space)
    int end = data.indexOf('&', start);
    if (end == -1) end = data.indexOf(' ', start);
    if (end == -1) end = data.length();

    String value = data.substring(start, end);
    return urldecode(value);
}

void saveSettings(String inputData)
{
  String val;
  
  // 1. Parse and copy values to settings struct
  val = getParamValue(inputData, "ssid"); strncpy(settings.ssid, val.c_str(), 40); settings.ssid[40] = '\0';
  val = getParamValue(inputData, "password"); strncpy(settings.password, val.c_str(), 30); settings.password[30] = '\0';
  val = getParamValue(inputData, "ipType"); strncpy(settings.ipType, val.c_str(), 1); settings.ipType[1] = '\0';
  val = getParamValue(inputData, "ip"); strncpy(settings.ip, val.c_str(), 15); settings.ip[15] = '\0';
  val = getParamValue(inputData, "subnet"); strncpy(settings.subnet, val.c_str(), 15); settings.subnet[15] = '\0';
  val = getParamValue(inputData, "defaultgw"); strncpy(settings.defaultgw, val.c_str(), 15); settings.defaultgw[15] = '\0';
  val = getParamValue(inputData, "dns"); strncpy(settings.dns, val.c_str(), 15); settings.dns[15] = '\0';
  val = getParamValue(inputData, "mdns"); strncpy(settings.mDNS_name, val.c_str(), 20); settings.mDNS_name[20] = '\0';
  val = getParamValue(inputData, "powerOn"); strncpy(settings.powerOn, val.c_str(), 1); settings.powerOn[1] = '\0';

  // 2. Commit to EEPROM
  saveValuesToEeprom();
}


// --- RS232 (Serial) Functions ---
/*
 * All Serial input functions have been removed as GPIO 3 (RX) 
 * is now used to control the relay.
 */
// void checkRS232() { ... } 
// void serialEvent() { ... }


// --- WiFi Functions ---

int scanWiFi()
{
  int networksFound = WiFi.scanNetworks(false, true); // Don't hide, scan async
  for (int i = 0; i < networksFound && i < 50; i++)
  {
    ssidVolume[i] = WiFi.RSSI(i);
  }
  return networksFound;
}

void startWifiAccessPoint()
{
  Serial.println();
  Serial.println("Startup as AccessPoint with Name: Relay-Modul");

  IPAddress local_IP(192, 168, 4, 1); // Use standard 192.168.4.1 for AP mode
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP("Relay-Modul");

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

bool startWiFiClient()
{
  WiFi.mode(WIFI_STA);
  String ssidStr = String(settings.ssid);
  String passwordStr = String(settings.password);
  String ipTypeStr = String(settings.ipType);

  Serial.print("\nConnecting to ");
  Serial.println(ssidStr);

  if (ipTypeStr.startsWith("1")) // Static IP
  {
    Serial.println("Using Static IP.");
    IPAddress ipAddress;
    ipAddress.fromString(settings.ip);
    IPAddress gatewayAddress;
    gatewayAddress.fromString(settings.defaultgw);
    IPAddress dnsAddress;
    dnsAddress.fromString(settings.dns);
    IPAddress subnetAddress;
    subnetAddress.fromString(settings.subnet);

    WiFi.config(ipAddress, gatewayAddress, dnsAddress, subnetAddress);
  }
  else // DHCP
  {
    Serial.println("Using DHCP.");
    WiFi.config(0U, 0U, 0U); // Clear static config
  }

  WiFi.begin(ssidStr, passwordStr);

  int count = 0;
  while (WiFi.status() != WL_CONNECTED && count < 30) // Wait max 15 seconds (30 * 500ms)
  {
    delay(500);
    Serial.print(".");
    count++;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("\nConnection failed. Switching to AP mode.");
    return false;
  }

  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (strlen(settings.mDNS_name) > 0)
  {
    Serial.print("mDNS address: http://");
    Serial.print(settings.mDNS_name);
    Serial.println(".local");
  }

  return true;
}

// --- Decoding Functions ---

String urldecode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == '+')
    {
      encodedString += ' ';
    }
    else if (c == '%')
    {
      i++;
      code0 = str.charAt(i);
      i++;
      code1 = str.charAt(i);
      c = (h2int(code0) << 4) | h2int(code1);
      encodedString += c;
    }
    else
    {
      encodedString += c;
    }
    yield();
  }
  return encodedString;
}

unsigned char h2int(char c)
{
  if (c >= '0' && c <= '9')
  {
    return (unsigned char)c - '0';
  }
  if (c >= 'a' && c <= 'f')
  {
    return (unsigned char)c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F')
  {
    return (unsigned char)c - 'A' + 10;
  }
  return 0;
}


// --- HTML Sending Functions ---
// NOTE: These functions are kept from the original code.
// They are large and send HTML/CSS/JS directly to the client.

void sendStartPage(WiFiClient client)
{
  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  client.print("<!DOCTYPE HTML><html><head>");
  client.print("<title>Relay Modul</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  sendFavicon(client);
  sendStyle(client);
  client.print("</head><body><div class=\"card\">");
  client.print("<h1>Relay Modul</h1>");
  client.print("Status: <b>");
  if (relayStatus)
  {
    client.print("ON");
  }
  else
  {
    client.print("OFF");
  }
  client.print("</b><br><br>");
  if (relayStatus)
  {
    client.print("<a class=\"button button-off\" href=\"/OFF\">Turn OFF</a>");
  }
  else
  {
    client.print("<a class=\"button button-on\" href=\"/ON\">Turn ON</a>");
  }
  client.print("<br><br><a class=\"button button-set\" href=\"/settings\">Settings</a>");
  client.print("</div></body></html>");
}

void sendSettingsPage(WiFiClient client)
{
  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  client.print("<!DOCTYPE HTML><html><head>");
  client.print("<title>Relay Settings</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  sendFavicon(client);
  sendStyle(client);
  
  // JavaScript for toggling IP fields
  client.print("<script>");
  client.print("function toggleIP() {");
  client.print("var ipType = document.getElementById('ipType').value;");
  client.print("var staticIP = document.getElementById('staticIP');");
  client.print("if(ipType == '1') { staticIP.style.display = 'block'; }");
  client.print("else { staticIP.style.display = 'none'; }");
  client.print("}");
  client.print("</script>");
  
  client.print("</head><body onload=\"toggleIP()\"><div class=\"card\">");
  client.print("<h1>Settings</h1>");
  client.print("<form action=\"/save\" method=\"get\">");
  
  // WiFi SSID
  client.print("<b>WiFi Network (SSID):</b><br>");
  client.print("<select name=\"ssid\">");
  for (int i = 0; i < wifiScanCount; i++)
  {
    client.print("<option value=\"");
    client.print(WiFi.SSID(i));
    client.print("\"");
    if (strcmp(settings.ssid, WiFi.SSID(i).c_str()) == 0)
    {
      client.print(" selected");
    }
    client.print(">");
    client.print(WiFi.SSID(i));
    client.print(" (");
    client.print(ssidVolume[i]);
    client.print("dBm)");
    client.print("</option>");
  }
  client.print("</select><br>");
  client.print("<small>or manually enter:</small><br>");
  client.print("<input type=\"text\" name=\"ssid_manual\" value=\""); // Note: form processing needs to handle ssid_manual if ssid is not the one
  client.print(settings.ssid);
  client.print("\"><br>");
  
  // WiFi Password
  client.print("<b>Password:</b><br>");
  client.print("<input type=\"password\" name=\"password\" value=\"");
  client.print(settings.password);
  client.print("\"><br><br>");
  
  // IP Type (DHCP/Static)
  client.print("<b>IP Type:</b><br>");
  client.print("<select name=\"ipType\" id=\"ipType\" onchange=\"toggleIP()\">");
  client.print("<option value=\"2\"");
  if (strcmp(settings.ipType, "2") == 0) client.print(" selected");
  client.print(">DHCP</option>");
  client.print("<option value=\"1\"");
  if (strcmp(settings.ipType, "1") == 0) client.print(" selected");
  client.print(">Static IP</option>");
  client.print("</select><br>");

  // Static IP Fields (hidden by default)
  client.print("<div id=\"staticIP\" style=\"display:none;\">");
  client.print("<b>IP Address:</b><br><input type=\"text\" name=\"ip\" value=\"");
  client.print(settings.ip);
  client.print("\"><br>");
  client.print("<b>Subnet Mask:</b><br><input type=\"text\" name=\"subnet\" value=\"");
  client.print(settings.subnet);
  client.print("\"><br>");
  client.print("<b>Gateway:</b><br><input type=\"text\" name=\"defaultgw\" value=\"");
  client.print(settings.defaultgw);
  client.print("\"><br>");
  client.print("<b>DNS Server:</b><br><input type=\"text\" name=\"dns\" value=\"");
  client.print(settings.dns);
  client.print("\"><br>");
  client.print("</div><br>");
  
  // mDNS Name
  client.print("<b>mDNS Name (.local):</b><br>");
  client.print("<input type=\"text\" name=\"mdns\" value=\"");
  client.print(settings.mDNS_name);
  client.print("\"><br><br>");

  // Power On State
  client.print("<b>State after Power On:</b><br>");
  client.print("<select name=\"powerOn\">");
  client.print("<option value=\"2\"");
  if (strcmp(settings.powerOn, "2") == 0) client.print(" selected");
  client.print(">OFF</option>");
  client.print("<option value=\"1\"");
  if (strcmp(settings.powerOn, "1") == 0) client.print(" selected");
  client.print(">ON</option>");
  client.print("</select><br><br>");

  // Save Button
  client.print("<input class=\"button button-on\" type=\"submit\" value=\"Save & Restart\">");
  client.print("</form>");
  client.print("<br><a class=\"button button-off\" href=\"/\">Cancel</a>");
  client.print("</div></body></html>");
}

void sendSavedPage(WiFiClient client)
{
  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  client.print("<!DOCTYPE HTML><html><head>");
  client.print("<title>Relay Settings</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  sendFavicon(client);
  sendStyle(client);
  client.print("<meta http-equiv=\"refresh\" content=\"5;url=/\">"); // Redirect after 5s
  client.print("</head><body><div class=\"card\">");
  client.print("<h1>Settings Saved!</h1>");
  client.print("<p>The device will now restart and try to connect to the new network.</p>");
  client.print("<p>You may need to reconnect your device to the new network.</p>");
  client.print("<p>Redirecting back in 5 seconds...</p>");
  client.print("</div></body></html>");
}

void sendStyle(WiFiClient client)
{
  client.print("<style>");
  client.print("html {font-family: Arial, Helvetica, sans-serif; display: inline-block; text-align: center;}");
  client.print("body {margin: 0;}");
  client.print(".card {background: #f4f4f4; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.2); max-width: 400px; margin: 20px auto; padding: 20px;}");
  client.print(".button {border: none; color: white; padding: 16px 32px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; margin: 4px 2px; transition-duration: 0.4s; cursor: pointer; border-radius: 8px; width: 80%; box-sizing: border-box;}");
  client.print(".button-on {background-color: #4CAF50;} .button-on:hover {background-color: #45a049;}");
  client.print(".button-off {background-color: #f44336;} .button-off:hover {background-color: #da190b;}");
  client.print(".button-set {background-color: #555555;} .button-set:hover {background-color: #333;}");
  client.print("input[type=text], input[type=password], select {width: 100%; padding: 12px 20px; margin: 8px 0; display: inline-block; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box;}");
  client.print("input[type=submit] {width: 100%;}");
  client.print("</style>");
}

void sendFavicon(WiFiClient client)
{
  client.print("<link href=\"data:image/x-icon;base64,AAABAAEAMDAAAAEACACoDgAAFgAAACgAAAAwAAAAYAAAAAEACAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAQEABAQEAAUFBQAGBgYABwcHAAgICAAKCgoACwsLAAwMDAANDQ0ADg4OABISEgATExMAFhYWABcXFwAZGRkAHBwcAB0dHgAeHh4AICAgACEhIQAiIiIAIyMjACMjJAAkJCQAJSUlACkpKQAqKioAKysrACwsLAAuLi4ALy8vADAwMAAxMTEAMjIyADQ0NAA2NjYAODg4AD4+PgBGRkYASEhJAElJSQBMTEwAT09PAFBQUABRUVEAVFRUAFZWVgBZWVkAXV1dAGBgYABmZmYAZ2dnAGhoaABpaWkAbGxsAG9vbwBxcXEAdXV1AHp6egB/f38Ag4ODAISEhACJiYkAjo6OAJKSkgCYmJgAmpqaAJubmwCdnZ0AoKCgAKKiogCjo6MApKSkAKenpwCpqakAra2tAK+vrwCwsLAAsrKyALS0tAC8vLwAvb29AMDAwADCwsIAxMTEAMXFxQDHx8YAx8fHAMjIyADJyckAysrKAMvLywDNzc0Azs7OAM/PzwDQ0NAA0tLSANPT0wDU1NQA1tbWANfX1wDY2NgA2dnZANra2gDb29wA3NzcAN7e3gDf398A4ODhAOHh4QDi4uIA4+PjAObm5gDn5+cA6OjoAOvr6wDs7OwA7+/vAPDw8ADx8fEA8vLyAPPz8wD09PQA9vb2APf39wD5+fkA+vr6APv7+wD8/PwA/f39AP7+/gD///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhIOEhISEg4SEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEg4RjKiZPhIOEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhFAGMDUDPISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEe3h7d3t2e3h7cCFOhIRmAmCEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISECQkJCAkICQgKBhJ1g4OEIEaEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEXVxZXVxeWl5YUxVmhIR8B1iEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhD0dSUscNYSEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhINCFBhDgISDhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhIOEhISEg4SEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEg4OEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhIOEhFRVhISDhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEg4RrEQcGHXOEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhHcPPn13NyqEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISDhE0WhISEcxQrLy8vLy8vLy8vhISEhISEhISEhISEhISEhISEhISEhISDhISEhHpzWywQhISEcRMsLy8vLy8vLy8vhISEhISEhISEhISEhISEhISEhISDhISEhGFFNiMXIjgHQH51NDGEhISEhISEhISEhISEhISEhISEhISEhISEhISDhISEf0suCwEeOlZyeoRiDQgEJ3iEhISEhISEhISEhISEhISEhISEhISEhISEhHtzXDsfAAwtSHqEhISEhIOEhFJkhISDhISEhISEhISEhISEhISEhISEhISDhGNGNyQVIDNEX4OEhISDhISEhISDhISEg4SEhISEhISEhISEhISEhISEhISEhIOEUQEgOVRxeYOEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhIJKGg5BhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhD8bSU0lK4OEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEbmltaG5nbmpvWxllhISBCFKEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEBgYGBgYGBgYGBRJ1g4OEIEeEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEdHR0dHR0dHR0bCBPhIRbBWuEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhEwGMDIASoSEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEg4RXKClbhIOEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhIOEhISEg4SEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEhISEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" rel=\"icon\" type=\"image/x-icon\" />");
}
