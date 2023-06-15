/*
  Project: ESP32 based WiFi/MQTT enabled (8) Double Searchlight High Absolute signal Neopixel Node
  (8 signal mast outputs / 16 Neopixel Signal Heads)
  Author: Thomas Seitz (thomas.seitz@tmrci.org)
  Version: 1.0.5
  Date: 2023-06-15
  Description: This sketch is designed for an ESP32 Node with 8 signal mast outputs, using MQTT to subscribe to messages published by JMRI.
  The expected incoming subscribed messages are for JMRI Signal Mast objects, and the expected message payload format is 'Aspect; Lit (or Unlit); Unheld (or Held)'.
  NodeID and IP address displayed on attached 128×64 OLED display.
*/

// Include necessary libraries
#include <Wire.h>              // Library for ESP32 I2C connection  https://github.com/esp8266/Arduino/tree/master/libraries/Wire
#include <Adafruit_GFX.h>      // Library for Adafruit displays     https://github.com/adafruit/Adafruit-GFX-Library
#include <Adafruit_SSD1306.h>  // Library for Monochrome OLEDs      https://github.com/adafruit/Adafruit_SSD1306
#include <WiFi.h>              // Library for WiFi connection       https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi
#include <PubSubClient.h>      // Library for MQTT                  https://github.com/knolleary/pubsubclient
#include <Adafruit_NeoPixel.h> // Library for Adafruit Neopixels    https://github.com/adafruit/Adafruit_NeoPixel
#include <map>                 // Library for std::map              https://en.cppreference.com/w/cpp/container/map
#include <string>              // Library for std::basic_string     https://en.cppreference.com/w/cpp/string/basic_string      

// Network configuration
const char* WIFI_SSID = "(HO) Touchscreens & MQTT Nodes";     // WiFi SSID
const char* WIFI_PASSWORD = "touch.666.pi";                   // WiFi Password

// MQTT configuration
const char* MQTT_SERVER = "129.213.106.87";                   // MQTT server address
const int MQTT_PORT = 1883;                                   // MQTT server port

// Instantiate MQTT client
WiFiClient espClient;                                         // Create a WiFi client
PubSubClient client(espClient);                               // Create an MQTT client using the WiFi client

const int SCREEN_WIDTH = 128; // OLED display width, in pixels
const int SCREEN_HEIGHT = 64; // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
const int OLED_RESET = -1; // Reset pin # (or -1 if sharing ESP32 reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Define the GPIO pins for the Neopixels in ascending order
const int neoPixelPins[7] = {16, 17, 18, 19, 23, 32, 33};

// Define the Neopixel chains and signal masts
Adafruit_NeoPixel signalMasts[8] = {                             // Array of Neopixels, one for each signal mast
    Adafruit_NeoPixel(2, neoPixelPins[0], NEO_GRB + NEO_KHZ800), // SM1 (double head absolute)
    Adafruit_NeoPixel(1, neoPixelPins[1], NEO_GRB + NEO_KHZ800), // SM2 (double head absolute)
    Adafruit_NeoPixel(1, neoPixelPins[2], NEO_GRB + NEO_KHZ800), // SM3 (double head absolute)
    Adafruit_NeoPixel(1, neoPixelPins[3], NEO_GRB + NEO_KHZ800), // SM4 (double head absolute)
    Adafruit_NeoPixel(1, neoPixelPins[4], NEO_GRB + NEO_KHZ800), // SM5 (double head absolute)
    Adafruit_NeoPixel(2, neoPixelPins[5], NEO_GRB + NEO_KHZ800), // SM6 (double head absolute)
    Adafruit_NeoPixel(2, neoPixelPins[5], NEO_GRB + NEO_KHZ800), // SM7 (doubled with SM8) (double head absolute)
    Adafruit_NeoPixel(2, neoPixelPins[6], NEO_GRB + NEO_KHZ800)  // SM8 (second 2 heads) (double head absolute)
};

// Define the NodeID and MQTT topic
String NodeID = "10-MC1";                                     // Node identifier
String mqttTopic = "TMRCI/output/" + NodeID + "/signalmast/"; // Base MQTT topic

// Variables to track NodeID and IP address
String previousNodeID = "";
String previousIPAddress = "";

// Function Prototypes
void callback(char* topic, byte* payload, unsigned int length);
void reconnectMQTT();
void reconnectWiFi();
void updateDisplay();

// Define the signal aspects and lookup tables
const uint32_t RED = signalMasts[0].Color(252, 15, 80);
const uint32_t YELLOW = signalMasts[0].Color(254, 229, 78);
const uint32_t GREEN = signalMasts[0].Color(59, 244, 150);

// Struct to represent signal mast aspect
struct Aspect {
    uint32_t head1;
    uint32_t head2;
};

// Lookup table for double head signal mast aspects
const std::map<std::string, Aspect> doubleSearchlightHighAbsoluteLookup = {
    {"Clear Alt", {GREEN, GREEN}},
    {"Clear", {GREEN, RED}},
    {"Advance Approach Medium", {GREEN, YELLOW}},
    {"Approach Medium", {YELLOW, GREEN}},
    {"Advance Approach", {YELLOW, YELLOW}},
    {"Medium Clear", {RED, GREEN}},
    {"Approach", {YELLOW, RED}},
    {"Medium Approach", {RED, YELLOW}},
    {"Restricting", {RED, YELLOW}},
    {"Stop", {RED, RED}}
};

