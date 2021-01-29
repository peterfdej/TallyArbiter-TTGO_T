/*
#########################################################################################
include libs:
Websockets
SocketIoClient
Arduino_JSON
TFT_eSPI
MultiButton

modify Arduino\libraries\SocketIoClient\SocketIoClient.cpp
//hexdump(payload, length);       ## remove this line (41) with the //

  and add boolean SocketIoConnected 2x:
  
  void SocketIoClient::webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  String msg;
  switch(type) {
    case WStype_DISCONNECTED:
      SOCKETIOCLIENT_DEBUG("[SIoC] Disconnected!\n");
      SocketIoConnected = false;  ##add this line
      break;
    case WStype_CONNECTED:
      SOCKETIOCLIENT_DEBUG("[SIoC] Connected to url: %s\n",  payload);
      SocketIoConnected = true;   ##add this line
      break;

modify Arduino\libraries\SocketIoClient\SocketIoClient.h add boolean SocketIoConnected
  public:
    void beginSSL(const char* host, const int port = DEFAULT_PORT, const char* url = DEFAULT_URL, const char* fingerprint = DEFAULT_FINGERPRINT);
    void begin(const char* host, const int port = DEFAULT_PORT, const char* url = DEFAULT_URL);
    void loop();
    void on(const char* event, std::function<void (const char * payload, size_t length)>);
    void emit(const char* event, const char * payload = NULL);
    void disconnect();
    void setAuthorization(const char * user, const char * password);
    bool SocketIoConnected;     ##add this line
 
Modify User_Setup_Select.h in libraryY TFT_eSPI
  //#include <User_Setup.h>
  #include <User_Setups/Setup25_TTGO_T_Display.h>
#########################################################################################
*/
#include <WiFi.h>
#include <SocketIoClient.h>
#include <Arduino_JSON.h>
#include <PinButton.h>
#include <Preferences.h>
#include <TFT_eSPI.h>       // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <Arduino.h>

const uint8_t vbatPin = 34;
float VBAT= 0;              // battery voltage from ESP32 ADC read
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

/* USER CONFIG VARIABLES
 *  Change the following variables before compiling and sending the code to your device.
 */
//Wifi SSID and password
const char * networkSSID = "NetworkSSID";
const char * networkPass = "NetworkPass";

//For static IP Configuration, change USE_STATIC to true and define your IP address settings below
bool USE_STATIC = false; // true = use static, false = use DHCP
IPAddress clientIp(192, 168, 2, 5); // Static IP
IPAddress subnet(255, 255, 255, 0); // Subnet Mask
IPAddress gateway(192, 168, 2, 1); // Gateway

//Tally Arbiter Server
const char * tallyarbiter_host = "192.168.0.3"; //IP address of the Tally Arbiter Server
const int tallyarbiter_port = 4455;

/* END OF USER CONFIG */

//ESP32 TTGO variables
PinButton btn1(0); //bottom button, screen on/off
PinButton btnAction(35); //top button, switch screen
Preferences preferences;
uint8_t wasPressed();
bool backlight = true;

//Tally Arbiter variables
SocketIoClient socket;
JSONVar BusOptions;
JSONVar Devices;
JSONVar DeviceStates;
String DeviceId = "Unassigned";
String DeviceName = "Unassigned";
String ListenerType = "m5-stickc";
bool mode_preview = false;  
bool mode_program = false;
const byte led_program = 25;  //red led
const byte led_preview = 27;  //green led
const byte led_blue = 26;     //blue led
String LastMessage = "";

//General Variables
bool networkConnected = false;
int currentScreen = 0;        //0 = Tally Screen, 1 = Settings Screen

