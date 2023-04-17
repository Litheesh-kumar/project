#include <Firebase_ESP_Client.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define WIFI_SSID "litheesh"
#define WIFI_PASSWORD "a12345678"
#define API_KEY "AIzaSyD3VJHDUMKmwexRm9T2FVT-TYITj5vZ1sw"
#define DATABASE_URL "https://final-review-39ca4-default-rtdb.firebaseio.com"

FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;
String fuelType;
float petrolPrice;
float dieselPrice;
float density15;
float price;
float temperature;
float litres;

unsigned long sendDataPrevMillis = 0;
int count = 0;
bool signupOK = false;
int flag = 1;
const int relayPin = 17;
const int solenoidPin = 15;
float cons = 0.9;
const int difference=5;
// HX711 Load Cell
HX711 scale;
float calibration_factor = -375;

// OneWire temperature sensor
OneWire oneWire(33);
DallasTemperature sensors(&oneWire);
byte sensorPin       = 25;
float calibrationFactor = 17.5;

volatile byte pulseCount;

float flowRate;
unsigned int flowMilliLitres;
unsigned long totalMilliLitres;
String alert;
unsigned long oldTime;

void setup() {
  // Initialize the relay pin as output
  pinMode(relayPin, OUTPUT);

  // Initialize the HX711 load cell
  scale.begin(21, 19);
  scale.set_scale(calibration_factor);

  // Initialize the temperature sensor
  sensors.begin();
  scale.tare();

  // Initialize the flow sensor pin as input
  pinMode(sensorPin, INPUT);
  digitalWrite(sensorPin, HIGH);

  // Turn on the relay
  digitalWrite(relayPin, HIGH);

  // Initialize flow rate and total milliliters
  pulseCount        = 0;
  flowRate          = 0.0;
  flowMilliLitres   = 0;
  totalMilliLitres  = 0;
  oldTime           = 0;
  attachInterrupt(sensorPin, pulseCounter, FALLING);
  // Connect to WiFi
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }

  // Initialize Firebase
  apiSetup();
  fireBaseSetup();
}

void fireBaseSetup() {

  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("ok");
    signupOK = true;
  }
  else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  while (flag) {
    if (Firebase.ready() && signupOK) {
      sendDataPrevMillis = millis();
      if (Firebase.RTDB.getString(&fbdo, "/fuelType")) {
        if (fbdo.dataType() == "string") {
          fuelType = fbdo.stringData();
          Serial.println(fuelType);
        }
        flag = 0;
      }
      else {
        Serial.println(fbdo.errorReason());
      }
    }
  }
  Serial.print("fuelType: ");
  Serial.print(fuelType);
  if (fuelType == "petrol") {
    cons = 0.9;
  }
  else {
    cons = 0.7;
  }
}

void apiSetup() {
  String rapidApiKey = "713077eb58mshd69d2b23fa55c50p10b0c8jsnb1ca44d1ce01";
  String url = "https://daily-petrol-diesel-lpg-cng-fuel-prices-in-india.p.rapidapi.com/v1/fuel-prices/today/india/tamil-nadu";

  HTTPClient http;
  http.begin(url);
  http.addHeader("x-rapidapi-host", "daily-petrol-diesel-lpg-cng-fuel-prices-in-india.p.rapidapi.com");
  http.addHeader("x-rapidapi-key", rapidApiKey);

  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    String payload = http.getString();
    int jsonStartIndex = payload.indexOf('{');
    String json = payload.substring(jsonStartIndex);

    Serial.println(json);
    // Here you can parse the JSON and extract the fuel prices for petrol and diesel
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, json);

    /*if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;

      }*/
    petrolPrice = doc["fuel"]["petrol"]["retailPrice"];
    dieselPrice = doc["fuel"]["diesel"]["retailPrice"];
    Serial.print("Today petrolPrice in TamilNadu is: ");
    Serial.println(petrolPrice);
    Serial.print("Today dieselPrice in TamilNadu is: ");
    Serial.println(dieselPrice);
  }
  else {
    Serial.println("Failed to fetch fuel prices");
  }

  http.end();
}

void loop() {
  // Measure the flow rate
  if ((millis() - oldTime) > 1000)   // Only process counters once per second
  {
    detachInterrupt(sensorPin);
    flowRate = ((1000.0 / (millis() - oldTime)) * pulseCount) / calibrationFactor;
    oldTime = millis();
    flowMilliLitres = (flowRate / 60) * 1000;
    totalMilliLitres += flowMilliLitres;
    litres = totalMilliLitres / 1000;

    unsigned int frac;

    // Print the flow rate for this second in litres / minute
    Serial.print("Flow rate: ");
    Serial.print(int(flowRate));  // Print the integer part of the variable
    Serial.print("L/min");
    Serial.print("\t");       // Print tab space

    // Print the cumulative total of litres flowed since starting
    Serial.print("Output Liquid Quantity: ");
    Serial.print(totalMilliLitres);
    Serial.println("mL");
    pulseCount = 0;
    attachInterrupt(sensorPin, pulseCounter, FALLING);
  }
  // If the total milliliters exceeds 100ml, turn off the solenoid valve by sending a high signal to the relay
  if (totalMilliLitres >= 100) {
    Serial.println("relay pin turned off");
    digitalWrite(relayPin, LOW);

    // Measure the temperaturew
    if (flowRate == 0) {
      sensors.requestTemperatures();
      temperature = sensors.getTempCByIndex(0);
      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.println("°C");

      // Measure the weight
      //float mass=10;
      float mass = scale.get_units(10);
      if (mass < 0)
      {
        mass = 0.00;
      }
      float Density = (+(mass / 100) * 1000)-27;
      Serial.print("observed Density: ");
      Serial.print(Density);

      density15 = Density + cons * (temperature - 15);
      Serial.print("density at 15 degree: ");
      Serial.println(density15);
      if (fuelType == "petrol") {
        price = petrolPrice * totalMilliLitres;
        price = price / 1000;
        Serial.print("price for the petrol intake: ");
        Serial.println(price);
      }
      if (fuelType == "diesel") {
        price = dieselPrice * totalMilliLitres;
        price = price / 1000;
        Serial.print("price for the diesel intake: ");
        Serial.println(price);
      }
        if(density15-Density>=difference || density15<=difference){
        alert="invalid";}
      if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0)) {
        sendDataPrevMillis = millis();
        Firebase.RTDB.setFloat(&fbdo, "output/Litre", litres);
        Firebase.RTDB.setFloat(&fbdo, "output/price", price);
        Firebase.RTDB.setFloat(&fbdo, "output/Density", density15);
        Firebase.RTDB.setFloat(&fbdo, "output/totalMillilitres", totalMilliLitres);
        Firebase.RTDB.setFloat(&fbdo, "output/petrolPrice", petrolPrice);
        Firebase.RTDB.setFloat(&fbdo, "output/temperature", temperature);
        Firebase.RTDB.setFloat(&fbdo, "output/dieselPrice", dieselPrice);
        Firebase.RTDB.setString(&fbdo, "output/caution", alert);
      }
    }

  }

}
void pulseCounter()
{
  // Increment the pulse counter
  pulseCount++;
}