void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    delay(10);
    Serial.println("Setup started");

    // Initialize WiFi and connect to the network
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
    Serial.println("IP address: " + WiFi.localIP().toString()); // Display IP address

    // Connect to the MQTT broker
    client.setServer(MQTT_SERVER, MQTT_PORT);
    client.setCallback(callback);
    reconnectMQTT();
    Serial.println("Connected to MQTT");

    // Initialize each Neopixel signal mast with a stop signal
    for (int i = 0; i < 8; i++) {
        signalMasts[i].begin();
        signalMasts[i].setBrightness(255);

        // Set all pixels of the signal mast to red color
        signalMasts[i].fill(RED, 0, 2);
        signalMasts[i].show();
    }

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;) {}
    }

    // Initial update of the display
    updateDisplay();
}

void loop() {
    // Reconnect to WiFi if connection lost
    if (WiFi.status() != WL_CONNECTED) {
        reconnectWiFi();
        updateDisplay();
    }

    // Reconnect to MQTT server if connection lost
    if (!client.connected()) {
        reconnectMQTT();
        updateDisplay();
    }

    client.loop();                                            // Run MQTT loop to handle incoming messages
}

void reconnectMQTT() {
    // Attempt to reconnect to MQTT
    while (!client.connected()) {
        Serial.println("Attempting to connect to MQTT...");
        if (client.connect(NodeID.c_str())) {
            client.subscribe((mqttTopic + "+").c_str());      // Subscribe to topics for all signal masts
            Serial.println("Connected to MQTT");
        } else {
            delay(5000);
        }
    }
}

void reconnectWiFi() {
    // Attempt to reconnect to WiFi
    while (WiFi.status() != WL_CONNECTED) {
        Serial.println("Attempting to connect to WiFi...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        delay(5000);
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    // Create a char array for null-termination of the payload
    char message[length + 1];
    for (unsigned int i = 0; i < length; i++) {
        message[i] = (char)payload[i];
    }
    message[length] = '\0';

    // Convert payload bytes into a string
    String payloadStr(message);

    // Extract the signal mast number from the topic
    int mastNumber = topic[strlen(topic) - 1] - '0';

    Serial.print("Received message for SM");
    Serial.print(mastNumber);
    Serial.print(" with payload: ");
    Serial.println(payloadStr);

    // Parse the payload into aspect, lit, and held strings
    int separatorIndex1 = payloadStr.indexOf(';');
    int separatorIndex2 = payloadStr.lastIndexOf(';');

    if (separatorIndex1 == -1 || separatorIndex2 == -1) {
        Serial.println("Error: Invalid payload format.");
        return;
    }

    // Extract and trim the aspect string
    String aspectStr = payloadStr.substring(0, separatorIndex1);
    aspectStr.trim();

    // Extract and trim the lit string
    String litStr = payloadStr.substring(separatorIndex1 + 1, separatorIndex2);
    litStr.trim();

    // Extract and trim the held string
    String heldStr = payloadStr.substring(separatorIndex2 + 1);
    heldStr.trim();

    if (mastNumber < 1 || mastNumber > 8) {
        Serial.println("Error: Invalid mast number.");
        return;
    }
    mastNumber -= 1;

    // Check if the signal mast should be unlit
    if (litStr == "Unlit") {
        // Turn off all pixels of the signal mast
        signalMasts[mastNumber].clear();
        signalMasts[mastNumber].show();
        return;
    }

    // Check if the signal mast should be held
    if (heldStr == "Held") {
        // Set aspect to stop for the corresponding heads of the signal mast
        signalMasts[mastNumber].setPixelColor(0, RED);
        signalMasts[mastNumber].show();
        return;
    }

    // Convert aspectStr from String to std::string
    std::string aspectKey = aspectStr.c_str();

    // Set the colors based on the aspect if it exists in the lookup table
    if (doubleSearchlightHighAbsoluteLookup.count(aspectKey)) {
        // Double head absolute signal mast
        const Aspect& aspect = doubleSearchlightHighAbsoluteLookup.at(aspectKey);
        signalMasts[mastNumber].setPixelColor(0, aspect.head1);
        signalMasts[mastNumber].setPixelColor(1, aspect.head2);
        signalMasts[mastNumber].show();
    }

    // Update display if NodeID or IP address changed
    updateDisplay();
}

void updateDisplay() {
    // Check if NodeID or IP address changed
    static String previousNodeID = "";
    static String previousIPAddress = "";

    if (NodeID != previousNodeID || WiFi.localIP().toString() != previousIPAddress) {
        // Update NodeID and IP address
        previousNodeID = NodeID;
        previousIPAddress = WiFi.localIP().toString();

        // Clear the display
        display.clearDisplay();

        // Display "NodeID" with larger text
        display.setTextSize(2);
        display.setTextColor(WHITE);
        display.setCursor(0, 0);
        display.println("NodeID");
        display.setTextSize(3); // Increase the text size for the NodeID
        display.println(NodeID);

        // Display "IP Address" with smaller text
        display.setTextSize(1);
        display.println("IP Address");
        display.println(WiFi.localIP().toString());

        display.display();
    }
}