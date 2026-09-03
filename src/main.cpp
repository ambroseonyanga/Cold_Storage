#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
// ---- Pin definitions ----
#define I2C_SDA 8
#define I2C_SCL 9
#define LCD_ADDR 0x27
#define LCD_COLS 20
#define LCD_ROWS 4
#define DS18B20_PIN 16
#define TEC_PIN 41
#define FAN_HOT_PIN 40
#define FAN_COLD_PIN 39
#define BUZZER_PIN 47
#define LED_GREEN 18
#define LED_BLUE 17
#define MENU_BUTTON 5
#define UP_BUTTON 6
#define DOWN_BUTTON 7
#define BACK_BUTTON 15
#define BATTERY_PIN 1

// ---- SD card pins ----
#define SD_CS 4
#define SD_SCK 12
#define SD_MOSI 11
#define SD_MISO 13

// ---- Configuration ----
#define TEC_ACTIVE_LOW false

// ============================================================
// BLE CONFIGURATION
// ============================================================

#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"

#define CHARACTERISTIC_UUID "abcd1234-5678-5678-5678-abcdef123456"

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;

bool deviceConnected = false;
unsigned long lastBLEUpdate = 0;

const unsigned long BLE_INTERVAL = 1000;

// ============================================================
// BLE CONNECTION CALLBACKS
// ============================================================

// class MyServerCallbacks : public BLEServerCallbacks
// {

//   void onConnect(BLEServer *pServer)
//   {
//     deviceConnected = true;
//     Serial.println("Phone connected via BLE");
//   }

//   void onDisconnect(BLEServer *pServer)
//   {
//     deviceConnected = false;
//     Serial.println("Phone disconnected");

//     BLEDevice::startAdvertising();
//   }
// };

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;

    Serial.println();
    Serial.println("================================");
    Serial.println("PHONE CONNECTED VIA BLE");
    Serial.println("================================");
  }

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;

    Serial.println();
    Serial.println("================================");
    Serial.println("PHONE DISCONNECTED");
    Serial.println("Restarting BLE advertising...");
    Serial.println("================================");

    delay(500);

    BLEDevice::startAdvertising();
  }
};

// ---- Constants ----
const float BATTERY_CUTOFF = 9.6;
const float DIVIDER_RATIO = 11.0;
const float BATTERY_CALIBRATION = 1.14;
const float HYSTERESIS = 0.5;
const int BATTERY_SAMPLES = 8;
const unsigned long LOG_INTERVAL = 5000; // log every 5 seconds

// ---- Objects ----
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
RTC_DS3231 rtc;
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
Preferences preferences;

// ---- Modes ----
enum HomeMode
{
  MODE_OFF,
  MODE_AUTO,
  MODE_ON
};
HomeMode homeMode = MODE_AUTO;

// ---- State ----
float setTemp = 25.0;
float currentTemp = 0.0;
float batteryVoltage = 0.0;
bool tecState = false;
bool tecAllowed = true;
unsigned long lastHeartbeat = 0, lastBlueBlink = 0;
bool heartbeatState = false, blueState = false;

// ---- Screens ----
enum Screen
{
  SCREEN_HOME,
  SCREEN_MENU,
  SCREEN_SET_TEMP,
  SCREEN_TEST_TEC,
  SCREEN_RUN
};
Screen currentScreen = SCREEN_HOME;
int menuIndex = 0;

// ---- LCD buffer ----
char lastLCD[LCD_ROWS][LCD_COLS + 1] = {0};
int lastLen[LCD_ROWS] = {0};

bool blinkState = false;
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 150;

bool sdOk = false, rtcOk = false, tecTestOk = false, batteryOk = false;

float batteryReadings[BATTERY_SAMPLES] = {0};
int batteryIndex = 0;

// ---- Non‑blocking temperature ----
unsigned long lastTempRequest = 0;
bool tempRequestPending = false;

// ---- Data logging ----
unsigned long startMillis = 0;
int logFileNumber = 0;
unsigned long lastLogTime = 0;
char logFileName[20];

