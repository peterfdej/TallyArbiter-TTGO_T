/*
#########################################################################################
include libs:
Websockets
Arduino_JSON
TFT_eSPI
MultiButton

Modify User_Setup_Select.h in libraryY TFT_eSPI
  //#include <User_Setup.h>
  #include <User_Setups/Setup25_TTGO_T_Display.h>
#########################################################################################
*/
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <Arduino_JSON.h>
#include <PinButton.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Arduino.h>
#include "esp_adc_cal.h"
#include "TallyArbiter.h"

#define ADC_EN  14  //ADC_EN is the ADC detection enable port
#define ADC_PIN 34

float battery_voltage;
String voltage;
int vref = 1100;
int batteryLevel = 100;
int barLevel = 0;
int LevelColor = TFT_WHITE;
bool backlight = true;
PinButton btntop(35); //top button, switch screen
PinButton btnbottom(0); //bottom button, screen on/off
Preferences preferences;

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

/* USER CONFIG VARIABLES
 *  Change the following variables before compiling and sending the code to your device.
 */

bool CUT_BUS = false; // true = Programm + Preview = Red Tally; false = Programm + Preview = Yellow Tally screen
bool LAST_MSG = true; // true = show messages on tally screen

//Wifi SSID and password
const char * networkSSID = "networkSSID";
const char * networkPass = "networkPass";

//For static IP Configuration, change USE_STATIC to true and define your IP address settings below
bool USE_STATIC = false; // true = use static, false = use DHCP
IPAddress clientIp(192, 168, 2, 5); // Static IP
IPAddress subnet(255, 255, 255, 0); // Subnet Mask
IPAddress gateway(192, 168, 2, 1); // Gateway

//Tally Arbiter Server
const char * tallyarbiter_host = "tallyarbiter"; //IP address or hostname of the Tally Arbiter Server
const int tallyarbiter_port = 4455;

/* END OF USER CONFIG */

//Tally Arbiter variables
SocketIOclient socket;
JSONVar BusOptions;
JSONVar Devices;
JSONVar DeviceStates;
String DeviceId = "Unassigned";
String DeviceName = "Unassigned";
String ListenerType = "m5-stickc";
bool mode_preview = false;  
bool mode_program = false;
const byte led_program = 25;  //red led   connected with 270ohm resistor
const byte led_preview = 27;  //green led connected with 270ohm resistor
const byte led_blue = 26;     //blue led  connected with 270ohm resistor
String LastMessage = "";

//General Variables
bool networkConnected = false;
int currentScreen = 0;        //0 = Tally Screen, 1 = Settings Screen

void espDelay(int ms) {
  esp_sleep_enable_timer_wakeup(ms * 1000);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_light_sleep_start();
}

void showVoltage() {
    static uint64_t timeStamp = 0;
    if (millis() - timeStamp > 5000) {
        timeStamp = millis();
        uint16_t v = analogRead(ADC_PIN);
        battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
        voltage = "Voltage :" + String(battery_voltage) + "V";
        batteryLevel = floor(100.0 * (((battery_voltage * 1.1) - 3.0) / (4.07 - 3.0))); //100%=3.7V, Vmin = 2.8V
        batteryLevel = batteryLevel > 100 ? 100 : batteryLevel;
        barLevel = 133 - (batteryLevel * 133/100);
        if (battery_voltage >= 3){
          LevelColor = TFT_WHITE;
        }
        else {
          LevelColor = TFT_RED;
        }
        if (currentScreen == 0){
          tft.fillRect(232, 0, 8, 135, LevelColor);
          tft.fillRect(233, 1, 6, barLevel, TFT_BLACK);
        }
        if (battery_voltage < 2.8){ //go into sleep,awake with top button
          tft.setRotation(1);
          tft.setCursor(0, 0);
          tft.fillScreen(TFT_BLACK);
          tft.setTextColor(TFT_WHITE);
          tft.setTextSize(2);
          tft.println("Battery level low");
          tft.println("Going into sleepmode");
          espDelay(5000);
          tft.writecommand(TFT_DISPOFF);
          tft.writecommand(TFT_SLPIN);
          //After using light sleep, you need to disable timer wake, because here use external IO port to wake up
          esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
          // esp_sleep_enable_ext1_wakeup(GPIO_SEL_35, ESP_EXT1_WAKEUP_ALL_LOW);
          esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
          delay(200);
          esp_deep_sleep_start();
        }
    }
}

