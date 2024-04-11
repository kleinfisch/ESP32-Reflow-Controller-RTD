// ***** INCLUDES *****
#include <Adafruit_ILI9341.h>
#include <Adafruit_MAX31865.h>
#include "Adafruit_GFX.h"
#include <Fonts/FreeSans9pt7b.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Update.h>
#include "FS.h"
#include <SD.h>
#include <WiFiManager.h>
#include "SPI.h"
#include "config.h"
#include "Button.h"
#include <SPIFFS.h>

HTTPClient http;


// Use hardware SPI
Adafruit_ILI9341 display = Adafruit_ILI9341(display_cs, display_dc, display_rst);
//Adafruit_ILI9341 display = Adafruit_ILI9341(display_cs, display_dc, display_mosi, display_sclk, display_rst);

Adafruit_MAX31865 max31865 = Adafruit_MAX31865(13);


#define FORMAT_SPIFFS_IF_FAILED true

Preferences preferences;
WiFiManager wm;
char msg_buf[10];

#define DEBOUNCE_MS 100
Button AXIS_Y = Button(BUTTON_AXIS_Y, true, DEBOUNCE_MS);
Button AXIS_X = Button(BUTTON_AXIS_X, true, DEBOUNCE_MS);

int digitalButtonPins[] = {BUTTON_SELECT, BUTTON_MENU, BUTTON_BACK};

#define numDigButtons sizeof(digitalButtonPins)

int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;
unsigned long lastDebounceTime_ = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 200;    // the debounce time; increase if the output flicker

String activeStatus = "";
bool menu = 0;
bool isFault = 0;
bool connected = 0;
bool horizontal = 0;
bool fan = 0;
bool buttons = 0;
bool buzzer = 0;
bool useOTA = 0;
bool debug = 0;
bool verboseOutput = 1;
bool disableMenu = 0;
bool profileIsOn = 0;
bool updataAvailable = 0;
bool testState = 0;
bool useSPIFFS = 0 ;

// Button variables
int buttonVal[numDigButtons] = {0};                            // value read from button
int buttonLast[numDigButtons] = {0};                           // buffered value of the button's previous state
long btnDnTime[numDigButtons];                               // time the button was pressed down
long btnUpTime[numDigButtons];                               // time the button was released
boolean ignoreUp[numDigButtons] = {false};                     // whether to ignore the button release because the click+hold was triggered
boolean menuMode[numDigButtons] = {false};                     // whether menu mode has been activated or not
int debounce = 50;
int holdTime = 1000;
int oldTemp = 0;

byte numOfPointers = 0;
byte state = 0; // 0 = boot, 1 = main menu, 2 = select profile, 3 = change profile, 4 = add profile, 5 = settings, 6 = info, 7 = start reflow, 8 = stop reflow, 9 = test outputs
byte previousState = 0;

byte settings_pointer = 0;
byte previousSettingsPointer = 0;
bool   SD_present = false;
//char* json = "";
int profileNum = 0;
#define numOfProfiles 10
String jsonName[numOfProfiles];
char json;
int profileUsed = 0;
char spaceName[] = "profile00";

// Structure for paste profiles
typedef struct {
  char      title[20];         // "Lead 183"
  char      alloy[20];         // "Sn63/Pb37"
  uint16_t  melting_point;     // 183

  uint16_t  temp_range_0;      // 30
  uint16_t  temp_range_1;      // 235

  uint16_t  time_range_0;      // 0
  uint16_t  time_range_1;      // 340

  char      reference[100];    // "https://www.chipquik.com/datasheets/TS391AX50.pdf"

  uint16_t  stages_preheat_0;  // 30
  uint16_t  stages_preheat_1;  // 100

  uint16_t  stages_soak_0;     // 120
  uint16_t  stages_soak_1;     // 150

  uint16_t  stages_reflow_0;   // 150
  uint16_t  stages_reflow_1;   // 183

  uint16_t  stages_cool_0;     // 240
  uint16_t  stages_cool_1;     // 183
} profile_t;

profile_t paste_profile[numOfProfiles]; //declaration of struct type array

#include "reflow_logic.h"