// ---- Helper functions ----
float readBatteryVoltage()
{
  int raw = analogRead(BATTERY_PIN);
  float voltage = (raw / 4095.0) * 3.3 * DIVIDER_RATIO * BATTERY_CALIBRATION;
  batteryReadings[batteryIndex] = voltage;
  batteryIndex = (batteryIndex + 1) % BATTERY_SAMPLES;
  float sum = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++)
    sum += batteryReadings[i];
  return sum / BATTERY_SAMPLES;
}

float readBatteryOnce()
{
  int raw = analogRead(BATTERY_PIN);
  return (raw / 4095.0) * 3.3 * DIVIDER_RATIO * BATTERY_CALIBRATION;
}

float readTemperature()
{
  if (!tempRequestPending)
  {
    sensors.requestTemperatures();
    tempRequestPending = true;
    lastTempRequest = millis();
    return currentTemp;
  }
  else
  {
    if (millis() - lastTempRequest > 80)
    {
      float temp = sensors.getTempCByIndex(0);
      tempRequestPending = false;
      if (temp == DEVICE_DISCONNECTED_C)
        return -999.0;
      else
        return temp;
    }
    return currentTemp;
  }
}

void updateTEC()
{
  if (batteryVoltage < BATTERY_CUTOFF && batteryVoltage > 0.1)
  {
    digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? HIGH : LOW);
    tecState = false;
    tecAllowed = false;
    return;
  }
  tecAllowed = true;

  bool desired = false;
  switch (homeMode)
  {
  case MODE_OFF:
    desired = false;
    break;
  case MODE_ON:
    desired = true;
    break;
  case MODE_AUTO:
    if (currentTemp > (setTemp + HYSTERESIS))
      desired = true;
    else
      desired = false;
    break;
  }

  if (desired)
  {
    digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? LOW : HIGH);
    tecState = true;
  }
  else
  {
    digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? HIGH : LOW);
    tecState = false;
  }
}

// ---- Preferences ----
void loadPrefs()
{
  preferences.begin("tec_config", false);
  setTemp = preferences.getFloat("setTemp", 25.0);
  int storedMode = preferences.getInt("homeMode", MODE_AUTO);
  homeMode = (HomeMode)storedMode;
  preferences.end();
}
void savePrefs()
{
  preferences.begin("tec_config", false);
  preferences.putFloat("setTemp", setTemp);
  preferences.putInt("homeMode", (int)homeMode);
  preferences.end();
}

void beep(int d = 10)
{
  digitalWrite(BUZZER_PIN, LOW);
  delay(d);
  digitalWrite(BUZZER_PIN, HIGH);
}

// ---- Data logging ----
String formatUptime(unsigned long seconds)
{
  int hrs = seconds / 3600;
  int mins = (seconds % 3600) / 60;
  int secs = seconds % 60;
  char buf[10];
  sprintf(buf, "%02d:%02d:%02d", hrs, mins, secs);
  return String(buf);
}

// void findNextLogFile()
// {
//   int maxNum = 0;
//   File root = SD.open("/");
//   while (true)
//   {
//     File entry = root.openNextFile();
//     if (!entry)
//       break;
//     if (!entry.isDirectory())
//     {
//       String name = entry.name();
//       if (name.startsWith("Log_") && name.endsWith(".csv"))
//       {
//         String numStr = name.substring(4, name.length() - 4);
//         int num = numStr.toInt();
//         if (num > maxNum)
//           maxNum = num;
//       }
//     }
//     entry.close();
//   }
//   root.close();
//   logFileNumber = maxNum + 1;
//   Serial.print("Next log file number: ");
//   Serial.println(logFileNumber);
// }

