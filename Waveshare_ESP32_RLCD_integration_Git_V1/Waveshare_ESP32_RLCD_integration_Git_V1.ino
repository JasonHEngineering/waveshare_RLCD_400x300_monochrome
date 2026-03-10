#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "display_bsp.h"   // Waveshare driver (DisplayPort)
#include <Wire.h>
#include "esp_sleep.h"
#include "font.h"
#include "secfont.h"
#include "PCF85063A.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

#include <time.h>
#include "i2c_bsp.h"
#include "codec_bsp.h"

 // your pre-converted 400x300 .bin image
 // use "Image_bin_generator_V2.py"
#include "SGP_skyline_1_image.h" 
#include "starry_night_1_image.h" 
#include "Jason_H_Engineering_Logo_400_300_1_image.h" 
#include "SGP_map_1_image.h"  
#include "paris_skyline_1_image.h"  

// ---------- AUDIO RELATED ----------
#define I2C_SDA 13
#define I2C_SCL 14
#define PA_CTRL 46

#define SAMPLE_RATE      16000
#define CHANNELS         1
#define BITS_PER_SAMPLE  16

#define RECORD_SECONDS   6
#define RECORD_TIME_MS 5000
#define AUDIO_FRAME_SIZE 2048
#define RECORD_BYTES (SAMPLE_RATE * RECORD_SECONDS * 2)

#define RMS_THRESHOLD    1000 //300 for living room, but I will use 800 for avoiding accidental triggers 
#define TRIGGER_FRAMES   3

static int trigger_count = 0;
float rmsValue = 0.0;
float smoothedRMS = 0.0;
unsigned long lastUIUpdate = 0;
unsigned long recordingStart = 0;
uint8_t frame_buffer[AUDIO_FRAME_SIZE];
uint8_t* record_buffer = NULL;
uint8_t* wav_buffer = NULL;

WiFiClientSecure client;

static I2cMasterBus I2cbus(I2C_SCL, I2C_SDA, 0); // scl, sda, port
CodecPort *codecport = NULL;
PCF85063A rtc;

// ---------- GPT RELATED ----------
String GPT_text = "Ask your question, keep it within " + String(RECORD_SECONDS) + " seconds!";
String GPT_answer = "";
String transcribe_model = "gpt-4o-mini-transcribe"; // else "gpt-4o-transcribe"
String gpt_model = "gpt-4.1-nano"; // else "gpt-4o-mini"

// ---------- GPT STATE MACHINE ----------
enum SystemState {
  IDLE,
  RECORDING,
  PROCESSING,
  DISPLAY_RESPONSE
};
SystemState state = IDLE;


// ---------- TRIVIAL STATE MACHINE ----------
enum TRIVIAL_SystemState {
  IMAGE_STATE,
  QUESTION_STATE,
  ANSWER_STATE
};

//TRIVIAL_SystemState state = IDLE;
TRIVIAL_SystemState TRIVIAL_currentScreen = IMAGE_STATE;
TRIVIAL_SystemState TRIVIAL_previousScreen = IMAGE_STATE;

unsigned long trivia_currentMillis = 0;

// Array of image pointers
const uint8_t* images[] = {
    Jason_H_Engineering_Logo_400_300_1_image_bin,
    SGP_skyline_1_image_bin,
    starry_night_1_image_bin,
    SGP_map_1_image_bin,
    paris_skyline_1_image_bin

};

const int imageCount = sizeof(images) / sizeof(images[0]);

int currentImage = 0;
unsigned long image_start = 0;
unsigned long trivial_Q_start = 0;
unsigned long trivial_A_start = 0;

const unsigned long image_interval = 8000; // 8 seconds
const unsigned long trivial_Q_interval = 8000; // 8 seconds
const unsigned long trivial_A_interval = 8000; // 8 seconds
String trivia_correct;
String trivia_question;

String htmlDecode(String input) {
  input.replace("&quot;", "\"");
  input.replace("&#039;", "'");
  input.replace("&amp;", "&");
  input.replace("&lt;", "<");
  input.replace("&gt;", ">");
  return input;
}

#define BUTTON_PIN 18 //switching key/button
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;
bool lastButtonState = HIGH;
unsigned long lastHomeUpdate = 0;
unsigned long lastBusUpdate = 0;

unsigned long lastBusScreen = 0;

// ----------------------------
// Screen State Machine
// ----------------------------
enum ScreenState {
  SCREEN_CLOCK,
  SCREEN_BUS,
  SCREEN_TRIVIA,
  SCREEN_GPT,
  SCREEN_CALENDAR
};

ScreenState currentScreen = SCREEN_CLOCK;
ScreenState previousScreen = SCREEN_CLOCK;

const char* ssid1 = "your_SSID_1";
const char* pass1 = "your_SSID_1_PASSWORD";

const char* ssid2 = "your_SSID_2";
const char* pass2 = "your_SSID_2_PASSWORD";

const unsigned long connectTimeout = 10000; // wifi timeout 10 seconds

const char* apiKey = "your_datamall_API_key"; // datamall API key
const char* openai_key = "your_OpenAI_API_key"; // OpenAI API key

const char* temp_RH_station_ = "S109"; // S109 is for "Ang Mo Kio Avenue 5", Change to your closest station

const char* busStopCode_one = "67609"; //Change to your own bus stop ID
const char bus_stop_name_one[] = "Buangkok Stn Exit A";

const char* busStopCode_two = "63009"; //Change to your own bus stop ID
const char bus_stop_name_two[] = "Buangkok Int";

String RH_JSON = "https://api-open.data.gov.sg/v2/real-time/api/relative-humidity";
String temp_JSON = "https://api-open.data.gov.sg/v2/real-time/api/air-temperature";

char forecast[32] = "TBD";   // global
char PM25[8];   // global

unsigned long lastRun = 0;
const unsigned long BUS_INTERVAL = 60000UL; // 60 seconds to pull data from datamall

struct DataSet {
  char busNo[10];
  char eta1[10];
  char eta2[10];
};

const int MAX_RECORDS = 20;
DataSet data_one[MAX_RECORDS];
DataSet data_two[MAX_RECORDS];

const int chunkSize = 5;
DataSet chunk_one[chunkSize];
DataSet chunk_two[chunkSize];

