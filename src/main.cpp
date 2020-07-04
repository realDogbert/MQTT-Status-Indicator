// make sure to upload configuration with 'pio run --target uploadfs'

#include <Arduino.h>
#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include <Adafruit_NeoPixel.h>


#define FALLBACK_SSID "your SSID here"
#define FALLBACK_PASSWORD "your password here"
#define FALLBACK_MQTT_SERVER "IP of your mqtt server here"

#define PROJECT "Status Indicator"
#define VERSION "1.1.1"
#define INTERVAL_MEASUREMENT_MS 60UL * 1000UL

#define MQTT_TOPIC_CMND_GROUP "cmnd/nodemcu"

#define MQTT_TOPIC_TASMOTE_POWER "stat/tasmota_AF39DC/POWER"

#define MQTT_TOPIC_PREFIX_CMD "cmnd/"
#define MQTT_TOPIC_PREFIX_TELE "tele/"
#define MQTT_TOPIC_PREFIX_STAT "stat/"

#define TELE_STATE "/STATE"
#define TELE_LWT "/LWT"

#define PREFIX_CLIENT_NAME "nodemcu"
#define LWT_CONNECTED "Online"
#define LWT_DISCONNECTED "Disconnected"

#define LED_PIN D2 
#define LED_COUNT 18

#define STATE_START 0
#define STATE_DEFAULT 1
#define STATE_COLOR 2
#define STATE_BLINK 3

// Struct for Configuration
struct Config {
  char ssid[64];
  char password[64];
  char mqttServer[64];
};
Config config;

WiFiClient espClient;
PubSubClient client(espClient);

String mqttClientName;
String ipAddress;

String topic_tele;
String topic_lwt;
String topic_stat;
String topic_cmnd;
String topic_stat_mode;

unsigned long lastMsg = 0;

String states[] = {"START", "DEFAULT", "COLOR", "BLINK"};
short state = STATE_START;
short prevState = STATE_START;
unsigned long stateStart;
short brightness = 50;


Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);



void ledSetAllPixels(uint32_t color) {

  for(int i=0; i<strip.numPixels(); i++) { // For each pixel in strip...
    strip.setPixelColor(i, color);         //  Set pixel's color (in RAM)
    strip.show();                          //  Update strip to match
  }

}


void ledRotate() {

  for (int rounds = 0; rounds < LED_COUNT; rounds++) {
    for (int i = 0; i < 8; i++)
    {
      strip.setBrightness(50 + (10 * rounds));
      strip.clear();
      for (int leds = 0; leds <= rounds; leds++) {
        strip.setPixelColor(leds + i, 80 - (rounds * 10), 50 + (rounds * 20) , 0);
      }
      strip.show();

      delay(75);
    }
  }
  

  strip.setBrightness(50);
  strip.clear();
  strip.show();
  
}

void ledBlink() {

  unsigned long now = millis();
  if (now < stateStart + 2000) {
    brightness = 255;
    strip.setBrightness(brightness);
    strip.show();
    state = STATE_BLINK;
  } else {
    brightness = 50;
    strip.clear();
    strip.setBrightness(brightness);
    state = STATE_DEFAULT;
  }
}



void readConfiguration() {
  File file = SPIFFS.open("/config.json", "r");
  
  // Parse the root object
  StaticJsonBuffer<512> jsonBuffer;
  JsonObject &root = jsonBuffer.parseObject(file);

  if (!root.success()) {
    Serial.println(F("Failed to read file, using default configuration"));
  } else {

    strlcpy(config.ssid, root["SSID"] | FALLBACK_SSID, sizeof(config.ssid));
    strlcpy(config.password, root["password"] | FALLBACK_PASSWORD, sizeof(config.password));
    strlcpy(config.mqttServer, root["mqttServer"] | FALLBACK_MQTT_SERVER, sizeof(config.mqttServer));

  }

  // Close the file (File's destructor doesn't close the file)
  file.close();

}

void createTopics() {

  char buffer [10];
  byte mac[6];
  WiFi.macAddress(mac);
  sprintf(buffer, "%s_%02X%02X%02X", PREFIX_CLIENT_NAME, mac[3], mac[4], mac[5]);

  mqttClientName = buffer;

  topic_tele = (MQTT_TOPIC_PREFIX_TELE + mqttClientName + "/STATE").c_str();
  topic_lwt = (MQTT_TOPIC_PREFIX_TELE + mqttClientName + "/LWT").c_str();
  topic_stat = (MQTT_TOPIC_PREFIX_STAT + mqttClientName + "/RESULT").c_str();
  topic_cmnd = (MQTT_TOPIC_PREFIX_CMD + mqttClientName).c_str();
  topic_stat_mode = (MQTT_TOPIC_PREFIX_STAT + mqttClientName + "/MODE").c_str();

}

void setupWifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(config.ssid);

  //Serial.printf("Password: %s\n", config.password);

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  IPAddress ip = WiFi.localIP();
  ipAddress = String(ip[0]);
  for (byte octet = 1; octet < 4; ++octet) {
   ipAddress += '.' + String(ip[octet]);
  }

  Serial.printf("\nConnected to Network %s with ip %s.\n\n", config.ssid, ipAddress.c_str());

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {

    client.disconnect();

    // Create a random client ID
    String clientId = mqttClientName;
    client.setKeepAlive(120);
    Serial.printf("Attempting MQTT connection to server %s as %s ... ", config.mqttServer, clientId.c_str());
    
    // Attempt to connect
    if (client.connect(clientId.c_str(), topic_lwt.c_str(), 1, true, LWT_DISCONNECTED)) {
      Serial.println(" connected.");

      // Once connected, publish an announcement...
      client.publish(topic_lwt.c_str(), LWT_CONNECTED, true);
      // ... and resubscribe
      client.subscribe(MQTT_TOPIC_CMND_GROUP);
      client.subscribe(topic_cmnd.c_str());

      // subscribe to tasmota topic
      client.subscribe(MQTT_TOPIC_TASMOTE_POWER);

      Serial.println("\nFinished setup. Ready to have fun!");

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


String getBaseData() {

  String json = "{";
  json += "\"software_project\": \"" + String(PROJECT) + "\", ";
  json += "\"software_version\": \"" + String(VERSION) + "\", ";
  json += "\"wifi_ip_address\": \"" + ipAddress + "\", ";
  json += "\"mqtt_client_name\": \"" + mqttClientName + "\"";
  json += "}";

  return json;

}

/*
String getBaseData() {

  DynamicJsonBuffer  jsonBuffer(200);
  JsonObject& root = jsonBuffer.createObject();
  jsonBuffer.clear();

  root["software_project"] = PROJECT;
  root["software_version"] = VERSION;
  root["wifi_ip_address"] = ipAddress;
  root["mqtt_client_name"] = mqttClientName;

  String result;
  root.printTo(result);
  return result;

}
*/


String getStatus() {

  String json = "{";
  //json += "\"item_state\": \"" + states[state] + "\", ";
  json += "\"uptime\": " + String(millis()) + ", ";
  json += "\"wifi_signal_rssi\": " + String(WiFi.RSSI());
  json += "}";

  return json;

}

void callback(char* topic, byte* payload, unsigned int length) {

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.println("] ");

  if (strcmp(MQTT_TOPIC_TASMOTE_POWER, topic) == 0) {

    Serial.println(F("Change TV Light."));
    if ((char)payload[1] == 'N') {
      ledSetAllPixels(strip.Color(0,255,0));
    } else {
      ledSetAllPixels(strip.Color(255,0,0));
    }
    state = STATE_BLINK;
    stateStart = millis();
      
    return;

  }

  // Allocate the memory pool on the stack.
  // Don't forget to change the capacity to match your JSON document.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonBuffer<512> jsonBuffer;

  // Parse the message
  JsonObject &message = jsonBuffer.parseObject(payload);
  if (!message.success()) {
    Serial.println(F("Error parsing the JSON."));
    
    ledSetAllPixels(strip.Color(255, 0, 0));
    state = STATE_COLOR;
    stateStart = millis();

  } else {
    char cmnd[32];
    int repeat = message["repeat"] | 3;
    strlcpy(cmnd, message["cmnd"], sizeof(cmnd));
    Serial.printf("Received command: %s, %i-times\n", cmnd, repeat);
    
    if (strcmp("blink", cmnd) == 0) {

      state = STATE_BLINK;
      stateStart = millis();

      int r = message["red"] | 255;
      int g = message["green"] | 255;
      int b = message["blue"] | 255;
      ledSetAllPixels(strip.Color(r, g, b));

    }

    if (strcmp("color", cmnd) == 0) {

      state = STATE_COLOR;
      stateStart = millis();

      int r = message["red"] | 255;
      int g = message["green"] | 255;
      int b = message["blue"] | 255;
      ledSetAllPixels(strip.Color(r, g, b));

    }

    if (strcmp("clear", cmnd) == 0) {

      state = STATE_DEFAULT;
      stateStart = millis();

      strip.setBrightness(50);
      strip.clear();
      strip.show();

    }

  }
  jsonBuffer.clear();

  // Publish base data
  client.publish(topic_stat.c_str(), getBaseData().c_str());

}





void setup() {

  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(50); // Set BRIGHTNESS to about 1/5 (max = 255)
  ledSetAllPixels(strip.Color(255,0,0));

  Serial.begin(115200);
  Serial.println();

  Serial.printf("Project: %s, Version: %s\n\n", PROJECT, VERSION);

  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  } else {
    Serial.println("SPIFFS mounted.");
    readConfiguration();
  }

  setupWifi();
  createTopics();

  client.setServer(config.mqttServer, 1883);
  client.setCallback(callback);

  // announce yourself to the broker
  client.publish(topic_tele.c_str(), getStatus().c_str());

  // give visual feedback
  ledSetAllPixels(strip.Color(0,255,0));
  state = STATE_BLINK;
  stateStart = millis();

}

void loop() {

  if (!client.connected()) {
    ledSetAllPixels(strip.Color(255, 165, 0));
    reconnect();
    strip.clear();
    strip.show();
  }
  client.loop();

  if (WiFi.status() != WL_CONNECTED) {
    state = STATE_COLOR;
    stateStart = millis();
    ledSetAllPixels(strip.Color(137, 104, 205));
  }

  if (!client.connected()) {

  }
    
  unsigned long now = millis();
  if (now - lastMsg > INTERVAL_MEASUREMENT_MS) {
    lastMsg = now;
    client.publish(topic_tele.c_str(), getStatus().c_str(), true);
  }

  switch (state) {
    case STATE_DEFAULT:
      strip.clear();
      strip.show();
      break;
    case STATE_COLOR:
      break;
    case STATE_BLINK:
      ledBlink();
      break;
  } 

  if (prevState != state) {
    prevState = state;
    client.publish(topic_stat_mode.c_str(), states[state].c_str(), true);
  } 

}