void findNextLogFile()
{
  int maxNum = 0;

  File root = SD.open("/");

  while (true)
  {
    File entry = root.openNextFile();

    if (!entry)
      break;

    if (!entry.isDirectory())
    {
      String name = entry.name();

      // Remove leading slash
      if (name.startsWith("/"))
      {
        name.remove(0, 1);
      }

      if (name.startsWith("Log_") && name.endsWith(".csv"))
      {
        String numStr = name.substring(4, name.length() - 4);

        int num = numStr.toInt();

        if (num > maxNum)
        {
          maxNum = num;
        }
      }
    }

    entry.close();
  }

  root.close();

  logFileNumber = maxNum + 1;

  Serial.print("Next log file number: ");
  Serial.println(logFileNumber);
}
// ---- Create file with header ----
void createLogFile()
{
  // sprintf(logFileName, "Log_%03d.csv", logFileNumber);
  sprintf(logFileName, "/Log_%03d.csv", logFileNumber);
  File logFile = SD.open(logFileName, FILE_WRITE);
  if (logFile)
  {
    logFile.println("Uptime,Set_Temp,Current_Temp,Battery_V,TEC_Status");
    logFile.flush();
    logFile.close();
    Serial.print("Log file created: ");
    Serial.println(logFileName);
    // Verify existence
    if (SD.exists(logFileName))
    {
      Serial.println("File exists on SD card.");
      lcd.setCursor(0, 2);
      lcd.print("Log file created");
      delay(500);
      lcd.setCursor(0, 2);
      lcd.print("                ");
    }
    else
    {
      Serial.println("ERROR: File not found after creation!");
      lcd.setCursor(0, 2);
      lcd.print("File creation fail");
      delay(500);
      lcd.setCursor(0, 2);
      lcd.print("                ");
    }
  }
  else
  {
    Serial.println("Failed to create log file");
    lcd.setCursor(0, 2);
    lcd.print("SD write error");
    delay(500);
    lcd.setCursor(0, 2);
    lcd.print("                ");
  }
}

// ---- Append a data line ----
void logData()
{
  if (!sdOk)
  {
    Serial.println("Cannot log: SD not OK");
    return;
  }

  // Open in append mode, write, flush, close
  File logFile = SD.open(logFileName, FILE_WRITE);
  if (!logFile)
  {
    Serial.println("Failed to open log file for append");
    return;
  }

  unsigned long seconds = (millis() - startMillis) / 1000;
  String uptime = formatUptime(seconds);
  String tecStatus = tecState ? "ON" : "OFF";

  logFile.print(uptime);
  logFile.print(",");
  logFile.print(setTemp, 1);
  logFile.print(",");
  logFile.print(currentTemp, 1);
  logFile.print(",");
  logFile.print(batteryVoltage, 2);
  logFile.print(",");
  logFile.println(tecStatus);

  logFile.flush();
  logFile.close();

  Serial.println("Logged one line");
}