int Current_Page_one = 1;
int Current_Page_two = 1;
int dataCount_one = 0;
int dataCount_two = 0;
int number_of_bus_one = 0;
int number_of_bus_two = 0;
int number_of_bus_page_one = 1;
int number_of_bus_page_two = 1;
int chunk_one_length = 5;
int chunk_two_length = 5;

const unsigned long BusScreen_INTERVAL = 5000;  // 5 second interval for the bus screen update
const int BAT_ADC_PIN = 4; // GPIO4

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 1000;  // 1 second interval for the Temp and RH sensor
static const uint8_t SHTC3_ADDR = 0x70; // I2C address for the Temp and RH sensor
static const int W = 400;
static const int H = 300;

// Singapore timezone offset in seconds
const int UTC_OFFSET = 8 * 3600;

// clountdown timer to update RTC and weather
const unsigned long SYNC_INTERVAL = 1800000UL;  // 30 mins
unsigned long lastSync = 0;


DisplayPort RlcdPort(12, 11, 5, 40, 41, W, H);
// DisplayPort::DisplayPort(int mosi, int scl, int dc, int cs, int rst, int width, int height, spi_host_device_t spihost)
// 1-bit "sprite/canvas"
GFXcanvas1 canvas(W, H);
float temp, humidity;  // temperature and humidity
U8G2_FOR_ADAFRUIT_GFX u8g2;

float v_bat=0.0;
int batt_raw=0;
float v_adc=0.0;  

const char* weekdayNames[] = {
  "Sunday","Monday","Tuesday","Wednesday",
  "Thursday","Friday","Saturday"
};

const char* monthNames[] = {
  "Jan","Feb","Mar","Apr","May","Jun",
  "Jul","Aug","Sep","Oct","Nov","Dec"
};


void connectWiFi() {
  bool useFirst = true;

  while (WiFi.status() != WL_CONNECTED) {

    const char* ssid = useFirst ? ssid1 : ssid2;
    const char* pass = useFirst ? pass1 : pass2;

    Serial.printf("\nTrying %s\n", ssid);

    WiFi.begin(ssid, pass);

    unsigned long startAttemptTime = millis();

    // Try for connectTimeout milliseconds
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttemptTime < connectTimeout) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return;
    }

    Serial.println("\nFailed. Switching network...");

    WiFi.disconnect(true);
    delay(1000);

    useFirst = !useFirst;  // 🔁 alternate
  }
}


// set canvas to display
void pushCanvasToRLCD(bool invert = false) {
  uint8_t *buf = canvas.getBuffer();        // 1bpp, packed
  const int bytesPerRow = (W + 7) / 8;      // za 400px = 50

  // Očisti RLCD buffer na bijelo (u tvom driveru postoji)
  RlcdPort.RLCD_ColorClear(ColorWhite);

  for (int y = 0; y < H; y++) {
    uint8_t *row = buf + y * bytesPerRow;

    for (int bx = 0; bx < bytesPerRow; bx++) {
      uint8_t v = row[bx];
      if (invert) v ^= 0xFF;

     
      int x0 = bx * 8;
      for (int bit = 0; bit < 8; bit++) {
        int x = x0 + bit;
        if (x >= W) break;

        bool on = (v & (0x80 >> bit)) != 0;
        if (on) {
          RlcdPort.RLCD_SetPixel((uint16_t)x, (uint16_t)y, ColorBlack);
        }
      }
    }
  }

  // Refresh 
  RlcdPort.RLCD_Display();
}

int ETA_in_mins(const char* eta) {

  if (eta == nullptr || strlen(eta) == 0) {
    Serial.println("No data");
    return -1;
  }

  struct tm tm;
  memset(&tm, 0, sizeof(tm));

  // Parse ISO-8601 (ignore timezone part)
  strptime(eta, "%Y-%m-%dT%H:%M:%S", &tm);

  time_t busTime = mktime(&tm);
  time_t now;
  time(&now);

  int eta_min = difftime(busTime, now) / 60;

  return eta_min;
}


bool syncTimeWithNTP()
{
  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  // 8*3600 = Singapore UTC+8

  struct tm timeinfo;

  Serial.print("Waiting for NTP time sync");

  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 15) {
    Serial.print(".");
    delay(500);
    retry++;
  }

  if (retry >= 15) {
    Serial.println("\nNTP sync failed");
    return false;
  }

  Serial.println("\nTime synchronized");

  uint8_t NTP_weekday = timeinfo.tm_wday;   // 0=Sunday
  uint8_t NTP_day     = timeinfo.tm_mday;
  uint8_t NTP_month   = timeinfo.tm_mon + 1;
  uint16_t NTP_year   = (timeinfo.tm_year + 1900); // PCF85063A expects 4-digit year

  uint8_t NTP_hour    = timeinfo.tm_hour;
  uint8_t NTP_minute  = timeinfo.tm_min;
  uint8_t NTP_second  = timeinfo.tm_sec;

  // Write to RTC
  rtc.setDate(NTP_weekday, NTP_day, NTP_month, NTP_year);
  rtc.setTime(NTP_hour, NTP_minute, NTP_second);

  Serial.printf("RTC updated: %02d:%02d:%02d %04d-%02d-%02d\n",
                NTP_hour, NTP_minute, NTP_second, NTP_year, NTP_month, NTP_day);
  return true;
}

int batteryToSegments(float vbat) {
  if (vbat >= 4.0) return 5;
  if (vbat >= 3.90) return 4;
  if (vbat >= 3.80) return 3;
  if (vbat >= 3.65) return 2;
  if (vbat >= 3.50) return 1;
  return 0;
}