void showSettings() {
  //displays the current network connection and Tally Arbiter server data
  tft.setCursor(0, 0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.println("SSID: " + String(networkSSID));
  tft.println(WiFi.localIP());
  tft.println();
  tft.println("Tally Arbiter Server:");
  tft.println(String(tallyarbiter_host) + ":" + String(tallyarbiter_port));
  tft.println();
  Serial.println(voltage);
  if(battery_voltage >= 4.2){
    tft.println("Battery charging...");   // show when TTGO is plugged in
  }
  else if (battery_voltage < 3) {
    tft.println("Battery empty. Recharge!!");
  }
  else {
    tft.println("Battery:" + String(batteryLevel) + "%");
     }

}

void showDeviceInfo() {
  //displays the currently assigned device and tally data
  evaluateMode();
}

void logger(String strLog, String strType) {
  if (strType == "info") {
    Serial.println(strLog);
    int x = strLog.length();
    for (int i=0; i < x; i=i+19) {
      tft.println(strLog.substring(0,19));
      strLog = strLog.substring(19);
    }
  }
  else {
    Serial.println(strLog);
  }
}

void connectToNetwork() {
  logger("Connecting to SSID: " + String(networkSSID), "info");
  WiFi.disconnect(true);
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_STA); //station
  WiFi.setSleep(false);
  if(USE_STATIC == true) {
    WiFi.config(clientIp, gateway, subnet);
  }
  WiFi.begin(networkSSID, networkPass);
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      logger("Network connected!", "info");
      logger(WiFi.localIP().toString(), "info");
      networkConnected = true;
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      tft.setCursor(0, 0);
      tft.fillScreen(TFT_BLACK);
      tft.setTextSize(2);
      logger("Network connection lost!", "info");
      digitalWrite(led_blue, HIGH);
      networkConnected = false;
      delay(1000);
      connectToNetwork();
      break;
  }
}

void ws_emit(String event, const char *payload = NULL) {
  if (payload) {
    String msg = "[\"" + event + "\"," + payload + "]";
    socket.sendEVENT(msg);
  } else {
    String msg = "[\"" + event + "\"]";
    socket.sendEVENT(msg);
  }
}

void connectToServer() {
  logger("Connecting to Tally Arbiter host: " + String(tallyarbiter_host), "info");
  socket.onEvent(socket_event);
  socket.begin(tallyarbiter_host, tallyarbiter_port);
}

void socket_event(socketIOmessageType_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case sIOtype_DISCONNECT:
      socket_Disconnected();
      break;
    case sIOtype_CONNECT:
      socket_Connected((char*)payload, length);
      break;
    case sIOtype_ACK:
    case sIOtype_ERROR:
    case sIOtype_BINARY_EVENT:
    case sIOtype_BINARY_ACK:
      // Not handled
      break;

    case sIOtype_EVENT:
      String msg = (char*)payload;
      String type = msg.substring(2, msg.indexOf("\"",2));
      String content = msg.substring(type.length() + 4);
      content.remove(content.length() - 1);

      logger("Got event '" + type + "', data: " + content, "info-quiet");

      if (type == "bus_options") BusOptions = JSON.parse(content);
      if (type == "reassign") socket_Reassign(content);
      if (type == "flash") socket_Flash();
      if (type == "messaging") socket_Messaging(content);

      if (type == "deviceId") {
        DeviceId = content.substring(1, content.length()-1);
        SetDeviceName();
        showDeviceInfo();
        currentScreen = 0;
      }

      if (type == "devices") {
        Devices = JSON.parse(content);
        SetDeviceName();
      }

      if (type == "device_states") {
        DeviceStates = JSON.parse(content);
        processTallyData();
      }

      break;
  }
}