void setup() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  Serial.begin(115200);

  Serial.println(projectName);

  Serial.println("FW version is: " + String(fwVersion) + "_&_" + String(__DATE__) + "_&_" + String(__TIME__));

  preferences.begin("store", false);
  buttons = preferences.getBool("buttons", 0);
  fan = preferences.getBool("fan", 0);
  horizontal = preferences.getBool("horizontal", 0);
  buzzer = preferences.getBool("buzzer", 0);
  useOTA = preferences.getBool("useOTA", 0);
  profileUsed = preferences.getInt("profileUsed", 0);
  useSPIFFS = preferences.getBool("useSPIFFS", 0);
  preferences.end();

  Serial.println();
  Serial.println("Buttons: " + String(buttons));
  Serial.println("Fan is: " + String(fan));
  Serial.println("Horizontal: " + String(horizontal));
  Serial.println("Buzzer: " + String(buzzer));
  Serial.println("OTA: " + String(useOTA));
  Serial.println("Used profile: " + String(profileUsed));
  Serial.println();
  // load profiles from ESP32 memory
  for (int i = 0; i < numOfProfiles; i++) {
    loadProfiles(i);
  }
  display.begin();
  startScreen();

  if ( !SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("Error mounting SPIFFS");
    return;
  }

  // SSR pin initialization to ensure reflow oven is off

  pinMode(ssrPin, OUTPUT);
  digitalWrite(ssrPin, LOW);

  // Buzzer pin initialization to ensure annoying buzzer is off
  digitalWrite(buzzerPin, LOW);
  pinMode(buzzerPin, OUTPUT);

  // LED pins initialization and turn on upon start-up (active low)
  pinMode(ledPin, OUTPUT);

  // Start-up splash
  //digitalWrite(fanPin, LOW);
  pinMode(fanPin, OUTPUT);

  delay(100);

  // Turn off LED (active low)
  digitalWrite(ledPin, ledState);

  // Button initialization
  pinMode(BUTTON_AXIS_Y, INPUT_PULLDOWN);
  pinMode(BUTTON_AXIS_X, INPUT_PULLDOWN);

  for (byte i = 0; i < numDigButtons - 1 ; i++) {
    // Set button input pin
    if (digitalButtonPins[i] > 20  && digitalButtonPins[i] < 40) {
      pinMode(digitalButtonPins[i], INPUT_PULLUP);
      digitalWrite(digitalButtonPins[i], LOW  );
      Serial.println(digitalButtonPins[i]);
    }
  }

  wifiSetup();

  if (WiFi.status() == WL_CONNECTED) { // Wait for the Wi-Fi to connect: scan for Wi-Fi networks, and connect to the strongest of the networks above
    Serial.println("\nConnected to " + WiFi.SSID() + "; IP address: " + WiFi.localIP().toString()); // Report which SSID and IP is in use
    connected = 1;

    if (useOTA != 0) {
      OTA();
    }
  }

  
  max31865.begin(MAX31865_2WIRE);

  // Set window size
  windowSize = 2000;
  // Initialize time keeping variable
  nextCheck = millis();
  // Initialize thermocouple reading variable
  nextRead = millis();

  if (useSPIFFS != 0) {
    profileNum = 0;
    listDir(SPIFFS, "/profiles", 0);
  } else {
    Serial.print(F("Initializing SD card..."));
    if (!SD.begin(SD_CS_pin)) { // see if the card is present and can be initialised. Wemos SD-Card CS uses D8
      Serial.println(F("Card failed or not present, no SD Card data logging possible..."));
      SD_present = false;
    } else {
      Serial.println(F("Card initialised... file access enabled..."));
      SD_present = true;
      // Reset number of profiles for fresh load from SD card
      profileNum = 0;
      listDir(SD, "/profiles", 0);
    }
  }
  // Load data from selected storage
  if ((SD_present == true) || (useSPIFFS != 0)) {
    profile_t paste_profile_load[numOfProfiles];
    // Scan all profiles from source

    for (int i = 0; i < profileNum; i++) {
      if (useSPIFFS != 0) {
        parseJsonProfile(SPIFFS, jsonName[i], i, paste_profile_load);
      } else {
        parseJsonProfile(SD, jsonName[i], i, paste_profile_load);
      }
    }
    //Compare profiles, if they are already in memory
    for (int i = 0; i < profileNum; i++) {
      compareProfiles(paste_profile_load[i], paste_profile[i], i);
    }
  }

  Serial.println();
  Serial.print("Number of profiles: ");
  Serial.println(profileNum);

  Serial.println("Titles and alloys: ");
  for (int i = 0; i < profileNum; i++) {
    Serial.print((String)i + ". ");
    Serial.print(paste_profile[i].title);
    Serial.print(", ");
    Serial.println(paste_profile[i].alloy);
  }
  Serial.println();
}

void updatePreferences() {
  preferences.begin("store", false);
  preferences.putBool("buttons", buttons);
  preferences.putBool("fan", fan);
  preferences.putBool("horizontal", horizontal);
  preferences.putBool("buzzer", buzzer);
  preferences.putBool("useOTA", useOTA);
  preferences.putBool("useSPIFFS", useSPIFFS);
  preferences.end();

  if (verboseOutput != 0) {
    Serial.println();
    Serial.println("Buttons is: " + String(buttons));
    Serial.println("Fan is: " + String(fan));
    Serial.println("Horizontal is: " + String(horizontal));
    Serial.println("OTA is : " + String(useOTA));
    Serial.println("Use SPIFFS is : " + String(useSPIFFS));
    Serial.println("Buzzer is: " + String(buzzer));
    Serial.println();
  }
}

void processButtons() {
  for (int i = 0; i < numDigButtons; i++) {
    digitalButton(digitalButtonPins[i]);
  }
  readAnalogButtons();
}

void loop() {
  wm.process();
  if (state != 9) { // if we are in test menu, disable LED & SSR control in loop
    reflow_main();
  }
  processButtons();
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  String tempFileName;
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      tempFileName = file.name();
      if (tempFileName.endsWith("json")) {
        Serial.println("Find this JSON file: "  + tempFileName);
        jsonName[profileNum] = tempFileName;
        profileNum++;
      }
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS & fs, String path, const char * type) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void wifiSetup() {
  wm.setConfigPortalBlocking(false);
  if (wm.autoConnect("ReflowOvenAP")) {
    Serial.println("connected...yeey :)");
  }
  else {
    Serial.println("Configportal running");
  }
}