void updateClockDisplay()
{
  static int lastDay = -1;   // track date change

  int weekday = rtc.getWeekday();
  int day     = rtc.getDay();
  int month   = rtc.getMonth();
  int year    = rtc.getYear();

  int hour    = rtc.getHour();
  int minute  = rtc.getMinute();
  int second  = rtc.getSecond();

  // Format date
  char dateStr[20];
  sprintf(dateStr, "%d %s %d", day, monthNames[month - 1], year);

  canvas.fillScreen(0);   // Clear buffer (black)

  // ----- Weekday -----
  canvas.setFont(&FreeSansBold18pt7b);  // smaller font
  canvas.setCursor(20, 50);
  canvas.print(weekdayNames[weekday]);
  //drawCenteredText(60, weekdayNames[weekday]);

  // ----- Date -----
  // drawCenteredText(90, dateStr);
  canvas.setCursor(20, 90);
  canvas.print(dateStr);

  // ----- Weather -----
  canvas.setFont(&FreeSansBold12pt7b);  // smaller font
  canvas.setCursor(20, 130);
  canvas.print(forecast);

  char timeStr[6];   // HH:MM\0
  sprintf(timeStr, "%02d:%02d", hour, minute);

  char secStr[4];   // HH:MM\0
  sprintf(secStr, "%02d",second);

  canvas.setFont(&DSEG7_Classic_Bold_84);
  canvas.setCursor(20, 270);
  canvas.print(timeStr);

  canvas.setFont(&DSEG7_Classic_Bold_36);
  canvas.setCursor(320, 224);
  canvas.print(secStr);

  // PM25
  int16_t PM_x = 270;
  int16_t PM_y = 50;   // 120 works better visually than 90 on 18pt fonts

  canvas.setFont(&FreeSansBold18pt7b);
  canvas.setCursor(PM_x, PM_y);
  canvas.print(PM25);

  canvas.setFont(&FreeSansBold12pt7b);
  // 2️⃣ Get cursor position after letters
  int16_t PM_x_after = canvas.getCursorX();
  canvas.print("PM");
  PM_x_after = canvas.getCursorX();

  // 3️⃣ Print the subscript numbers manually
  // Offset vertically and reduce font size
  canvas.setFont(&FreeSansBold9pt7b);   // smaller font for subscript
  int16_t PM_sub_y = PM_y + 8;               // adjust downward (baseline + offset)
  canvas.setCursor(PM_x_after + 2, PM_sub_y);
  canvas.print("2.5");
  canvas.setFont(&FreeSansBold18pt7b);
  
  // RH and Temp
  char t_str[12]; 
  char h_str[12]; 
  sprintf(t_str, "%.1f", temp);
  sprintf(h_str, "%.1f%%", humidity);

  int16_t y = 90;  
  canvas.setCursor(PM_x, y);
  canvas.print(t_str);

  // 2️⃣ Get position after number
  int16_t x_after = canvas.getCursorX();

  // 3️⃣ Draw degree symbol (tuned for 18pt on 4.2" display)
  int16_t deg_x = x_after + 8;      // horizontal offset
  int16_t deg_y = y - 20;           // vertical offset
  int16_t deg_r = 5;                // radius

  canvas.fillCircle(deg_x, deg_y, deg_r, 1);
  canvas.fillCircle(deg_x, deg_y, deg_r - 3, 0);  // hollow center (1 = white)

  // 4️⃣ Print the "C"
  canvas.setCursor(x_after + 12, y);
  canvas.print("c");

  canvas.setCursor(PM_x, 130);
  canvas.print(h_str);

  //bat frame
  int bat_frame_start_X = 353;
  int bat_frame_start_Y = 5; //+269
  int bat_frame_start_W = 43;
  int bat_frame_start_H = 26;
  canvas.fillRect(bat_frame_start_X, bat_frame_start_Y, bat_frame_start_W, bat_frame_start_H, 1);
  canvas.fillRect(bat_frame_start_X+4, bat_frame_start_Y+4, bat_frame_start_W-8, bat_frame_start_H-8, 0);
  canvas.fillRect(bat_frame_start_X-4, bat_frame_start_Y+8, 4, 10, 1);

  //bet segments
  for(int i=0;i<batteryToSegments(v_bat);i++)
  canvas.fillRect(bat_frame_start_X+5+(i*7), bat_frame_start_Y+6, 5, 14, 1);

  pushCanvasToRLCD(false);
}

// GPT related 
void draw_query()
{
  canvas.fillScreen(0);
  canvas.setTextWrap(true);

  u8g2.begin(canvas);                 // Attach to display
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  canvas.setTextColor(1);          // ← REQUIRED

  printWrapped(u8g2, GPT_text.c_str(), 5, 20, W-15);

  int nextLineY = u8g2.getCursorY();

  u8g2.setCursor(3, nextLineY+20);
  u8g2.print("-------------------------------------------------------------------------------");

  pushCanvasToRLCD(false);
}