void socket_Disconnected() {
  digitalWrite(led_blue, HIGH);
  tft.setCursor(0, 0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  logger("Disconnected from   TallyArbiter!", "info");
}

void socket_Connected(const char * payload, size_t length) {
  logger("Connected to Tally Arbiter server.", "info");
  digitalWrite(led_blue, LOW);
  tft.fillScreen(TFT_BLACK);
  String deviceObj = "{\"deviceId\": \"" + DeviceId + "\", \"listenerType\": \"" + ListenerType + "\"}";
  char charDeviceObj[1024];
  strcpy(charDeviceObj, deviceObj.c_str());
  ws_emit("bus_options");
  ws_emit("device_listen_m5", charDeviceObj);
  ws_emit("devices");
}

void socket_Flash() {
  //flash the screen white 3 times
  tft.fillScreen(TFT_WHITE);
  digitalWrite(led_blue, HIGH);
  delay(500);
  tft.fillScreen(TFT_BLACK);
  digitalWrite(led_blue, LOW);
  delay(500);
  tft.fillScreen(TFT_WHITE);
  digitalWrite(led_blue, HIGH);
  delay(500);
  tft.fillScreen(TFT_BLACK);
  digitalWrite(led_blue, LOW);
  delay(500);
  tft.fillScreen(TFT_WHITE);
  digitalWrite(led_blue, HIGH);
  delay(500);
  tft.fillScreen(TFT_BLACK);
  digitalWrite(led_blue, LOW);
  
  //then resume normal operation
  switch (currentScreen) {
      case 0:
        showDeviceInfo();
        break;
      case 1:
        showSettings();
        break;
  }
}

String strip_quot(String str) {
  if (str[0] == '"') {
    str.remove(0, 1);
  }
  if (str.endsWith("\"")) {
    str.remove(str.length()-1, 1);
  }
  return str;
}

void socket_Reassign(String payload) {
  String oldDeviceId = payload.substring(0, payload.indexOf(','));
  String newDeviceId = payload.substring(oldDeviceId.length()+1);
  oldDeviceId = strip_quot(oldDeviceId);
  newDeviceId = strip_quot(newDeviceId);
  
  String reassignObj = "{\"oldDeviceId\": \"" + oldDeviceId + "\", \"newDeviceId\": \"" + newDeviceId + "\"}";
  char charReassignObj[1024];
  strcpy(charReassignObj, reassignObj.c_str());
  //socket.emit("listener_reassign_object", charReassignObj);
  ws_emit("listener_reassign_object", charReassignObj);
  ws_emit("devices");
  tft.fillScreen(TFT_WHITE);
  digitalWrite(led_blue, HIGH);
  delay(200);
  tft.fillScreen(TFT_BLACK);
  digitalWrite(led_blue, LOW);
  delay(200);
  tft.fillScreen(TFT_WHITE);
  digitalWrite(led_blue, HIGH);
  delay(200);
  tft.fillScreen(TFT_BLACK);
  digitalWrite(led_blue, LOW);
  DeviceId = newDeviceId;
  preferences.begin("tally-arbiter", false);
  preferences.putString("deviceid", newDeviceId);
  preferences.end();
  SetDeviceName();
}

void socket_Messaging(String payload) {
  String strPayload = String(payload);
  int typeQuoteIndex = strPayload.indexOf(',');
  String messageType = strPayload.substring(0, typeQuoteIndex);
  messageType.replace("\"", "");
  //Only messages from producer and clients.
  if (messageType != "server") {
    int messageQuoteIndex = strPayload.lastIndexOf(',');
    String message = strPayload.substring(messageQuoteIndex + 1);
    message.replace("\"", "");
    LastMessage = messageType + ": " + message;
    evaluateMode();
  }
}

void processTallyData() {
  for (int i = 0; i < DeviceStates.length(); i++) {
    if (getBusTypeById(JSON.stringify(DeviceStates[i]["busId"])) == "\"preview\"") {
      if (DeviceStates[i]["sources"].length() > 0) {
        mode_preview = true;
      }
      else {
        mode_preview = false;
      }
    }
    if (getBusTypeById(JSON.stringify(DeviceStates[i]["busId"])) == "\"program\"") {
      if (DeviceStates[i]["sources"].length() > 0) {
        mode_program = true;
      }
      else {
        mode_program = false;
      }
    }
  }
  currentScreen = 0;
  evaluateMode();
}

String getBusTypeById(String busId) {
  for (int i = 0; i < BusOptions.length(); i++) {
    if (JSON.stringify(BusOptions[i]["id"]) == busId) {
      return JSON.stringify(BusOptions[i]["type"]);
    }
  }
  return "invalid";
}

void SetDeviceName() {
  for (int i = 0; i < Devices.length(); i++) {
    if (JSON.stringify(Devices[i]["id"]) == "\"" + DeviceId + "\"") {
      String strDevice = JSON.stringify(Devices[i]["name"]);
      DeviceName = strDevice.substring(1, strDevice.length() - 1);
      break;
    }
  }
  preferences.begin("tally-arbiter", false);
  preferences.putString("devicename", DeviceName);
  preferences.end();
  evaluateMode();
}

void evaluateMode() {
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  
  if (mode_preview && !mode_program) {
    logger("The device is in preview.", "info-quiet");
    digitalWrite(led_program, LOW);
    digitalWrite(led_preview, HIGH);
    tft.setTextColor(TFT_BLACK);
    tft.fillScreen(TFT_GREEN);
  }
  else if (!mode_preview && mode_program) {
    logger("The device is in program.", "info-quiet");
    digitalWrite(led_program, HIGH);
    digitalWrite(led_preview, LOW);
    tft.setTextColor(TFT_BLACK);
    tft.fillScreen(TFT_RED);
  }
  else if (mode_preview && mode_program) {
    logger("The device is in preview+program.", "info-quiet");
    tft.setTextColor(TFT_BLACK);
    if (CUT_BUS == true) {
      tft.fillScreen(TFT_RED);
    }
    else {
      tft.fillScreen(TFT_YELLOW);
    }
    digitalWrite(led_program, HIGH);
    digitalWrite(led_preview, LOW);
  }
  else {
    digitalWrite(led_program, LOW);
    digitalWrite(led_preview, LOW);
    tft.setTextColor(TFT_WHITE);
    tft.fillScreen(TFT_BLACK);
  }
  tft.println(DeviceName);
  tft.println("-------------------");
  if (LAST_MSG == true) {
    //tft.println(LastMessage);
    logger(LastMessage, "info");
  }
  tft.fillRect(232, 0, 8, 135, LevelColor);
  tft.fillRect(233, 1, 6, barLevel, TFT_BLACK);
}


void setup(void) {
  Serial.begin(115200);
  while (!Serial);
/*
  ADC_EN is the ADC detection enable port
  If the USB port is used for power supply, it is turned on by default.
  If it is powered by battery, it needs to be set to high level
  */
  pinMode(ADC_EN, OUTPUT);
  digitalWrite(ADC_EN, HIGH);

  // Initialize the TTGO object
  logger("Initializing TTGO.", "info-quiet");
  setCpuFrequencyMhz(80);    //Save battery by turning down the CPU clock
  btStop();                  //Save battery by turning off BlueTooth
  tft.init();
  tft.setRotation(1);
  tft.setCursor(0, 0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setSwapBytes(true);
  tft.pushImage(0, 0,  240, 135, TallyArbiter);
  espDelay(5000);
  logger("Tally Arbiter TTGO Listener Client booting.", "info");
  
  connectToNetwork(); //starts Wifi connection
  while (!networkConnected) {
    delay(200);
  }

  pinMode(led_program, OUTPUT);
  pinMode(led_preview, OUTPUT);
  pinMode(led_blue, OUTPUT);
  digitalWrite(led_blue, HIGH);
  Serial.println("Blue LED ON.");

  preferences.begin("tally-arbiter", false);
  if(preferences.getString("deviceid").length() > 0){
    DeviceId = preferences.getString("deviceid");
  }
  if(preferences.getString("devicename").length() > 0){
    DeviceName = preferences.getString("devicename");
  }
  preferences.end();
  
  connectToServer();
}

void loop() {
  socket.loop();
  btntop.update();
  btnbottom.update();
  showVoltage();

  if (btntop.isClick()) {
    switch (currentScreen) {
      case 0:
        showSettings();
        currentScreen = 1;
        break;
      case 1:
        showDeviceInfo();
        currentScreen = 0;
        break;
    }
  }
  if (btnbottom.isClick()) {
    if (backlight == true) {
      digitalWrite(TFT_BL, LOW);
      backlight = false;
    } else {
      digitalWrite(TFT_BL, HIGH);
      backlight = true;
    }
  }
}