// ---- LCD Update ----
void updateLCD()
{
  char newLCD[LCD_ROWS][LCD_COLS + 1];
  int newLen[LCD_ROWS] = {0};

  switch (currentScreen)
  {
  case SCREEN_HOME:
  {
    char buf[LCD_COLS + 1];
    if (currentTemp > -100)
      snprintf(buf, LCD_COLS + 1, "Set:%4.1fC Cur:%4.1fC", setTemp, currentTemp);
    else
      snprintf(buf, LCD_COLS + 1, "Set:%4.1fC Cur:ERR", setTemp);
    strcpy(newLCD[0], buf);
    newLen[0] = strlen(buf);

    const char *tecStr = !tecAllowed ? "OFF-LOW" : (tecState ? "ON " : "OFF");
    snprintf(buf, LCD_COLS + 1, "TEC:%s   Bat:%5.2fV", tecStr, batteryVoltage);
    strcpy(newLCD[1], buf);
    newLen[1] = strlen(buf);

    newLCD[2][0] = '\0';
    newLen[2] = 0;
    strcpy(newLCD[3], "     RUN   STOP     ");
    newLen[3] = 20;
    break;
  }

  case SCREEN_MENU:
  {
    strcpy(newLCD[0], "MENU");
    newLen[0] = 4;
    newLCD[1][0] = '\0';
    newLen[1] = 0;
    const char *items[] = {"RUN", "SET"};
    for (int i = 0; i < 2; i++)
    {
      char line[LCD_COLS + 1];
      if (i == menuIndex)
        snprintf(line, LCD_COLS + 1, ">%s", items[i]);
      else
        snprintf(line, LCD_COLS + 1, " %s", items[i]);
      strcpy(newLCD[i + 2], line);
      newLen[i + 2] = strlen(line);
    }
    break;
  }

  case SCREEN_SET_TEMP:
  {
    strcpy(newLCD[0], "SET TEMP");
    newLen[0] = 8;
    newLCD[1][0] = '\0';
    newLen[1] = 0;
    char setLine[LCD_COLS + 1];
    char numBuf[6];
    if (blinkState)
    {
      snprintf(numBuf, sizeof(numBuf), "%4.1f", setTemp);
    }
    else
    {
      strcpy(numBuf, "     ");
    }
    snprintf(setLine, LCD_COLS + 1, "Set: %s C", numBuf);
    strcpy(newLCD[2], setLine);
    newLen[2] = strlen(setLine);
    char curBuf[LCD_COLS + 1];
    if (currentTemp > -100)
      snprintf(curBuf, LCD_COLS + 1, "Cur: %4.1f C", currentTemp);
    else
      snprintf(curBuf, LCD_COLS + 1, "Cur: ERR");
    strcpy(newLCD[3], curBuf);
    newLen[3] = strlen(curBuf);
    break;
  }

  case SCREEN_RUN:
  {
    strcpy(newLCD[0], "RUNNING");
    newLen[0] = 7;
    char buf[LCD_COLS + 1];
    snprintf(buf, LCD_COLS + 1, "Set:%4.1fC Cur:%4.1fC", setTemp, currentTemp);
    strcpy(newLCD[1], buf);
    newLen[1] = strlen(buf);
    const char *tecStr = !tecAllowed ? "OFF-LOW" : (tecState ? "ON " : "OFF");
    snprintf(buf, LCD_COLS + 1, "TEC:%s Bat:%5.2fV", tecStr, batteryVoltage);
    strcpy(newLCD[2], buf);
    newLen[2] = strlen(buf);
    strcpy(newLCD[3], "STOP");
    newLen[3] = 4;
    break;
  }

  case SCREEN_TEST_TEC:
  {
    strcpy(newLCD[0], "TEST TEC");
    newLen[0] = 8;
    newLCD[1][0] = '\0';
    newLen[1] = 0;
    newLCD[2][0] = '\0';
    newLen[2] = 0;
    char buf[LCD_COLS + 1];
    if (blinkState)
    {
      snprintf(buf, LCD_COLS + 1, "TEC: %s", tecState ? "ON" : "OFF");
    }
    else
    {
      snprintf(buf, LCD_COLS + 1, "TEC:   ");
    }
    strcpy(newLCD[3], buf);
    newLen[3] = strlen(buf);
    break;
  }
  }

  for (int r = 0; r < LCD_ROWS; r++)
  {
    int maxLen = max(newLen[r], lastLen[r]);
    for (int c = 0; c < maxLen; c++)
    {
      char newChar = (c < newLen[r]) ? newLCD[r][c] : ' ';
      char lastChar = (c < lastLen[r]) ? lastLCD[r][c] : ' ';
      if (newChar != lastChar)
      {
        lcd.setCursor(c, r);
        lcd.print(newChar);
        lastLCD[r][c] = newChar;
      }
    }
    lastLen[r] = newLen[r];
    if (newLen[r] < LCD_COLS)
      lastLCD[r][newLen[r]] = '\0';
  }
}

// ============================================================
//  BOOT SCREEN & POST
// ============================================================
void showPostScreen(const char *msg, bool ok)
{
  lcd.setCursor(0, 3);
  lcd.print("                ");
  lcd.setCursor(0, 3);
  lcd.print(msg);
  lcd.print(ok ? " OK" : " FAIL");
  delay(400);
}

void bootSequence()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("COLD STORAGE");
  lcd.setCursor(0, 1);
  lcd.print("STARTING...");
  lcd.setCursor(0, 2);
  lcd.print("                ");
  lcd.setCursor(0, 3);
  lcd.print("                ");

  // Flash LEDs fast for 3 seconds
  unsigned long start = millis();
  bool ledState = false;
  while (millis() - start < 3000)
  {
    ledState = !ledState;
    digitalWrite(LED_GREEN, ledState);
    digitalWrite(LED_BLUE, ledState);
    delay(100);
  }
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);

  // ---- POST ----
  lcd.setCursor(0, 3);
  lcd.print("                ");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS, SPI);
  showPostScreen("SD Card", sdOk);
  if (sdOk)
  {
    Serial.println("SD mounted successfully.");
  }

  rtcOk = rtc.begin();
  if (rtcOk && rtc.lostPower())
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  showPostScreen("RTC", rtcOk);

  digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? LOW : HIGH);
  delay(100);
  digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? HIGH : LOW);
  tecTestOk = true;
  showPostScreen("TEC Relay", tecTestOk);

  batteryVoltage = readBatteryOnce();
  batteryOk = (batteryVoltage > 0.1 && batteryVoltage >= BATTERY_CUTOFF);
  if (!batteryOk)
    beep(200);
  showPostScreen("Battery", batteryOk);

  sensors.begin();
  bool dsOk = (sensors.getDeviceCount() > 0);
  showPostScreen("DS18B20", dsOk);

  delay(300);
}