void drawBus()
{
  canvas.fillScreen(0);
  
  int start_col = 10;

  // Bus Stop Code One
  canvas.setFont(&FreeSansBold9pt7b);
  canvas.setTextColor(1);          // ← REQUIRED
  canvas.setCursor(start_col, 25);
  String mystring = "Bus Stop ID: " + String(busStopCode_one);
  canvas.print(mystring);

  canvas.setCursor(start_col, 41);
  canvas.print(bus_stop_name_one);

  int col1 = 60;
  int col2 = 102;
  int col3 = 157;
  int row_header = 70;
  int row_interval = 43;

  canvas.setCursor(col1-30, row_header);
  canvas.print("Bus");    
  canvas.setCursor(col2, row_header);
  canvas.print("Eta");    

  // 2️⃣ Get cursor position after letters
  int16_t x_after_eta = canvas.getCursorX();
  int16_t y_after_eta = canvas.getCursorY();
  // 3️⃣ Print the subscript numbers manually
  // Offset vertically and reduce font size
  canvas.setFont(&FreeSans9pt7b);   // smaller font for superscript
  int16_t sub_y = y_after_eta - 8;               // adjust downward (baseline + offset)
  canvas.setCursor(x_after_eta, sub_y);
  canvas.print("1");
  canvas.setFont(&FreeSansBold9pt7b);

  canvas.setCursor(col3, row_header);
  canvas.print("Eta");    

  x_after_eta = canvas.getCursorX();
  y_after_eta = canvas.getCursorY();
  canvas.setFont(&FreeSans9pt7b);   // smaller font for superscript
  sub_y = y_after_eta - 8;               // adjust downward (baseline + offset)
  canvas.setCursor(x_after_eta, sub_y);
  canvas.print("2");
  canvas.setFont(&FreeSansBold9pt7b);

  for(int j=0;j<chunk_one_length;j++){
    canvas.fillRoundRect(start_col, 78+(row_interval*j), 70, 30, 5, 1);
    canvas.setTextColor(0);
    canvas.setFont(&FreeSansBold12pt7b);

    // compensate cursor -x for bus char length
    size_t len = strlen(chunk_one[j].busNo);
    int compensator = 49-(70-14*len)/2;

    canvas.setCursor(col1-compensator, 100+(row_interval*j));
    canvas.print(chunk_one[j].busNo);

    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold12pt7b);
    canvas.setCursor(col2, 100+(row_interval*j));
    canvas.print(chunk_one[j].eta1);

    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold12pt7b);
    canvas.setCursor(col3, 100+(row_interval*j));
    canvas.print(chunk_one[j].eta2);
  }


  // Bus Stop Code Two
  canvas.setFont(&FreeSansBold9pt7b);
  canvas.setTextColor(1);          // ← REQUIRED
  canvas.setCursor(200 + start_col, 25);
  mystring = "Bus Stop ID: " + String(busStopCode_two);
  canvas.print(mystring);

  canvas.setCursor(200 + start_col, 41);
  canvas.print(bus_stop_name_two);

  int col4 = 200 + col1;
  int col5 = 200 + col2;
  int col6 = 200 + col3;

  canvas.setCursor(col4-30, row_header);
  canvas.print("Bus");    
  canvas.setCursor(col5, row_header);
  canvas.print("Eta");    

  // 2️⃣ Get cursor position after letters
  x_after_eta = canvas.getCursorX();
  y_after_eta = canvas.getCursorY();
  // 3️⃣ Print the subscript numbers manually
  // Offset vertically and reduce font size
  canvas.setFont(&FreeSans9pt7b);   // smaller font for superscript
  sub_y = y_after_eta - 8;               // adjust downward (baseline + offset)
  canvas.setCursor(x_after_eta, sub_y);
  canvas.print("1");
  canvas.setFont(&FreeSansBold9pt7b);

  canvas.setCursor(col6, row_header);
  canvas.print("Eta");    

  x_after_eta = canvas.getCursorX();
  y_after_eta = canvas.getCursorY();
  canvas.setFont(&FreeSans9pt7b);   // smaller font for superscript
  sub_y = y_after_eta - 8;               // adjust downward (baseline + offset)
  canvas.setCursor(x_after_eta, sub_y);
  canvas.print("2");
  canvas.setFont(&FreeSansBold9pt7b);

  for(int j=0;j<chunk_two_length;j++){
    canvas.fillRoundRect(200 + start_col, 78+(row_interval*j), 70, 30, 5, 1);
    canvas.setTextColor(0);
    canvas.setFont(&FreeSansBold12pt7b);

    // compensate cursor -x for bus char length
    size_t len = strlen(data_two[j].busNo);
    int compensator = 49-(70-14*len)/2;

    canvas.setCursor(col4-compensator, 100+(row_interval*j));
    canvas.print(data_two[j].busNo);

    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold12pt7b);
    canvas.setCursor(col5, 100+(row_interval*j));
    canvas.print(data_two[j].eta1);

    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold12pt7b);
    canvas.setCursor(col6, 100+(row_interval*j));
    canvas.print(data_two[j].eta2);
  }

  // vertical line in the middle
  int centerX = 400 / 2;
  int thickness = 3;

  canvas.fillRect(centerX - thickness/2, 0, thickness, 300, 1);

  pushCanvasToRLCD(false);
  }


// ----------------------------
// Button Handling
// ----------------------------
void handleButton() {

  bool reading = digitalRead(BUTTON_PIN);

  if (reading == LOW && lastButtonState == HIGH &&
      (millis() - lastDebounceTime) > debounceDelay) {

    lastDebounceTime = millis();

    // Toggle screen
    if (currentScreen == SCREEN_CLOCK) {
      currentScreen = SCREEN_BUS;
      weatherforecast();
      get_temp();
      get_humidity();
      syncTimeWithNTP();
      getPM25();
    }
    else if (currentScreen == SCREEN_BUS) {
      currentScreen = SCREEN_TRIVIA;
      updateBusArrival_one();
      updateBusArrival_two();
    }
    else if (currentScreen == SCREEN_TRIVIA) {
      currentScreen = SCREEN_GPT;
      draw_query(); // initial screen
      }
    else if (currentScreen == SCREEN_GPT)
      currentScreen = SCREEN_CLOCK;
  }

  drawScreen();
  lastButtonState = reading;
}


// ----------------------------
// Timer Handler
// ----------------------------
void handleScreenTimers() {

  unsigned long currentMillis = millis();

  // Reset timer when screen changes
  if (currentScreen != previousScreen) {
    previousScreen = currentScreen;

    if (currentScreen == SCREEN_CLOCK)
      lastHomeUpdate = currentMillis;

    if (currentScreen == SCREEN_BUS)
      lastBusUpdate = currentMillis;
  }

  switch (currentScreen) {

    case SCREEN_CLOCK:
      if (currentMillis - lastHomeUpdate >= SYNC_INTERVAL) {
        lastHomeUpdate = currentMillis;

        connectWiFi();

        if (WiFi.status() == WL_CONNECTED) {
            read_batt_ADC();
            weatherforecast();
            get_temp();
            get_humidity();
            syncTimeWithNTP();
            getPM25();
          lastSync = millis();
          Serial.println("Update complete");
        }
        else {
          Serial.println("WiFi failed, recoonecting ... ");
          connectWiFi();
        }
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);

      }
      break;

    case SCREEN_BUS:
      if (currentMillis - lastBusUpdate >= BUS_INTERVAL) {
        lastBusUpdate = currentMillis;
        updateBusArrival_one();
        updateBusArrival_two();
      }
      break;

    case SCREEN_TRIVIA:

    // placeholder timer handler for future development

    break;

    case SCREEN_GPT:

    // placeholder timer handler for future development

    break;

  }
}



