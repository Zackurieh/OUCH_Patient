
#include <TFT_eSPI.h> // Hardware-specific library
//#include <SPI.h>
//#include "Pain_Pics.h"
#include "TestFace.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "time.h"

//Wifi details
const char* ssid = "KSU Guest"; // "SETUP-94C1"; // 
const char* password = "cheery2601dozed";

//MQTT Details
const char* server = "b3c2e9f97aa1449ea629a6e7fc89071c.s1.eu.hivemq.cloud"; //Server for the MQTT
const int port = 8883;
const char* MQTT_username = "ouchTeam";
const char* MQTTPass = "1OuchKSU$";

char topic[50] = ""; //"ece591/Push/";
char painTopic[50] = ""; //"ece591/PainLvl/"; //This will be changed
char battTopic[50] = ""; //"ece591/Battery/";

//Json Stuff
JsonDocument doc;  //Sets up a document for Json
WiFiClientSecure espClient;
PubSubClient client(espClient);

//Pin Info (button and voltage reading)
const int voltPin = 35;
const int batteryPin = 32;
const int buttonPin = 34;
unsigned long buttonStart;
bool buttonState = false; //Is the button pushed
bool buttonPressed = false; //Has the button been pushed long enough?


//Analog voltage reading, initialize to 0 (range from 0-4095)
int ADC_reading = 0;
//Convert ADC_reading to be a 1-10 value (resolution of 1240 parts per volt)
int one_to_ten;          //This is the value we want to send
float minVoltage = 1.23; //Find the minimum voltage on the track (for pain lvl 10)
float maxVoltage = 1.86; //Find max voltage on track (for pain lvl 1)
int resVal = 1240;       // Resolution/Volt or 4095/3.3

//Battery Reading
int battery_read = 0;
int batt_percent = 0;
float minBattery = 3.32/2;
float maxBattery = 3.80/2;
int previous_battery = 100;

unsigned long lastDisplay = millis();
unsigned long lastBattery = millis();
int lastPain = 100;

//Filtering Constants
const int filterSize = 250; //Number of values to add for a filtering effect
signed long listofData[filterSize] = {0};
signed long dataSum = 0;
int filterCount = 0;
float averageVal;
//Battery Filter
signed long batteryList[filterSize] = {0};
unsigned long batterySum = 0;
float batteryAverage;


///LCD Screen Setup
TFT_eSPI tft = TFT_eSPI();

#define MAX_IMAGE_WIDTH 290 // Adjust for your images
const unsigned short* faceArray[5] = {TwoPain, FourPain, SixPain, EightPain, TenPain};
int x_num_pos = 155;
int y_num_pos = 85;
int batt_xval = 360;
int batt_yval = 0;

//Get ESP32 Unique Address
uint8_t baseMac[6];
char Mac_address[20];


//TimeStamping Stuff
const char* ntpServer = "pool.ntp.org";
String curr_date;
String curr_time;
void printLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  //Serial.println(&timeinfo, "%A %B %d %Y %H:%M:%S");
  //Get the current date and time
  curr_date = String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday) + "-" + String(timeinfo.tm_year+1900);
  curr_time = String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
}