void setup(void) {
  Serial.begin(115200);
  while (!Serial);

  // Initialize the TTGO object
  logger("Initializing TTGO.", "info-quiet");
  setCpuFrequencyMhz(80);    //Save battery by turning down the CPU clock
  btStop();                  //Save battery by turning off BlueTooth
  tft.setRotation(1);
  tft.setCursor(0, 0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.init();
  logger("Tally Arbiter TTGO Listener Client booting.", "info");
  
  delay(1000); //wait 1000ms before moving on
  connectToNetwork(); //starts Wifi connection
  while (!networkConnected) {
    delay(200);
  }

  pinMode(vbatPin, INPUT);
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
  btn1.update();
  btnAction.update();

  if (btnAction.isClick()) {
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

  if (btn1.isClick()) {
    if (backlight == true) {
      digitalWrite(TFT_BL, LOW);
      backlight = false;
    } else {
      digitalWrite(TFT_BL, HIGH);
      backlight = true;
    }
  }
  if (!socket.SocketIoConnected){
    tft.setCursor(0, 0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    logger("Disconnected from TallyArbiter", "info");
    digitalWrite(led_blue, HIGH);
    delay(100);
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
  VBAT = (float)(analogRead(vbatPin)) * 3600 / 4095 * 2;
  int batteryLevel = floor(100.0 * (((VBAT * 1.1 / 1000) - 3.0) / (4.07 - 3.0)));
  batteryLevel = batteryLevel > 100 ? 100 : batteryLevel;
  if(batteryLevel >= 100){
    tft.println("Battery charging...");   // show when TTGO is plugged in
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
    tft.println(strLog);
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
      logger("Network connection lost!", "info");
      networkConnected = false;
      delay(1000);
      connectToNetwork();
      break;
  }
}

void connectToServer() {
  logger("Connecting to Tally Arbiter host: " + String(tallyarbiter_host), "info");
  socket.on("connect", socket_Connected);
  socket.on("bus_options", socket_BusOptions);
  socket.on("deviceId", socket_DeviceId);
  socket.on("devices", socket_Devices);
  socket.on("device_states", socket_DeviceStates);
  socket.on("flash", socket_Flash);
  socket.on("reassign", socket_Reassign);
  socket.on("messaging", socket_Messaging);
  socket.begin(tallyarbiter_host, tallyarbiter_port);
}

void socket_Connected(const char * payload, size_t length) {
  logger("Connected to Tally Arbiter server.", "info");
  digitalWrite(led_blue, LOW);
  tft.fillScreen(TFT_BLACK);
  String deviceObj = "{\"deviceId\": \"" + DeviceId + "\", \"listenerType\": \"" + ListenerType + "\"}";
  char charDeviceObj[1024];
  strcpy(charDeviceObj, deviceObj.c_str());
  socket.emit("bus_options");
  socket.emit("device_listen_m5", charDeviceObj);
  socket.emit("devices");
}

void socket_BusOptions(const char * payload, size_t length) {
  BusOptions = JSON.parse(payload);
}

void socket_Devices(const char * payload, size_t length) {
  Devices = JSON.parse(payload);
  SetDeviceName();
}

void socket_DeviceId(const char * payload, size_t length) {
  DeviceId = String(payload);
  SetDeviceName();
  showDeviceInfo();
  currentScreen = 0;
}

void socket_DeviceStates(const char * payload, size_t length) {
  DeviceStates = JSON.parse(payload);
  processTallyData();
}

void socket_Flash(const char * payload, size_t length) {
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

void socket_Reassign(const char * payload, size_t length) {
  String oldDeviceId = String(payload).substring(0,8);
  String newDeviceId = String(payload).substring(11);
  String reassignObj = "{\"oldDeviceId\": \"" + oldDeviceId + "\", \"newDeviceId\": \"" + newDeviceId + "\"}";
  char charReassignObj[1024];
  strcpy(charReassignObj, reassignObj.c_str());
  socket.emit("listener_reassign_object", charReassignObj);
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

void socket_Messaging(const char * payload, size_t length) {
  String strPayload = String(payload);
  int typeQuoteIndex = strPayload.indexOf("\"");
  String messageType = strPayload.substring(0, typeQuoteIndex);
  //Only messages from producer and clients.
  if (messageType != "server") {
    int messageQuoteIndex = strPayload.lastIndexOf("\"");
    String message = strPayload.substring(messageQuoteIndex+1);
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
    digitalWrite(led_program, HIGH);
    digitalWrite(led_preview, LOW);
    tft.setTextColor(TFT_BLACK);
    tft.fillScreen(TFT_YELLOW);
  }
  else {
    digitalWrite(led_program, LOW);
    digitalWrite(led_preview, LOW);
    tft.setTextColor(TFT_WHITE);
    tft.fillScreen(TFT_BLACK);
  }
  tft.println(DeviceName);
  tft.println("--------------------");
  tft.println(LastMessage);
}