// ----------------------------
// Screen Drawing Controller
// ----------------------------
void drawScreen() {

  switch (currentScreen) {

    case SCREEN_CLOCK:

      updateClockDisplay();

      break;

    case SCREEN_BUS:
      number_of_bus_page_one = (number_of_bus_one + chunkSize -1)/chunkSize;
      number_of_bus_page_two = (number_of_bus_two + chunkSize -1)/chunkSize;      
      if (millis() - lastBusScreen >= BusScreen_INTERVAL) {
        lastBusScreen = millis();
        // update a dynamic struct array from updateBusArrival_one and updateBusArrival_two 
        // so that if there are more than 5 buses at a bus stop
        // the buses  no. and ETA and reloaded in mutiples of 5 every 5 seconds
        Current_Page_one+=1;
        if (Current_Page_one > number_of_bus_page_one) {
          Current_Page_one = 1;
          }
        chunk_one_length = chunkSize;
        if (Current_Page_one == number_of_bus_page_one) { // last page
            chunk_one_length = number_of_bus_one % chunkSize;
            if (chunk_one_length == 0) {
              chunk_one_length = chunkSize;
              };
          }
        // Copy elements into chunk
        for (int i = 0; i < chunkSize; i++) {
          chunk_one[i] = data_one[(Current_Page_one-1)*chunkSize + i];   // struct copy (safe)
        }

        Current_Page_two+=1;
        if (Current_Page_two > number_of_bus_page_two) {
          Current_Page_two = 1;
          }
        chunk_two_length = chunkSize;
        if (Current_Page_two == number_of_bus_page_two) { // last page
            chunk_two_length = number_of_bus_two % chunkSize;
            if (chunk_two_length == 0) {
              chunk_two_length = chunkSize;
              };
          }
        // Copy elements into chunk
        for (int i = 0; i < chunkSize; i++) {
          chunk_two[i] = data_two[(Current_Page_two-1)*chunkSize + i];   // struct copy (safe)
        }
      }
      drawBus();
      break;

    case SCREEN_TRIVIA:

      switch (TRIVIAL_currentScreen) {

        case IMAGE_STATE:
          handleImage();
          break;

        case QUESTION_STATE:
          handleTrivia_question();
          break;

        case ANSWER_STATE:
          drawTrivial_answer();
          break;
      }

    break;

    case SCREEN_GPT:    

      readMicrophone();
      switch (state) {

        case IDLE:
          handleIdle();
          break;

        case RECORDING:
          handleRecording();
          break;

        case PROCESSING:
          handleProcessing();
          break;

        case DISPLAY_RESPONSE:
          handleResponse();
          break;
      }
    break;
  }
}


void weatherforecast() {

  HTTPClient http;
  http.begin("https://api.data.gov.sg/v1/environment/2-hour-weather-forecast");
  int httpCode2 = http.GET();

  if (httpCode2 == 200) {
    DynamicJsonDocument doc(8192);
    deserializeJson(doc, http.getString());

    const char* pulled_forecast =
      doc["items"][0]["forecasts"][0]["forecast"];
    //Serial.println(forecast);
    snprintf(forecast, sizeof(forecast), pulled_forecast);
  }
  http.end();
}


void get_temp() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(temp_JSON);

    int httpCode = http.GET();

    if (httpCode == 200) {
      DynamicJsonDocument doc(8192);
      deserializeJson(doc, http.getString());

    // Find temperature for station S109
    JsonArray readings = doc["data"]["readings"][0]["data"];
    for (JsonObject reading : readings) {
      if (String(reading["stationId"]) == temp_RH_station_) {
        temp = reading["value"];
        Serial.print("Station S109 Temperature: ");
        Serial.print(temp);
        Serial.println(" °C");
        break;
      }
    }
  }
    http.end();
  }
}




void get_humidity() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(RH_JSON);

    int httpCode = http.GET();

    if (httpCode == 200) {
      DynamicJsonDocument doc(8192);
      deserializeJson(doc, http.getString());

    // Find temperature for station S109
    JsonArray readings = doc["data"]["readings"][0]["data"];
    for (JsonObject reading : readings) {
      if (String(reading["stationId"]) == temp_RH_station_) { // "S109" is for "Ang Mo Kio Avenue 5"
        humidity = reading["value"];
        Serial.print("Station S109 Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");
        break;
      }
    }
  }
    http.end();
  }
}



void getPM25() {
  if (WiFi.status() == WL_CONNECTED) {
    
    HTTPClient http;
    http.begin("https://api.data.gov.sg/v1/environment/pm25");
    
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      String payload = http.getString();
      
      Serial.println("Response:");
      Serial.println(payload);

      // Parse JSON
      DynamicJsonDocument doc(4096);
      deserializeJson(doc, payload);

      JsonObject readings = doc["items"][0]["readings"]["pm25_one_hourly"];
      
      int central = readings["central"] | -1; // just central Singapore is good enough
      sprintf(PM25, "%d", central);

    } else {
      Serial.print("Error: ");
      Serial.println(httpResponseCode);
    }
    
    http.end();
  }
}



void updateBusArrival_one() {

  HTTPClient http1;

  String url = "https://datamall2.mytransport.sg/ltaodataservice/v3/BusArrival?BusStopCode=" + String(busStopCode_one);

  http1.begin(url);
  http1.addHeader("AccountKey", apiKey);
  http1.addHeader("accept", "application/json");

  int httpCode1 = http1.GET();
  if (httpCode1 == 200) {
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, http1.getString());

    if (error) {
      Serial.print("JSON Error: ");
      Serial.println(error.c_str());
      http1.end();
      delay(30000);
      return;
    }

    JsonArray services = doc["Services"].as<JsonArray>();

    dataCount_one = 0;

    for (JsonObject service : services) {

      if (dataCount_one >= MAX_RECORDS) break;

      const char* pulled_busNo = service["ServiceNo"] | "";
      const char* pulled_eta1  = service["NextBus"]["EstimatedArrival"] | "";
      const char* pulled_eta2  = service["NextBus2"]["EstimatedArrival"] | "";

      // store above in dynamic global array
      snprintf(data_one[dataCount_one].busNo, sizeof(data_one[dataCount_one].busNo), "%s", pulled_busNo);
      snprintf(data_one[dataCount_one].eta1, sizeof(data_one[dataCount_one].eta1), "%d", ETA_in_mins(pulled_eta1));
      snprintf(data_one[dataCount_one].eta2, sizeof(data_one[dataCount_one].eta2), "%d", ETA_in_mins(pulled_eta2));

      dataCount_one++;

    }

    number_of_bus_one = dataCount_one;
  }
  http1.end();

}