void setup() {
  Serial.begin(38400);

  // Initialise the TFT
  tft.begin();
  tft.fillScreen(TFT_WHITE);
  tft.setRotation(3);

  setup_wifi();
  client.setServer(server, port);
  client.setCallback(callback);
  delay(300);
  espClient.setInsecure();
  pinMode(buttonPin, INPUT);

  configTime(3600*-5, 0, ntpServer);
  printLocalTime();

  //Get the individual MAC address
  esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  sprintf(Mac_address, "%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  snprintf(painTopic, 50, "ece591/%s/Pain", Mac_address);
  snprintf(battTopic, 50, "ece591/%s/Batt", Mac_address);
  snprintf(topic, 50, "ece591/%s/Name", Mac_address);

  Serial.println("Setup Complete");
}

void loop() {
  //Keep the client connected to PubSub
  if (!client.connected()) {
    reconnect();
  }
  client.loop();


  //Read in the voltage readings (Battery and Pain state)
  ADC_reading = analogRead(voltPin);
  battery_read = analogRead(batteryPin);

  //Put data in filter
  dataSum += ADC_reading;
  dataSum -= listofData[filterCount];         //Subtract the oldest data in filter
  listofData[filterCount] = ADC_reading;      //Replace oldest data with new data

  batterySum += battery_read;
  batterySum -= batteryList[filterCount];
  batteryList[filterCount] = battery_read;
  filterCount = ++filterCount % filterSize;         //Increment filter position

  //If button is pushed
  if (digitalRead(buttonPin) == HIGH){
    //If button has not been pushed yet
    if (buttonState == false){
      buttonStart = millis();
      buttonState = true;
    }
    //If button has been pushed for a while
    else if (millis() - buttonStart >= 50 && buttonState == true){
      buttonPressed = true; //State we have been pushed and reset state
      buttonState = false;
    }
  }
  //If button is not pushed
  else{
    buttonState = false;
    //If button just got released, transmit data then reset buttonPressed
    if (buttonPressed == true){
      averageVal = (dataSum / filterSize); //Compute the Average (filter) and convert to 1-10
    
      //Original bounds are the .96-2.05V readings (ADC reading from 1200-2545)
      one_to_ten = map(averageVal, minVoltage*resVal, maxVoltage*resVal, 10, 1);


      //This is where MQTT Messaging will go
      Serial.println("\n\nSend Reading!!!!!");
      Serial.print("Said Reading: ");
      Serial.println(one_to_ten);

      //Send MQTT Message
      printLocalTime();
      doc.clear();
      doc["Pain"] = one_to_ten;
      doc["Date"] = curr_date;
      doc["Time"] = curr_time;
      char output[75];
      serializeJson(doc, output);
      client.publish(painTopic, output);

      tft.setTextSize(3);
      tft.drawString("Sent", x_num_pos+240, y_num_pos+180);
      tft.fillCircle(50, 100, 30, TFT_WHITE);

      buttonPressed = false;
    }
  }

  //Calculate the data every second
  if (millis() - lastDisplay >= 1000){
    averageVal = (dataSum / filterSize); //Compute the Average (filter) and convert to 1-10

    one_to_ten = map(averageVal, minVoltage*resVal, maxVoltage*resVal, 10, 1);

    //If the value has changed, update screen
    if (lastPain != one_to_ten){
      if (lastPain > 10 || lastPain < 1){
        tft.fillRect(0, y_num_pos+100, 320, 480-y_num_pos, TFT_WHITE);
      } //If bad input last time, reset the bottom half of screen

      //LCD Stuff!!!
      tft.fillRect(x_num_pos-50, y_num_pos+180, 300, 100, TFT_WHITE); //Clear the number
      tft.setTextSize(50);
      tft.drawNumber(one_to_ten, x_num_pos+50, y_num_pos+180);
      tft.setSwapBytes(true);

      switch ((one_to_ten-1)/2) {
        case 0:
          tft.pushImage(x_num_pos, y_num_pos, 170, 170, TwoPain); //faceArray[index]);
          break;
        case 1:
          tft.pushImage(x_num_pos, y_num_pos, 170, 170, FourPain); //faceArray[index]);
          break;
        case 2:
          tft.pushImage(x_num_pos, y_num_pos, 170, 170, SixPain); //faceArray[index]);
          break;
        case 3:
          tft.pushImage(x_num_pos, y_num_pos, 170, 170, EightPain); //faceArray[index]);
          break;
        case 4:
          tft.pushImage(x_num_pos, y_num_pos, 170, 170, TenPain); //faceArray[index]);
          break;
        default:
          tft.fillRect(x_num_pos, y_num_pos, 300, 300, TFT_WHITE);
          tft.setTextColor(TFT_BLACK);
          tft.setTextSize(5);
          tft.drawString("Bad Input", x_num_pos-50, y_num_pos+180);
      }
      lastPain = one_to_ten;

      Serial.print("Pain Reading Changed to: ");
      Serial.println(one_to_ten);
      Serial.println();
    }
    lastDisplay = millis();
  }

  //Send battery % every 5 seconds
  if(millis() - lastBattery >= 5000){

    batteryAverage = batterySum / filterSize;
    batt_percent = map(batteryAverage, minBattery*resVal, maxBattery*resVal, 0, 100);

    //Send current battery status
    Serial.print("Battery Percentage: ");
    Serial.println(batt_percent);

    //Send the battery value over MQTT if battery% is less than 20
    if (batt_percent < 20){
      printLocalTime();
      doc.clear();
      doc["Battery"] = batt_percent;
      doc["Date"] = curr_date;
      doc["Time"] = curr_time;
      char output[75];
      serializeJson(doc, output);
      client.publish(battTopic, output);
    }

    if(batt_percent < 0){
      batt_percent = 0;
    }
    else if (batt_percent > 100){
      batt_percent = 100;
    }
    //Print Battery % on the LCD screen when the battery % has changed
    if(batt_percent != previous_battery){
      //Draw the battery graphic
      tft.fillRect(batt_xval, batt_yval, 200, 60, TFT_WHITE);
      tft.drawRoundRect(batt_xval, batt_yval, 108, 58, 4, TFT_BLACK);
      tft.fillRect(batt_xval+108, batt_yval+19, 8, 20, TFT_BLACK);

      //Put the green ratio (%) in
      tft.fillRect(batt_xval+4, batt_yval+4, 108-8, 58-8, TFT_WHITE);
      tft.fillRect(batt_xval+4, batt_yval+4, batt_percent, 58-8, TFT_GREEN);
      tft.setTextSize(3);
      tft.drawString(String(batt_percent)+"%", batt_xval+20, batt_yval+20);

      previous_battery = batt_percent;
    }

    tft.fillRect(x_num_pos+230, y_num_pos+180, 300, 100, TFT_WHITE);
    lastBattery = millis();
  }
}

//Connect to the wifi
void setup_wifi() {
  delay(10);
  //Display on screen
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(6);
  tft.drawString("Connecting", 70, 50);
  tft.drawString("to Wifi", 135, 120);

  WiFi.begin(ssid); //, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("Connected");

  tft.fillScreen(TFT_WHITE);
}

//Connect to the MQTT Broker
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    tft.fillScreen(TFT_WHITE);
    tft.setTextSize(6);
    tft.drawString("Connecting", 70, 50);
    tft.drawString("to MQTT", 135, 120);
    Serial.print("Attempting MQTT connection...");

    // Attempt to connect
    String clientID = "ESP32Hoang-";
    clientID += String(random(0xffff), HEX);  //Create random ID
    if (client.connect(clientID.c_str(), MQTT_username, MQTTPass)) {
      Serial.println("connected");             //State that we are connected
      client.publish(topic, "ESP Connected");  //Publish hello world
      // ... and resubscribe
      client.subscribe(topic);
      tft.fillScreen(TFT_WHITE);
    } 
    else {
      tft.fillScreen(TFT_WHITE);
      tft.setTextSize(5);
      tft.drawString("Failed, rc= ", 40, 50);
      tft.drawNumber(client.state(), 390, 50);
      tft.drawString("Try in 5 seconds", 10, 120);

      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    message += (char)payload[i];
  }
  Serial.println();
  deserializeJson(doc, message);  //Puts the json into doc
  if (doc.containsKey("ID")) {
    String Patient_Name = doc["ID"];
    Serial.println(Patient_Name); //This was here to debug. Json is exporting correctly
    
    tft.fillRect(0, 0, batt_xval-1, 85, TFT_WHITE);
    tft.setTextSize(5);
    tft.drawString(Patient_Name, 20, 20);
  }
  else if (doc.containsKey("Push")){
    tft.fillCircle(50, 100, 30, TFT_RED);
  }
}