// void setupBLE()
// {

//   Serial.println("Starting BLE...");

//   BLEDevice::init("ColdStorage-ESP32");

//   pServer = BLEDevice::createServer();

//   pServer->setCallbacks(new MyServerCallbacks());

//   BLEService *pService =
//       pServer->createService(SERVICE_UUID);

//   pCharacteristic =
//       pService->createCharacteristic(

//           CHARACTERISTIC_UUID,

//           BLECharacteristic::PROPERTY_READ |
//               BLECharacteristic::PROPERTY_NOTIFY

//       );

//   pCharacteristic->addDescriptor(
//       new BLE2902());

//   pCharacteristic->setValue("Cold Storage Ready");

//   pService->start();

//   BLEAdvertising *pAdvertising =
//       BLEDevice::getAdvertising();

//   pAdvertising->addServiceUUID(SERVICE_UUID);

//   pAdvertising->setScanResponse(true);

//   BLEDevice::startAdvertising();

//   Serial.println("BLE Advertising Started");
// }

void setupBLE()
{
  Serial.println();
  Serial.println("================================");
  Serial.println("Starting BLE...");
  Serial.println("================================");

  BLEDevice::init("ColdStorage-ESP32");

  pServer = BLEDevice::createServer();

  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService =
      pServer->createService(SERVICE_UUID);

  pCharacteristic =
      pService->createCharacteristic(
          CHARACTERISTIC_UUID,
          BLECharacteristic::PROPERTY_READ |
              BLECharacteristic::PROPERTY_NOTIFY);

  // Required descriptor for notifications
  pCharacteristic->addDescriptor(
      new BLE2902());

  // Initial value
  pCharacteristic->setValue("READY");

  // Start service
  pService->start();

  // Configure advertising
  BLEAdvertising *pAdvertising =
      BLEDevice::getAdvertising();

  pAdvertising->addServiceUUID(SERVICE_UUID);

  pAdvertising->setScanResponse(true);

  // Start advertising
  BLEDevice::startAdvertising();

  Serial.println("BLE SERVICE STARTED");
  Serial.print("Device Name: ");
  Serial.println("ColdStorage-ESP32");

  Serial.print("Service UUID: ");
  Serial.println(SERVICE_UUID);

  Serial.print("Characteristic UUID: ");
  Serial.println(CHARACTERISTIC_UUID);

  Serial.println("BLE Advertising Started");
  Serial.println("================================");
}

void sendBLEData()
{

  if (!deviceConnected)
    return;

  String tecStatus =
      tecState ? "ON" : "OFF";

  String data = "{";

  data += "\"temperature\":";
  data += String(currentTemp, 2);

  data += ",";

  data += "\"setTemp\":";
  data += String(setTemp, 2);

  data += ",";

  data += "\"battery\":";
  data += String(batteryVoltage, 2);

  data += ",";

  data += "\"tec\":\"";
  data += tecStatus;
  data += "\"";

  data += "}";

  pCharacteristic->setValue(
      data.c_str());

  pCharacteristic->notify();

  Serial.print("BLE Sent: ");

  Serial.println(data);
}