void updateBusArrival_two() {

  HTTPClient http1;

  String url = "https://datamall2.mytransport.sg/ltaodataservice/v3/BusArrival?BusStopCode=" + String(busStopCode_two);

  http1.begin(url);
  http1.addHeader("AccountKey", apiKey);
  http1.addHeader("accept", "application/json");

  int httpCode1 = http1.GET();
  if (httpCode1 == 200) {
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, http1.getString());

    if (error) {
      Serial.print("JSON Error: ");
      Serial.println(error.c_str());
      http1.end();
      delay(30000);
      return;
    }

    JsonArray services = doc["Services"].as<JsonArray>();

    dataCount_two = 0;

    for (JsonObject service : services) {

      if (dataCount_two >= MAX_RECORDS) break;

      const char* pulled_busNo = service["ServiceNo"] | "";
      const char* pulled_eta1  = service["NextBus"]["EstimatedArrival"] | "";
      const char* pulled_eta2  = service["NextBus2"]["EstimatedArrival"] | "";

      // store above in dynamic global array
      snprintf(data_two[dataCount_two].busNo, sizeof(data_two[dataCount_two].busNo), "%s", pulled_busNo);
      snprintf(data_two[dataCount_two].eta1, sizeof(data_two[dataCount_two].eta1), "%d", ETA_in_mins(pulled_eta1));
      snprintf(data_two[dataCount_two].eta2, sizeof(data_two[dataCount_two].eta2), "%d", ETA_in_mins(pulled_eta2));

      dataCount_two++;

    }

    number_of_bus_two = dataCount_two;
  }
  http1.end();

}



