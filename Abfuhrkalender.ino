/**
  Date: February 2025
  Hardware: ESP8266 or ESP32
  Description:
  Gets the trash calendar as .ics over HTTPS get (no streaming) and lights RGB LEDs 
  in the colour of the garbage can that gets picked up the next day.
  The time is synced over ntp.
  
  External librarie used:
  - Adafruit_NeoPixel
 */
const bool extraDebugPrint = true;
const bool replaceYearStringInUrl = true;

#include "WifiSecret.h"
#include "CalendarURL.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <Adafruit_NeoPixel.h>
struct tm; // As required by POSIX.1-2008, declare tm as incomplete type. The actual definition is in time.h.

const int neoPixelPin = 5;
const int numOfNeoPixels = 3;
Adafruit_NeoPixel pixels(numOfNeoPixels, neoPixelPin, NEO_GRB + NEO_KHZ800);

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

enum TrashType {
  RESIDUAL,
  BIO,
  PLASTIC,
  PAPER,
  CHRISTMAS_TREE,
  COUNT
};

const uint8_t TrashTypeRed[TrashType::COUNT] = { 150, 150, 200, 1, 0 };
const uint8_t TrashTypeGreen[TrashType::COUNT] = { 150, 20, 200, 1, 255 };
const uint8_t TrashTypeBlue[TrashType::COUNT] = { 140, 000, 000, 255, 0 };

// Strings in the .ics file to trigger while parsing the file for dates.
const String eventTrigger = "DTSTART;VALUE=DATE:";
const String eventTypeTrigger = "SUMMARY:";
const String TrashTypeStrings[TrashType::COUNT] = { "Restabfall", "Bioabfall", "Wertstoff", "Papiertonne", "Tannenbaum" };

struct Event {
  TrashType type;
  uint8_t year;
  uint8_t month;
  uint8_t day;
};
const int maxNumberOfEvents = 100;
Event events[maxNumberOfEvents];
int numberOfEvents = 0;

enum TodaysEventDay {
  TODAY,
  TOMORROW
};

struct TodaysEvent {
  TrashType type;
  TodaysEventDay day;
};

TodaysEvent todaysEvents[TrashType::COUNT];
int numberOfTodaysEvents = 0;

void setup();
void loop();
bool updateIcs();
bool handleFailure();
void startWifi();
void stopWifi();
void printTimeInfo(tm* timeinfo);
time_t myTimegm(tm* t);
bool isEventOnDate(Event& event, tm* timeinfo);
bool updateLeds(bool lastEventUpdateSuccessful);
void parseIcsLine(const String& line);
void replaceYearInUrl(char* url);
bool getIcs();

void setup() {
  Serial.begin(115200);
  pixels.begin();
  pixels.clear();
  pixels.setPixelColor(0, pixels.Color(255, 0, 0));
  pixels.show();

  delay(4000);  // Let Arduino IDE Serial Monitor notice a reset after upload.
  Serial.println();
  Serial.println();
  Serial.println();
  startWifi();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
  static bool firstRun = true;
  static bool success = false;

  if (firstRun) {
    Serial.println("[updateIcs] first run");
    firstRun = false;
    success = updateIcs();
  }

  bool newMonth = updateLeds(success);
  if (newMonth) {
    Serial.println("[updateIcs] new month");
    success = updateIcs();
  } else {
    delay(59000);
    if (!success) {
      Serial.println("[updateIcs] retry");
      success = updateIcs();
    }
  }
}

bool updateIcs() {
  bool lastEventUpdateSuccessful = true;
  bool success = getIcs();
  while (!success) {
    lastEventUpdateSuccessful = false;
    bool nonCriticalFailure = handleFailure();
    if (nonCriticalFailure) {
      break;
    }
    lastEventUpdateSuccessful = true;
    success = getIcs();
  }
  return lastEventUpdateSuccessful;
}

bool handleFailure() {
  if (0 < numberOfEvents) {
    return true;
  }

  // Try again in 10 minutes. Blink red meanwhile.
  const int waitSeconds = 600;
  bool toggle = true;
  for (int i = 0; i < waitSeconds; i++) {
    toggle = !toggle;
    for (int p = 0; p < numOfNeoPixels; p++) {
      pixels.setPixelColor(p, pixels.Color(255 * static_cast<int>(toggle), 0, 0));
    }
    pixels.show();
    Serial.print("[Failure] waiting for retry: ");
    Serial.print(i);
    Serial.print("/");
    Serial.print(waitSeconds);
    Serial.println("s");
    delay(1000);
  }
  return false;
}

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PSWD);
  Serial.printf("[Wifi] Connecting to %s\n", SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
}

void stopWifi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  Serial.println("[Wifi] off");
  delay(100);
}

