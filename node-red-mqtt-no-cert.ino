#include <FS.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <math.h>

// --------------------------------------------------------------------------------------------
//        UPDATE CONFIGURATION TO MATCH YOUR ENVIRONMENT
// --------------------------------------------------------------------------------------------

// Watson IoT connection details
#define MQTT_HOST "<org id>.messaging.internetofthings.ibmcloud.com"
#define MQTT_PORT 1883
#define MQTT_DEVICEID "d:<org id>:<device type>:<device id>"
#define MQTT_USER "use-token-auth"
#define MQTT_TOKEN "<device token>"
#define MQTT_TOPIC "iot-2/evt/status/fmt/json"
#define MQTT_TOPIC_DISPLAY "iot-2/cmd/display/fmt/json"
#define MQTT_TOPIC_INTERVAL "iot-2/cmd/interval/fmt/json"

// Add GPIO pins used to connect devices
#define RGB_PIN 4 // GPIO pin the data line of RGB LED is connected to
#define DHT_PIN 5 // GPIO pin the data line of the DHT sensor is connected to

// Specify DHT11 (Blue) or DHT22 (White) sensor
#define DHTTYPE DHT22
//#define DHTTYPE DHT11
#define NEOPIXEL_TYPE NEO_RGB + NEO_KHZ800

// Temperatures to set LED by (assume temp in C)
#define ALARM_COLD 0.0
#define ALARM_HOT 30.0
#define WARN_COLD 10.0
#define WARN_HOT 25.0

// Add WiFi connection information
char ssid[] = "CACGuest2";  // your network SSID (name)
char pass[] = "quickiris517";  // your network password

// Model parameters from part4 - to implement the model on the ESP8266
// Replace these parameters with the model parameters from your Jupyter Notebook
#define MODEL_INTERCEPT -14.172253183961212
#define MODEL_TEMP_COEF -3.0625
#define MODEL_HUM_COEF 1.3247

// --------------------------------------------------------------------------------------------
//        SHOULD NOT NEED TO CHANGE ANYTHING BELOW THIS LINE
// --------------------------------------------------------------------------------------------

Adafruit_NeoPixel pixel = Adafruit_NeoPixel(8, RGB_PIN, NEOPIXEL_TYPE);
DHT dht(DHT_PIN, DHTTYPE);

// MQTT objects
void callback(char* topic, byte* payload, unsigned int length);
WiFiClient wifiClient;
PubSubClient mqtt(MQTT_HOST, MQTT_PORT, callback, wifiClient);

// variables to hold data
StaticJsonDocument<100> jsonDoc;
JsonObject payload = jsonDoc.to<JsonObject>();
JsonObject status = payload.createNestedObject("d");
StaticJsonDocument<100> jsonReceiveDoc;
static char msg[50];

float h = 0.0; // humidity
float t = 0.0; // temperature
unsigned char r = 0; // LED RED value
unsigned char g = 0; // LED Green value
unsigned char b = 0; // LED Blue value
int32_t ReportingInterval = 10;  // Reporting Interval seconds

float applyModel(float h, float t) {
  // apply regression formula w1 + w2x + w3y 
  float regression = MODEL_INTERCEPT + MODEL_HUM_COEF * h + MODEL_TEMP_COEF * t;
  // return sigmoid logistic function on regression result
  return  1/(1 + exp(0.0 - (double)regression));
}

void callback(char* topic, byte* payload, unsigned int length) {
   // handle message arrived
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] : ");
  
  payload[length] = 0; // ensure valid content is zero terminated so can treat as c-string
  Serial.println((char *)payload);
  DeserializationError err = deserializeJson(jsonReceiveDoc, (char *)payload);
  if (err) {
    Serial.print(F("deserializeJson() failed with code ")); 
    Serial.println(err.c_str());
  } else {
    JsonObject cmdData = jsonReceiveDoc.as<JsonObject>();
    if (0 == strcmp(topic, MQTT_TOPIC_DISPLAY)) {
      //valid message received
      r = cmdData["r"].as<unsigned char>(); // this form allows you specify the type of the data you want from the JSON object
      g = cmdData["g"];
      b = cmdData["b"];
      jsonReceiveDoc.clear();
      pixel.setPixelColor(0, r, g, b);
      pixel.show();
    } else if (0 == strcmp(topic, MQTT_TOPIC_INTERVAL)) {
      //valid message received
      ReportingInterval = cmdData["Interval"].as<int32_t>(); // this form allows you specify the type of the data you want from the JSON object
      Serial.print("Reporting Interval has been changed:");
      Serial.println(ReportingInterval);
      jsonReceiveDoc.clear();
    } else {
      Serial.println("Unknown command received");
    }
  }
}


void setup() {
  // Start serial console
  Serial.begin(115200);
  Serial.setTimeout(2000);
  while (!Serial) { }
  Serial.println();
  Serial.println("ESP8266 Sensor Application");

  // Start WiFi connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi Connected");

  // Start connected devices
  dht.begin();
  pixel.begin();

  // Connect to MQTT - IBM Watson IoT Platform
   while(! mqtt.connected()){
    if (mqtt.connect(MQTT_DEVICEID, MQTT_USER, MQTT_TOKEN)) { // Token Authentication
//    if (mqtt.connect(MQTT_DEVICEID)) { // No Token Authentication
      Serial.println("MQTT Connected");
      mqtt.subscribe(MQTT_TOPIC_DISPLAY);
      mqtt.subscribe(MQTT_TOPIC_INTERVAL);
    } else {
      Serial.println("MQTT Failed to connect! ... retrying");
      delay(500);
    }
  }
}


void loop() {
  mqtt.loop();
  while (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqtt.connect(MQTT_DEVICEID, MQTT_USER, MQTT_TOKEN)) {
      Serial.println("MQTT Connected");
// Should verify the certificates here - like in the startup function
      mqtt.subscribe(MQTT_TOPIC_DISPLAY);
      mqtt.subscribe(MQTT_TOPIC_INTERVAL);
      mqtt.loop();
    } else {
      Serial.println("MQTT Failed to connect!");
      delay(5000);
    }
  }

  h = dht.readHumidity();
  t = dht.readTemperature(); // uncomment this line for Celsius
  // t = dht.readTemperature(true); // uncomment this line for Fahrenheit

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
  } else {
    /* Control of the LED is now handles by the incoming command
    // Set RGB LED Colour based on temp
    b = (t < ALARM_COLD) ? 255 : ((t < WARN_COLD) ? 150 : 0);
    r = (t >= ALARM_HOT) ? 255 : ((t > WARN_HOT) ? 150 : 0);
    g = (t > ALARM_COLD) ? ((t <= WARN_HOT) ? 255 : ((t < ALARM_HOT) ? 150 : 0)) : 0;
    pixel.setPixelColor(0, r, g, b);
    pixel.show();
    */

    // Apply the model to the sensor readings
    float modelPrediction = applyModel(h, t);

    // Print Message to console in JSON format
    status["temp"] = t;
    status["humidity"] = h;
    Serial.print("Model output = ");
    Serial.println(modelPrediction);
    status["class"] = modelPrediction < 0.5 ? 0 : 1;
    serializeJson(jsonDoc, msg, 50);
    Serial.println(msg);
    if (!mqtt.publish(MQTT_TOPIC, msg)) {
      Serial.println("MQTT Publish failed");
    }
  }

  Serial.print("ReportingInterval :");
  Serial.print(ReportingInterval);
  Serial.println();
  // Pause - but keep polling MQTT for incoming messages
  for (int32_t i = 0; i < ReportingInterval; i++) {
    mqtt.loop();
    delay(1000);
  }
}