void fetchTrivia() {
  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;
    http.begin("https://opentdb.com/api.php?amount=1&type=multiple");

    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String payload = http.getString();

      // Parse JSON
      DynamicJsonDocument doc(4096);
      deserializeJson(doc, payload);

      trivia_correct = doc["results"][0]["correct_answer"].as<String>();
      trivia_correct = htmlDecode(trivia_correct);

      trivia_question = doc["results"][0]["question"].as<String>();
      trivia_question = htmlDecode(trivia_question);

    } else {
      Serial.print("Error: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  }
}


void handleImage() {
  if (millis() - image_start >= image_interval) {
      trivial_Q_start = millis();

      currentImage++;
      if (currentImage >= imageCount)
          currentImage = 0;
      RlcdPort.RLCD_DisplayRaw(images[currentImage]);
      fetchTrivia();

      TRIVIAL_currentScreen = QUESTION_STATE;
  }
}

void handleTrivia_question() {
  if (millis() - trivial_Q_start >= trivial_Q_interval) {    

    trivial_A_start = millis();
    drawTrivial();

    TRIVIAL_currentScreen = ANSWER_STATE;
  }
}

void drawTrivial_answer() {
  if (millis() - trivial_A_start >= trivial_A_interval) {  

    image_start = millis();
    drawCorrect();

    TRIVIAL_currentScreen = IMAGE_STATE;
  }
}


void draw_common_part_of_trivia()
{
  canvas.fillScreen(0);
  canvas.setTextWrap(true);

  u8g2.begin(canvas);                 // Attach to display
  u8g2.setFont(u8g2_font_fur20_tf);
  canvas.setTextColor(1);          
  printWrapped(u8g2, trivia_question.c_str(), 5, 25, W-15);

  int nextLineY = u8g2.getCursorY();
  u8g2.setCursor(3, nextLineY+25);
  u8g2.print("-------------------------------------------------------------------------------");
}


void drawTrivial()
{
  draw_common_part_of_trivia();
  pushCanvasToRLCD(false);
}


void drawCorrect() {

  draw_common_part_of_trivia();
  int nextLineY = u8g2.getCursorY();
  printWrapped(u8g2, trivia_correct.c_str(), 5, nextLineY+25, W-15);
  pushCanvasToRLCD(false);

}

// ================= CUSTOM TEXT AND WORD WRAPPER  =================
void printWrapped(U8G2_FOR_ADAFRUIT_GFX &u8g2,
                      const char *text,
                      int startX,
                      int startY,
                      int maxWidth)
{
    int cursorY = startY;
    int lineHeight = u8g2.getFontAscent() - u8g2.getFontDescent() + 4;

    char lineBuffer[256];   // buffer for current line
    int lineLen = 0;
    int lineWidth = 0;

    char wordBuffer[128];   // buffer for current English word
    int wordLen = 0;
    int wordWidth = 0;

    int i = 0;
    while (text[i] != '\0') {
        unsigned char c = text[i];
        int charLen = 1;

        // Determine UTF-8 length
        if (c < 0x80) charLen = 1;
        else if ((c & 0xE0) == 0xC0) charLen = 2;
        else if ((c & 0xF0) == 0xE0) charLen = 3;
        else if ((c & 0xF8) == 0xF0) charLen = 4;

        char tempChar[5] = {0};
        memcpy(tempChar, &text[i], charLen);
        tempChar[charLen] = '\0';
        i += charLen;

        // ---------- ASCII / English ----------
        if ((unsigned char)c < 0x80) {
            if (c == ' ' || c == '\n') {
                // Flush word buffer
                if (wordLen > 0) {
                    if (lineWidth + wordWidth > maxWidth) {
                        // print current line
                        if (lineLen > 0) {
                            lineBuffer[lineLen] = '\0';
                            u8g2.setCursor(startX, cursorY);
                            u8g2.print(lineBuffer);
                            cursorY += lineHeight;
                        }
                        lineLen = 0;
                        lineWidth = 0;
                    }
                    // append word to lineBuffer
                    memcpy(lineBuffer + lineLen, wordBuffer, wordLen);
                    lineLen += wordLen;
                    lineWidth += wordWidth;
                    wordLen = 0;
                    wordWidth = 0;
                }

                // handle space
                if (c == ' ') {
                    int spaceWidth = u8g2.getUTF8Width(" ");
                    if (lineWidth + spaceWidth > maxWidth) {
                        // wrap
                        if (lineLen > 0) {
                            lineBuffer[lineLen] = '\0';
                            u8g2.setCursor(startX, cursorY);
                            u8g2.print(lineBuffer);
                            cursorY += lineHeight;
                        }
                        lineLen = 0;
                        lineWidth = 0;
                    } else {
                        lineBuffer[lineLen++] = ' ';
                        lineWidth += spaceWidth;
                    }
                }

                // handle newline
                if (c == '\n') {
                    if (lineLen > 0) {
                        lineBuffer[lineLen] = '\0';
                        u8g2.setCursor(startX, cursorY);
                        u8g2.print(lineBuffer);
                    }
                    cursorY += lineHeight;
                    lineLen = 0;
                    lineWidth = 0;
                }
            } else {
                // accumulate English word
                memcpy(wordBuffer + wordLen, tempChar, charLen);
                wordLen += charLen;
                wordBuffer[wordLen] = '\0';
                wordWidth = u8g2.getUTF8Width(wordBuffer);
            }
        }
        // ---------- UTF-8 / Chinese ----------
        else {
            // flush English word first
            if (wordLen > 0) {
                if (lineWidth + wordWidth > maxWidth-10) {
                    if (lineLen > 0) {
                        lineBuffer[lineLen] = '\0';
                        u8g2.setCursor(startX, cursorY);
                        u8g2.print(lineBuffer);
                        cursorY += lineHeight;
                    }
                    lineLen = 0;
                    lineWidth = 0;
                }
                memcpy(lineBuffer + lineLen, wordBuffer, wordLen);
                lineLen += wordLen;
                lineWidth += wordWidth;
                wordLen = 0;
                wordWidth = 0;
            }

            // measure Chinese char
            int charWidth = u8g2.getUTF8Width(tempChar);

            if (lineWidth + charWidth > (maxWidth-40)) {
                // wrap
                if (lineLen > 0) {
                    lineBuffer[lineLen] = '\0';
                    u8g2.setCursor(startX, cursorY);
                    u8g2.print(lineBuffer);
                    cursorY += lineHeight;
                }
                lineLen = 0;
                lineWidth = 0;
            }

            memcpy(lineBuffer + lineLen, tempChar, charLen);
            lineLen += charLen;
            lineWidth += charWidth;
        }
    }

    // flush remaining word
    if (wordLen > 0) {
        if (lineWidth + wordWidth > (maxWidth-40)) {
            if (lineLen > 0) {
                lineBuffer[lineLen] = '\0';
                u8g2.setCursor(startX, cursorY);
                u8g2.print(lineBuffer);
                cursorY += lineHeight;
            }
            lineLen = 0;
            lineWidth = 0;
        }
        memcpy(lineBuffer + lineLen, wordBuffer, wordLen);
        lineLen += wordLen;
    }

    // flush remaining line
    if (lineLen > 0) {
        lineBuffer[lineLen] = '\0';
        u8g2.setCursor(startX, cursorY);
        u8g2.print(lineBuffer);
    }
}

// ================= WAV HEADER =================
void writeWavHeader(uint8_t* buffer, int dataSize) {
  memcpy(buffer, "RIFF", 4);
  uint32_t chunkSize = dataSize + 36;
  memcpy(buffer + 4, &chunkSize, 4);
  memcpy(buffer + 8, "WAVEfmt ", 8);

  uint32_t subChunk1Size = 16;
  uint16_t audioFormat = 1;
  uint16_t numChannels = CHANNELS;
  uint32_t sampleRate = SAMPLE_RATE;
  uint32_t byteRate = sampleRate * CHANNELS * 2;
  uint16_t blockAlign = CHANNELS * 2;
  uint16_t bitsPerSample = 16;

  memcpy(buffer + 16, &subChunk1Size, 4);
  memcpy(buffer + 20, &audioFormat, 2);
  memcpy(buffer + 22, &numChannels, 2);
  memcpy(buffer + 24, &sampleRate, 4);
  memcpy(buffer + 28, &byteRate, 4);
  memcpy(buffer + 32, &blockAlign, 2);
  memcpy(buffer + 34, &bitsPerSample, 2);

  memcpy(buffer + 36, "data", 4);
  memcpy(buffer + 40, &dataSize, 4);
}

String sendToWhisper(uint8_t* wav_data, int wav_size)
{
  //WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("api.openai.com", 443)) {
    Serial.println("Connection failed");
    return "CONNECT FAIL";
  }

  String boundary = "----ESP32Boundary7MA4YWxkTrZu0gW";

  String head = "";

  // ---- model ----
  head += "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  head += transcribe_model + "\r\n";
  
  // ---- language ----
  head += "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
  head += "en\r\n";

  // ---- file header ----
  head += "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  head += "Content-Type: audio/wav\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";

  int contentLength = head.length() + wav_size + tail.length();

  // ===== HTTP HEADERS =====
  client.println("POST /v1/audio/transcriptions HTTP/1.1");
  client.println("Host: api.openai.com");
  client.println("Authorization: Bearer " + String(openai_key));
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(contentLength));
  client.println("Connection: close");
  client.println();

  // ===== SEND BODY =====
  client.print(head);

  int sent = 0;
  const int chunkSize = 4096;

  while (sent < wav_size) {
    int toSend = min(chunkSize, wav_size - sent);
    client.write(wav_data + sent, toSend);
    sent += toSend;
  }

  client.print(tail);

  Serial.println("Request sent. Waiting response...");


String response = "";
unsigned long startTime = millis();

while (client.connected() || client.available()) {

  if (client.available()) {
    response += client.readString();
    startTime = millis();  // reset timeout when data received
  }

  // Safety timeout (in case server never closes cleanly)
  if (millis() - startTime > 8000) {
    Serial.println("Response timeout");
    break;
  }
}


  client.stop();

  Serial.println("===== RAW RESPONSE =====");
  Serial.println(response);
  Serial.println("========================");

  int jsonStart = response.indexOf("{");
  if (jsonStart < 0) return "NO JSON FOUND";

  String jsonPart = response.substring(jsonStart);

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, jsonPart)) {
    return "JSON ERROR";
  }

  if (!doc.containsKey("text"))
    return "NO TEXT FIELD";

  return doc["text"].as<String>();
}