void printTimeInfo(tm* timeinfo) {
  Serial.print("Year: ");
  Serial.print(timeinfo->tm_year + 1900);
  Serial.print(" Month: ");
  Serial.print(timeinfo->tm_mon + 1);
  Serial.print(" Day: ");
  Serial.println(timeinfo->tm_mday);

  Serial.print("Day of week: ");
  Serial.print(timeinfo->tm_wday);
  char timeWeekDay[10];
  strftime(timeWeekDay, 10, "%A", timeinfo);
  Serial.print(" ");
  Serial.println(timeWeekDay);

  Serial.print("Time: ");
  Serial.print(timeinfo->tm_hour);
  Serial.print(":");
  Serial.print(timeinfo->tm_min);
  Serial.print(":");
  Serial.println(timeinfo->tm_sec);
}

time_t myTimegm(tm* t) {
  char* old_tz = getenv("TZ");  // Save the old timezone
  setenv("TZ", "UTC", 1);
  tzset();
  time_t timestamp = mktime(t);
  if (old_tz)
    setenv("TZ", old_tz, 1);  // Restore old timezone
  else
    unsetenv("TZ");
  tzset();
  return timestamp;
}

bool isEventOnDate(Event& event, tm* timeinfo) {
  if(extraDebugPrint) {
    Serial.println(event.type);
    Serial.println(event.year + 100);
    Serial.println(timeinfo->tm_year);

    Serial.println(event.month);
    Serial.println(timeinfo->tm_mon + 1);

    Serial.println(event.day);
    Serial.println(timeinfo->tm_mday);
  }

  return event.year + 100 == timeinfo->tm_year && event.month == timeinfo->tm_mon + 1 && event.day == timeinfo->tm_mday;
}

bool updateLeds(bool lastEventUpdateSuccessful) {
  Serial.println("This Day:");
  tm localTime;
  if (!getLocalTime(&localTime)) {
    Serial.println("Failed to obtain time");
    return false;
  }

  const int addDebugDaysToToday = 0;
  time_t hackTimestampThisDay = myTimegm(&localTime) + addDebugDaysToToday * 3600 * 24;
  tm* timeinfo = localtime(&hackTimestampThisDay);
  printTimeInfo(timeinfo);

  static int lastMonth = timeinfo->tm_mon;
  static int lastDay = timeinfo->tm_mday;
  static bool firstRun = true;

  if (!firstRun && lastDay == timeinfo->tm_mday) {
    return false;
  }

  Serial.println("Next Day:");
  time_t timestampNextDay = myTimegm(timeinfo) + 3600 * 24;
  struct tm* timeinfoNextDay = localtime(&timestampNextDay);
  printTimeInfo(timeinfoNextDay);

  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  bool nextDay = false;
  bool thisDay = false;

  numberOfTodaysEvents = 0;

  for (int i = 0; i < numberOfEvents; i++) {
    if(extraDebugPrint) {
      Serial.print("i: ");
      Serial.println(i);
    }
    thisDay = isEventOnDate(events[i], timeinfo);
    nextDay = isEventOnDate(events[i], timeinfoNextDay);

    if (thisDay || nextDay) {

      todaysEvents[numberOfTodaysEvents].type = events[i].type;
      if (thisDay) {
        todaysEvents[numberOfTodaysEvents].day = TodaysEventDay::TODAY;
      }

      if (nextDay) {
        todaysEvents[numberOfTodaysEvents].day = TodaysEventDay::TOMORROW;
      }
      numberOfTodaysEvents++;

      const int index = static_cast<int>(events[i].type);
      r = TrashTypeRed[index];
      g = TrashTypeGreen[index];
      b = TrashTypeBlue[index];
      Serial.println(TrashTypeStrings[static_cast<int>(events[i].type)]);
    }
  }

  if (numberOfTodaysEvents == 1) {
    for (int p = 0; p < numOfNeoPixels; p++) {
      if (p != numOfNeoPixels / 2 && thisDay) {
        pixels.setPixelColor(p, pixels.Color(0, 0, 0));
        continue;
      }
      pixels.setPixelColor(p, pixels.Color(r, g, b));
    }
  } else if (1 < numberOfTodaysEvents) {
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    for (int p = 1; p < numOfNeoPixels; p++) {
      const int index = static_cast<int>(todaysEvents[p - 1].type);
      r = TrashTypeRed[index];
      g = TrashTypeGreen[index];
      b = TrashTypeBlue[index];
      pixels.setPixelColor(p, pixels.Color(r, g, b));
      Serial.println(TrashTypeStrings[static_cast<int>(todaysEvents[p - 1].type)]);
    }
  } else {
    pixels.clear();
  }

  if (!lastEventUpdateSuccessful) {
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));
  }

  pixels.show();

  bool newMonth = lastMonth != timeinfo->tm_mon;
  lastMonth = timeinfo->tm_mon;
  lastDay = timeinfo->tm_mday;
  return newMonth;
}