// ============================================================
//  SETUP
// ============================================================
void setup()
{
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("ESP32 STARTED");

  Serial.println("=== TEC Controller ===");

  loadPrefs();

  pinMode(TEC_PIN, OUTPUT);
  pinMode(FAN_HOT_PIN, OUTPUT);
  pinMode(FAN_COLD_PIN, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BATTERY_PIN, INPUT);

  pinMode(MENU_BUTTON, INPUT_PULLDOWN);
  pinMode(UP_BUTTON, INPUT_PULLDOWN);
  pinMode(DOWN_BUTTON, INPUT_PULLDOWN);
  pinMode(BACK_BUTTON, INPUT_PULLDOWN);

  digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(FAN_HOT_PIN, LOW);
  digitalWrite(FAN_COLD_PIN, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(BUZZER_PIN, HIGH);

  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();

  // ---- BOOT ----
  bootSequence();

  // ---- Init sensors & logging ----
  for (int i = 0; i < BATTERY_SAMPLES; i++)
    batteryReadings[i] = batteryVoltage;
  batteryIndex = 0;

  sensors.requestTemperatures();
  delay(100);
  currentTemp = sensors.getTempCByIndex(0);
  if (currentTemp == DEVICE_DISCONNECTED_C)
    currentTemp = -999.0;
  tempRequestPending = false;

  startMillis = millis();
  if (sdOk)
  {
    findNextLogFile();
    createLogFile(); // creates file, writes header, verifies
  }
  else
  {
    Serial.println("SD card not mounted – logging disabled");
  }
  lastLogTime = millis();

  // ---- Clear LCD and show home ----
  lcd.clear();
  for (int r = 0; r < LCD_ROWS; r++)
  {
    lastLCD[r][0] = '\0';
    lastLen[r] = 0;
  }
  updateLCD();

  setupBLE();

  beep(100);
}

// ============================================================
//  LOOP
// ============================================================
void loop()
{
  // ---- BUTTONS (READ FIRST) ----
  bool menu = digitalRead(MENU_BUTTON);
  bool up = digitalRead(UP_BUTTON);
  bool down = digitalRead(DOWN_BUTTON);
  bool back = digitalRead(BACK_BUTTON);

  static bool lastMenu = 0, lastUp = 0, lastDown = 0, lastBack = 0;
  static unsigned long lastDebounce = 0;
  const unsigned long DEBOUNCE_MS = 5;

  // MENU
  if (menu != lastMenu)
  {
    if (millis() - lastDebounce > DEBOUNCE_MS)
    {
      lastDebounce = millis();
      if (menu == HIGH && lastMenu == LOW)
      {
        beep(10);
      }
      else if (menu == LOW && lastMenu == HIGH)
      {
        if (currentScreen == SCREEN_HOME)
        {
          currentScreen = SCREEN_MENU;
          menuIndex = 0;
        }
        else if (currentScreen == SCREEN_MENU)
        {
          if (menuIndex == 0)
          {
            homeMode = MODE_AUTO;
            savePrefs();
            currentScreen = SCREEN_RUN;
          }
          else if (menuIndex == 1)
          {
            currentScreen = SCREEN_SET_TEMP;
          }
        }
      }
      lastMenu = menu;
    }
  }

  // UP
  if (up != lastUp)
  {
    if (millis() - lastDebounce > DEBOUNCE_MS)
    {
      lastDebounce = millis();
      if (up == HIGH && lastUp == LOW)
      {
        beep(10);
      }
      else if (up == LOW && lastUp == HIGH)
      {
        if (currentScreen == SCREEN_HOME)
        {
          homeMode = MODE_AUTO;
          savePrefs();
        }
        else if (currentScreen == SCREEN_MENU)
        {
          menuIndex = (menuIndex - 1 + 2) % 2;
        }
        else if (currentScreen == SCREEN_SET_TEMP || currentScreen == SCREEN_RUN)
        {
          setTemp += 0.5;
          if (setTemp > 50.0)
            setTemp = 50.0;
          savePrefs();
        }
        else if (currentScreen == SCREEN_TEST_TEC)
        {
          if (tecAllowed)
          {
            if (tecState)
            {
              digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? HIGH : LOW);
              tecState = false;
            }
            else
            {
              digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? LOW : HIGH);
              tecState = true;
            }
          }
        }
      }
      lastUp = up;
    }
  }

  // DOWN
  if (down != lastDown)
  {
    if (millis() - lastDebounce > DEBOUNCE_MS)
    {
      lastDebounce = millis();
      if (down == HIGH && lastDown == LOW)
      {
        beep(10);
      }
      else if (down == LOW && lastDown == HIGH)
      {
        if (currentScreen == SCREEN_HOME)
        {
          homeMode = MODE_OFF;
          savePrefs();
        }
        else if (currentScreen == SCREEN_MENU)
        {
          menuIndex = (menuIndex + 1) % 2;
        }
        else if (currentScreen == SCREEN_SET_TEMP || currentScreen == SCREEN_RUN)
        {
          setTemp -= 0.5;
          if (setTemp < -10.0)
            setTemp = -10.0;
          savePrefs();
        }
        else if (currentScreen == SCREEN_TEST_TEC)
        {
          if (tecAllowed)
          {
            if (tecState)
            {
              digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? HIGH : LOW);
              tecState = false;
            }
            else
            {
              digitalWrite(TEC_PIN, TEC_ACTIVE_LOW ? LOW : HIGH);
              tecState = true;
            }
          }
        }
      }
      lastDown = down;
    }
  }

  // BACK
  if (back != lastBack)
  {
    if (millis() - lastDebounce > DEBOUNCE_MS)
    {
      lastDebounce = millis();
      if (back == HIGH && lastBack == LOW)
      {
        beep(10);
      }
      else if (back == LOW && lastBack == HIGH)
      {
        if (currentScreen == SCREEN_HOME)
        {
          // nothing
        }
        else if (currentScreen == SCREEN_MENU)
        {
          currentScreen = SCREEN_HOME;
        }
        else if (currentScreen == SCREEN_SET_TEMP)
        {
          savePrefs();
          currentScreen = SCREEN_MENU;
        }
        else if (currentScreen == SCREEN_TEST_TEC)
        {
          currentScreen = SCREEN_MENU;
        }
        else if (currentScreen == SCREEN_RUN)
        {
          homeMode = MODE_OFF;
          savePrefs();
          currentScreen = SCREEN_MENU;
        }
      }
      lastBack = back;
    }
  }

  // ---- Non‑blocking temperature ----
  if (!tempRequestPending)
  {
    sensors.requestTemperatures();
    tempRequestPending = true;
    lastTempRequest = millis();
  }
  else
  {
    if (millis() - lastTempRequest > 80)
    {
      float temp = sensors.getTempCByIndex(0);
      tempRequestPending = false;
      if (temp != DEVICE_DISCONNECTED_C)
      {
        currentTemp = temp;
      }
    }
  }

  // ---- Battery ----
  batteryVoltage = readBatteryVoltage();

  // ---- TEC ----
  updateTEC();

  // ---- Fans ----
  digitalWrite(FAN_HOT_PIN, (tecState && tecAllowed) ? HIGH : LOW);
  digitalWrite(FAN_COLD_PIN, (tecState && tecAllowed) ? HIGH : LOW);

  // ---- LEDs ----
  if (millis() - lastHeartbeat >= 1000)
  {
    lastHeartbeat = millis();
    heartbeatState = !heartbeatState;
    digitalWrite(LED_GREEN, heartbeatState);
  }
  if (tecState && tecAllowed)
  {
    if (millis() - lastBlueBlink >= 500)
    {
      lastBlueBlink = millis();
      blueState = !blueState;
      digitalWrite(LED_BLUE, blueState);
    }
  }
  else
  {
    digitalWrite(LED_BLUE, LOW);
    blueState = false;
  }

  // ---- Blink ----
  if (currentScreen == SCREEN_SET_TEMP || currentScreen == SCREEN_TEST_TEC)
  {
    if (millis() - lastBlinkTime >= BLINK_INTERVAL)
    {
      lastBlinkTime = millis();
      blinkState = !blinkState;
    }
  }
  else
  {
    blinkState = true;
  }

  // ---- Data logging ----
  bool isRunning = (homeMode == MODE_AUTO || homeMode == MODE_ON);
  if (isRunning && sdOk && (millis() - lastLogTime >= LOG_INTERVAL))
  {
    lastLogTime = millis();
    logData();
  }

  // ---- LCD refresh ----
  static unsigned long lastLCD = 0;
  if (millis() - lastLCD >= 250)
  {
    lastLCD = millis();
    updateLCD();
  }

  // ============================================================
  // SEND DATA TO PHONE
  // ============================================================

  if (millis() - lastBLEUpdate >= BLE_INTERVAL)
  {

    lastBLEUpdate = millis();

    sendBLEData();
  }
}