String askGPT(String question)
{
  //WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("api.openai.com", 443)) {
    return "CONNECT FAIL";
  }

  DynamicJsonDocument doc(2048);
  doc["model"] = gpt_model;
  doc["max_tokens"] = 60;
  doc["temperature"] = 0.3;

  JsonArray messages = doc.createNestedArray("messages");

  JsonObject sys = messages.createNestedObject();
  sys["role"] = "system";
  sys["content"] = "Answer briefly in 1-2 sentences.";

  JsonObject msg = messages.createNestedObject();
  msg["role"] = "user";
  msg["content"] = question;

  String body;
  serializeJson(doc, body);

  client.println("POST /v1/chat/completions HTTP/1.1");
  client.println("Host: api.openai.com");
  client.println("Authorization: Bearer " + String(openai_key));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(body.length()));
  client.println("Connection: close");
  client.println();
  client.print(body);

  String response = "";

  // FAST EXIT LOOP
  while (client.connected() || client.available()) {
    if (client.available()) {
      response += (char)client.read();
    }
  }

  client.stop();

  int jsonStart = response.indexOf("{");
  if (jsonStart < 0) return "NO JSON";

  DynamicJsonDocument resp(4096);
  if (deserializeJson(resp, response.substring(jsonStart))) {
    return "JSON ERROR";
  }

  return resp["choices"][0]["message"]["content"].as<String>();
}

// ================= RECORD CLIP =================
void recordClip() {
  Serial.println("Recording...");

  int bytes_collected = 0;

  while (bytes_collected < RECORD_BYTES) {
    codecport->CodecPort_EchoRead(frame_buffer, AUDIO_FRAME_SIZE);
    //codec->CodecPort_EchoRead(frame_buffer, AUDIO_FRAME_SIZE);
    int copy_bytes = min(AUDIO_FRAME_SIZE, RECORD_BYTES - bytes_collected);
    memcpy(record_buffer + bytes_collected, frame_buffer, copy_bytes);
    bytes_collected += copy_bytes;
  }

  writeWavHeader(wav_buffer, RECORD_BYTES);
  memcpy(wav_buffer + 44, record_buffer, RECORD_BYTES);

}


void readMicrophone() {

  codecport->CodecPort_EchoRead(frame_buffer, AUDIO_FRAME_SIZE);

  int16_t* pcm = (int16_t*)frame_buffer;
  int samples = AUDIO_FRAME_SIZE / 2;

  long long sum = 0;
  for (int i = 0; i < samples; i++)
    sum += (long long)pcm[i] * pcm[i];

  float rms = sqrt((float)sum / samples);
  Serial.printf("RMS: %.2f\n", rms);

  if (rms > RMS_THRESHOLD)
    trigger_count++;
  else
    trigger_count = 0;

}


void handleIdle() {
  
  if ( trigger_count >= TRIGGER_FRAMES) {
    trigger_count = 0;
    Serial.println(">>> Voice detected!");
    state = RECORDING;
  }
}


void handleRecording() {
  canvas.fillScreen(0);
  drawRecordingHeader();
  pushCanvasToRLCD(false);
  recordClip();
  state = PROCESSING;

}

void drawRecordingHeader() {
  canvas.fillCircle(125, 40, 5, 1);
  canvas.drawCircle(125, 40, 10, 2);
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setCursor(145, 48);
  canvas.print("Recording ...");
  pushCanvasToRLCD(false);
}


void handleProcessing() {
  canvas.fillScreen(0);
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setCursor(105, 48);
  canvas.print("Sending to GPT ...");
  pushCanvasToRLCD(false);

  String text = sendToWhisper(wav_buffer, RECORD_BYTES + 44);

  if (text == NULL) {
    GPT_text = "Ask your question, keep it within 5 seconds!";
  } 

  else{
    GPT_text = "Query: " + text;
  }
  draw_query();

  if (text != "NO JSON" && text != "CONNECT FAIL" && text.length() > 3) {
    Serial.println("Asking GPT...");
    String answer = askGPT(text);
    GPT_answer = gpt_model + ": " + answer;
    Serial.println(GPT_answer);
  }
  else {
    GPT_answer = gpt_model + ": Awaiting your query ...";
  }

  state = DISPLAY_RESPONSE;
}


void handleResponse() {

  canvas.fillScreen(0);
  canvas.setTextWrap(true);

  u8g2.begin(canvas);                 // Attach to display
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  canvas.setTextColor(1);          // ← REQUIRED

  printWrapped(u8g2, GPT_text.c_str(), 5, 20, W-15);

  int nextLineY = u8g2.getCursorY();

  u8g2.setCursor(3, nextLineY+20);
  u8g2.print("-------------------------------------------------------------------------------");

  nextLineY = u8g2.getCursorY();

  printWrapped(u8g2, GPT_answer.c_str(), 5, nextLineY+20, W-15);
  pushCanvasToRLCD(false);
  
  state = IDLE;
}


void read_batt_ADC() {
  batt_raw = analogRead(BAT_ADC_PIN);
  v_adc = (batt_raw / 4095.0f) * 3.3f;  
  v_bat = v_adc * 3.0f*1.079; 
}


void setup() {
  Serial.begin(115200);
  delay(100);

  analogReadResolution(12); // 0..4095
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);

  read_batt_ADC();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  RlcdPort.RLCD_Init();
 
  esp_sleep_enable_timer_wakeup(10ULL * 6000000ULL); 

  /* ---- Audio ---- */
  pinMode(PA_CTRL, OUTPUT);
  digitalWrite(PA_CTRL, HIGH);
  delay(300);  // IMPORTANT for codec stability

  Serial.println("Allocating PSRAM...");
  record_buffer = (uint8_t*)heap_caps_malloc(RECORD_BYTES, MALLOC_CAP_SPIRAM);
  wav_buffer = (uint8_t*)heap_caps_malloc(RECORD_BYTES + 44, MALLOC_CAP_SPIRAM);

  codecport = new CodecPort(I2cbus, "S3_RLCD_4_2");
  codecport->CodecPort_SetInfo("es8311 & es7210", 1, SAMPLE_RATE, CHANNELS, 16);
  codecport->CodecPort_SetMicGain(35);

  rtc.setBus(I2cbus);
  rtc.begin();

  /* ---- WiFi ---- */
  Serial.print("Connecting to WiFi");
  connectWiFi();

  weatherforecast();      //SCREEN_CLOCK
  get_humidity();         //SCREEN_CLOCK
  get_temp();             //SCREEN_CLOCK
  syncTimeWithNTP();      //SCREEN_CLOCK
  getPM25();              //SCREEN_CLOCK
  updateBusArrival_one(); //SCREEN_BUS
  updateBusArrival_two(); //SCREEN_BUS

  drawScreen();

}

void loop() {

  handleButton();
  handleScreenTimers();

}