void parseIcsLine(const String& line) {
  static bool trigger = false;
  if (maxNumberOfEvents <= numberOfEvents) {
    Serial.println("Max number of events reached.");
    return;
  }

  if (strstr(line.c_str(), eventTrigger.c_str())) {
    trigger = true;
    const String year = line.substring(eventTrigger.length(), eventTrigger.length() + 4);
    const String month = line.substring(eventTrigger.length() + 4, eventTrigger.length() + 6);
    const String day = line.substring(eventTrigger.length() + 6, eventTrigger.length() + 8);

    events[numberOfEvents].year = (uint8_t)atoi(year.c_str()) - 2000;
    events[numberOfEvents].month = (uint8_t)atoi(month.c_str());
    events[numberOfEvents].day = (uint8_t)atoi(day.c_str());

    Serial.println();
    Serial.print(year);
    Serial.print(", ");
    Serial.println(events[numberOfEvents].year);
    Serial.print(month);
    Serial.print(", ");
    Serial.println(events[numberOfEvents].month);
    Serial.print(day);
    Serial.print(", ");
    Serial.println(events[numberOfEvents].day);
    return;
  }

  if (trigger && strstr(line.c_str(), eventTypeTrigger.c_str())) {
    trigger = false;
    for (int i = 0; i < COUNT; i++) {
      events[numberOfEvents].type = TrashType::COUNT;
      if (strstr(line.c_str(), TrashTypeStrings[i].c_str())) {
        events[numberOfEvents].type = static_cast<TrashType>(i);
        break;
      }
    }
    if (events[numberOfEvents].type < COUNT) {
      Serial.println(TrashTypeStrings[static_cast<int>(events[numberOfEvents].type)]);
      numberOfEvents++;
    } else {
      Serial.println("Failure");
    }
  }
}

void replaceYearInUrl(char* url) {
  char* year = strstr(url, ".ics");
  if (!year) {
    return;
  }
  year = year - 2;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  String yearString = String(timeinfo.tm_year - 1900);
  if (yearString.length() != 2) {
    return;
  }

  *year = yearString[0];
  *(year + 1) = yearString[1];
}

bool getIcs() {
  if (WiFi.status() != WL_CONNECTED) {
    startWifi();
  }

  Serial.println("[GET-ICS]");
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  client->setBufferSizes(65536, 2048);
  client->setTimeout(3000);

  Serial.print("[HTTPS] begin get:");
  char url[150];
  strcpy(url, icsURL);
  if (replaceYearStringInUrl) {
    replaceYearInUrl(url);
  }
  Serial.println(url);

  HTTPClient https;
  if (!https.begin(*client, url)) {
    Serial.printf("[HTTPS] Unable to connect\n");
    return false;
  }

  Serial.print("[HTTPS] GET...\n");
  int httpCode = https.GET();  // start connection and send HTTP header
  if (httpCode <= 0) {         // httpCode will be negative on error
    Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    return false;
  }
  Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[HTTPS] GET... HTTP CODE not OK, error: %s\n", https.errorToString(httpCode).c_str());
    return false;
  }

  size_t totalCharacters = 0;
  numberOfEvents = 0;
  String line, oldLine;
  bool onlyDigitsInLine = false;
  unsigned int lineNumber = 0;
  int zeroLines = 0;

  while(line = client->readStringUntil('\n')) {
    if(line.length() == 0) {
      zeroLines++;
      Serial.println("zero line");
      if( 2 <= zeroLines) {
        Serial.println("zero line break");
          break;
      } else {
        continue;
      }
    }
    zeroLines = 0;
    lineNumber++;
    if(extraDebugPrint) {
      Serial.print(lineNumber);
      Serial.print(": ");
      Serial.println(line);
    }
    totalCharacters += line.length();
    line.trim();

    if (onlyDigitsInLine) {
      oldLine += line;
    }
    bool skipOldLine = onlyDigitsInLine;

    onlyDigitsInLine = std::all_of(line.begin(), line.end(), ::isdigit);

    if (!onlyDigitsInLine) {
      if (!oldLine.isEmpty()) {
        parseIcsLine(oldLine);
      }

      oldLine = skipOldLine ? "" : line;
    }
  }
  https.end();

  Serial.printf("Total non-control characters gotten: %zu\n", totalCharacters);
  Serial.printf("Total events parsed: %d\n", numberOfEvents);
  if (numberOfEvents == 0) {
    Serial.println("Failure, expected more than zero events.");
    return false;
  }

  stopWifi();  // Save energy.
  return true;
}