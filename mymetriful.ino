
#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

#include <Metriful_sensor.h>

// Wi-Fi and webserver settings 
const char* ssid = "CasaDiMi";
const char* password = "Muska1ova";
ESP8266WebServer server(80);


//helper for restart command
bool restartRequested = false;

// Structs for Metriful data
AirData_t airData = {0};
AirQualityData_t airQualityData = {0};
LightData_t lightData = {0};
SoundData_t soundData = {0};
ParticleData_t particleData = {0};

// JSON objects definition for the web serice response
DynamicJsonDocument doc(2048);
JsonObject temperature = doc["Temperature"].createNestedObject();
JsonObject pressure = doc["Pressure"].createNestedObject();
JsonObject humidity = doc["Humidity"].createNestedObject();
JsonObject air_quality = doc["AirQuality"].createNestedObject();
JsonObject next_read_after = doc["NextReadAfter"].createNestedObject();

// Reading intervals (used to calc when next metriful reading is)
long interval = 0;
long lastRead = 0;
int calcNextRead(){
  return (lastRead + interval - millis())/1000;
}

// Define routing
void restServerRouting() {
    server.on("/", HTTP_GET, []() {
        server.send(200, F("text/html"),
            F("Welcome to the REST Web Server"));
    });
    server.on(F("/restart"), HTTP_GET, callRestart);
    server.on(F("/measurements"), HTTP_GET, serveAll);
    server.on(F("/setInterval"), HTTP_GET, setInterval);
}


void callRestart() {
  restartRequested = true;
  server.send(200, "text/json", "the system restarts in 5 sec");
}

//Serve all measurements
void serveAll(){  
  //Temperature
  uint8_t T_intPart = 0;
  uint8_t T_fractionalPart = 0;
  bool isPositive = true;
  const char * unit = getTemperature(&airData, &T_intPart, &T_fractionalPart, &isPositive);

  temperature["pos-neg"] = isPositive?"+":"-";
  temperature["T_intPart"] = T_intPart;
  temperature["T_fractionalPart"] = T_fractionalPart;
  temperature["unit"] = unit;

  //Pressure
  pressure["value"] = airData.P_Pa;
  pressure["unit"] = "Pa";

  //Humidity
  humidity["H_pc_int"] = airData.H_pc_int;
  humidity["H_pc_fr_1dp"] = airData.H_pc_fr_1dp;
  humidity["unit"] ="%";

  //Air Quality
  air_quality["accuracy"] = interpret_AQI_accuracy(airQualityData.AQI_accuracy);
  if (airQualityData.AQI_accuracy > 0) {
    air_quality["AQI_int"] = airQualityData.AQI_int;
    air_quality["AQI_fr_1dp"] = airQualityData.AQI_fr_1dp;
    air_quality["interpretation"] = interpret_AQI_value(airQualityData.AQI_int);

    air_quality["CO2e_int"] =  airQualityData.CO2e_int;
    air_quality["CO2e_fr_1dp"] =  airQualityData.CO2e_fr_1dp;
    air_quality["CO2e_unit"] ="ppm";

    air_quality["bVOC_int"] = airQualityData.bVOC_int;
    air_quality["bVOC_fr_2dp"] = airQualityData.bVOC_fr_2dp;
    air_quality["bVOC_unit"] ="ppm";
  }
  
  //Next reading
  next_read_after["value"] = calcNextRead();
  next_read_after["unit"] = "seconds";

  String json;
  serializeJsonPretty(doc, json);
  server.send(200, "text/json", json);
}


//handle settings call 
void setInterval() {
  String response = "";
  if (server.arg("value")== ""){
    response = "Please supply interval (3s, 100s or 300s) in the value parameter";
  }
  else
  {
    uint8_t period = resolveCyclePeriodOption(server.arg("value"));
    if (period == -1)
    {
      response = "No valid value parameter supplied. Using default of 3s";
      setupMetriful(CYCLE_PERIOD_3_S);
    }
    else{
      response = "Metriful is set with the supplied interval of " + server.arg("value");
      setupMetriful(period);
    }
  }
  server.send(200, "text/json", response);
}

// Manage not found URL
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}


void setupMetriful(uint8_t cycle_period){
  // Initialize the host pins, set up the serial port and reset:
  SensorHardwareSetup(I2C_ADDRESS); 
  
  // Apply chosen settings to the MS430
  uint8_t particleSensor = PARTICLE_SENSOR;
  TransmitI2C(I2C_ADDRESS, PARTICLE_SENSOR_SELECT_REG, &particleSensor, 1);
  TransmitI2C(I2C_ADDRESS, CYCLE_TIME_PERIOD_REG, &cycle_period, 1);

  // Get the reading period to use for next read counter
  switch (cycle_period){
    case 0:
      interval = 3000;
      break;
    case 1:
      interval = 100000;
      break;
    case 2:
      interval = 300000;
      break;
  }
  
  ready_assertion_event = false;
  TransmitI2C(I2C_ADDRESS, CYCLE_MODE_CMD, 0, 0);
}

void setup(void) {
  Serial.begin(115200);
  WiFi.hostname("metriful");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");
 
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
 
  // Set server routing
  restServerRouting();
  // Set not found response
  server.onNotFound(handleNotFound);
  // Start server
  server.begin();
  Serial.println("HTTP server started");

  delay(1000);
  // Prep the metriful sensor
  setupMetriful(CYCLE_PERIOD_100_S);
}
 
void loop(void) {

  // Wait for the next new data release, indicated by a falling edge on READY
  while (!ready_assertion_event) {
    server.handleClient();
    // Handle restart requests
    if (restartRequested)
    {
      delay(5000);
      ESP.restart();
    }
    yield();
  }
  
  lastRead = millis();  
  ready_assertion_event = false;
  
  // Air data
  // Choose output temperature unit (C or F) in Metriful_sensor.h
  ReceiveI2C(I2C_ADDRESS, AIR_DATA_READ, (uint8_t *) &airData, AIR_DATA_BYTES);

  // Air quality data
  // The initial self-calibration of the air quality data may take several minutes
  ReceiveI2C(I2C_ADDRESS, AIR_QUALITY_DATA_READ, (uint8_t *) &airQualityData, AIR_QUALITY_DATA_BYTES);
}

//helper function for cycle period parameter resolution
uint8_t resolveCyclePeriodOption(String input) {
    if( input == "3s" ) return CYCLE_PERIOD_3_S;
    if( input == "100s" ) return CYCLE_PERIOD_100_S;
    if( input == "300s" ) return CYCLE_PERIOD_300_S;
    return -1;
}

