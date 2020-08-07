// Load Wi-Fi library
#include <WiFi.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <ESPAsyncWiFiManager.h>
#include <ArduinoOTA.h>
#include <BME280I2C.h>
#include "time.h"

#define LEDPIN 2
//#define NAN 1e+30
#define isnan(n) n == float(NAN)

MDNSResponder mdns;
DNSServer dnsServer;
AsyncWebServer webServer(80);
AsyncWiFiManager wifiManager(&webServer, &dnsServer);
BME280I2C bme;    // Default : forced mode, standby time = 1000 ms
                  // Oversampling = pressure ×1, temperature ×1, humidity ×1, filter off

char daysOfTheWeek[7][10] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char  monthsOfTheYear[12][10] = {"January", "Febuary", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

// Auxiliar variables to store the current output state
bool output26State = false;
bool output27State = false;

// Assign output variables to GPIO pins
// Motor A
int motor1Pin1 = 27;
int motor1Pin2 = 26;

struct tm timeinfo;
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -25200;     // MST = -7 * 60 * 60
const int   daylightOffset_sec = 3600;  // set to 0 when DST is not active

float lastTemp = 0.0;
float lastHum = 0.0;
float lastPres = 0.0;

// Current time
//currentTime = rtc.now();
time_t epochTime = 0;
unsigned int epochTimeOffset = 0;
time_t espTime = 0;
// Previous time
unsigned long previousTime = 0;
unsigned long previousTime2 = 0;
// Define timeout time in milliseconds (example: 2000ms = 2s)
// timeout once a day.  24 hours * 60 minutes * 60 seconds * 1000 milliseconds
const unsigned long timeoutTime = 86400000;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head><meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
<style>
html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}
.button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;
  text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}
.button2 {background-color: #555555;}
.switch {position: relative; display: inline-block; width: 120px; height: 68px}
.switch input {display: none}
.slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #555555; border-radius: 6px}
.slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px;
  background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 3px}
input:checked+.slider {background-color: #4CAF50}
input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px);
  transform: translateX(52px)}
</style>
</head>

<body><h1>CHAMBER DOOR</h1>
%BUTTONPLACEHOLDER%
<script>
function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  if(element.checked) {
    xhr.open("GET", "/update?output="+element.id+"&state=1", true);
    if(element.id == 26) {
      document.getElementById("doorstatus26").innerHTML = "(GPIO 26) Door is currently OPEN";
    }
    if(element.id == 27) {
      document.getElementById("doorstatus27").innerHTML = "(GPIO 27) Door is currently OPEN";
    }
  }
  else {
    xhr.open("GET", "/update?output="+element.id+"&state=0", true);
    if(element.id == 26) {
      document.getElementById("doorstatus26").innerHTML = "(GPIO 26) Door is currently CLOSED";
    }
    if(element.id == 27) {
      document.getElementById("doorstatus27").innerHTML = "(GPIO 27) Door is currently CLOSED";
    }
  }
  xhr.send();
}
</script>
</body></html>
)rawliteral";

String buttonProcessor(const String& var) {
    Serial.println(var);
    if(var == "BUTTONPLACEHOLDER") {
      String buttons = "";

      if(lastPres > 0.0) {
        buttons += "<p>Temperature: ";
        buttons += String(lastTemp);
        buttons += " F<br>";
        buttons += "Humidity: ";
        buttons += String(lastHum);
        buttons += " %<br>";
        buttons += "Pressure: ";
        buttons += String(lastPres);
        buttons += " in/hg</p>";
      }

      buttons += "<p id=\"doorstatus26\">(GPIO 26) Door is currently ";
      if(output26State) {
        buttons += "OPEN</p>";
        buttons += "<label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"26\" checked><span class=\"slider\"></span></label>";
      } else {
        buttons += "CLOSED</p>";
        buttons += "<label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"26\"><span class=\"slider\"></span></label>";
      }
      buttons += "<p id=\"doorstatus27\">(GPIO 27) Door is currently ";
      if(output27State) {
        buttons += "OPEN</p>";
        buttons += "<label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"27\" checked><span class=\"slider\"></span></label>";
      } else {
        buttons += "CLOSED</p>";
        buttons += "<label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"27\"><span class=\"slider\"></span></label>";
      }
      buttons += "<p>";
      buttons += String(daysOfTheWeek[timeinfo.tm_wday]);
      buttons += ", ";
      buttons += String(monthsOfTheYear[timeinfo.tm_mon]);
      buttons += " ";
      buttons += String(timeinfo.tm_mday);
      buttons += " ";
      buttons += String(timeinfo.tm_year+1900);
      buttons += " ";
      if(timeinfo.tm_hour > 12) {
        buttons += String(timeinfo.tm_hour-12);
      } else if(timeinfo.tm_hour == 0) {
        buttons += String(12);
      } else {
        buttons += String(timeinfo.tm_hour);
      }
      buttons += ":";
      if(timeinfo.tm_min < 10) {
        buttons += String("0");
      }
      buttons += String(timeinfo.tm_min);
      buttons += ".";
      if(timeinfo.tm_sec < 10) {
        buttons += String("0");
      }
      buttons += String(timeinfo.tm_sec);

      if(timeinfo.tm_hour > 12) {
        buttons += " PM";
      } else {
        buttons += " AM";
      }
      buttons += "</p>";

      buttons += "<br>";
      buttons += "<p>Timer set to OPEN the door at 5:30 AM</p>";
      buttons += "<p>Timer set to CLOSE the door at 9:00 PM</p>";
      return buttons;
    }
    return String();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()));

  // sets the pins as outputs:
  pinMode(motor1Pin1, OUTPUT);
  pinMode(motor1Pin2, OUTPUT);
  // pinMode(enable1Pin, OUTPUT);
  pinMode(LEDPIN, OUTPUT);

  Wire.begin();
  bme.begin();

  Serial.println("Connecting to WiFi.");
  wifiManager.autoConnect();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  webServer.on( "/", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("sent web root page");
    Serial.println("Free Heap: " + String(ESP.getFreeHeap()));
    request->send_P( 200, "text/html", index_html, buttonProcessor );
  });

  webServer.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    String var1;
    String var2;
    if(request->hasParam("output") && request->hasParam("state")) {
      var1 = request->getParam("output")->value();
      var2 = request->getParam("state")->value();
      // PROCESS REST API - get button clicks from vals and move the motors
      if(var1.toInt() == 26 && var2.toInt() == 1) {
        // open the door
        Serial.println("button clicked, Open the pod bay door, HAL. DOOR IS OPEN");
        output26State = true;
        digitalWrite(LEDPIN, HIGH);
        // Move the DC motor forward at maximum speed
        Serial.println("Moving Forward");
        digitalWrite(motor1Pin1, HIGH);
        digitalWrite(motor1Pin2, LOW);
        delay(2000);

        // Stop the DC motor
        Serial.println("Motor stopped");
        digitalWrite(motor1Pin1, LOW);
        digitalWrite(motor1Pin2, LOW);
        delay(1000);
      } else if(var1.toInt() == 26 && var2.toInt() == 0) {
        //close the door
        Serial.println("button un-clicked Close the door: DOOR IS CLOSED");
        output26State = false;
        digitalWrite(LEDPIN, LOW);
        // Move DC motor backwards at maximum speed
        Serial.println("Moving Backwards");
        digitalWrite(motor1Pin1, LOW);
        digitalWrite(motor1Pin2, HIGH);
        delay(2000);

        // Stop the DC motor
        Serial.println("Motor stopped");
        digitalWrite(motor1Pin1, LOW);
        digitalWrite(motor1Pin2, LOW);
        delay(1000);
      }
    } else {
      var1 = "Either nothing sent or invalid input";
      var2 = "Either nothing sent or invalid input";
    }
    request->send(200, "text/plain", "OK");
  });
  //webServer.on( "/favicon.ico", handleFavicon );
  //webServer.on( "/power", handlePower);
  //webServer.on( "/program", handleProgram);
  //webServer.onNotFound( handleNotFound );
  webServer.begin();

  Serial.println("Configuring OTA");
  ArduinoOTA.setHostname("chamberdoor");
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Setup of OTA complete");

  if ( MDNS.begin ( "chamberdoor" ) ) {
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Failed setting up mDNS responder!");
  }
  MDNS.addService("http", "tcp", 80);

  // init and get time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);


  if(!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time from NTP server");
  } else {
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    epochTime = mktime(&timeinfo);
    epochTimeOffset = (millis()/1000);
    espTime = epochTime;
  }


}

void loop() {
  ArduinoOTA.handle();                      // Listen for OTA requests
  // update "clock"
  if(millis() % 1000) {
    espTime = epochTime-epochTimeOffset+(millis()/1000);
    timeinfo = *localtime(&espTime);
  }
  unsigned long currentMillis = millis();
  if((unsigned long)(currentMillis - previousTime) >= timeoutTime) {
    // lets update from NTP once a day.
    epochTime = getLocalTime(&timeinfo);
    epochTimeOffset = (millis()/1000);
    espTime = epochTime;
    previousTime = currentMillis;
  }
  if((unsigned long)(currentMillis - previousTime2) >= 15000 ) {
    float temp(NAN);
    float hum(NAN);
    float pres(NAN);
    BME280::TempUnit tempUnit(BME280::TempUnit_Fahrenheit);
    BME280::PresUnit presUnit(BME280::PresUnit_inHg);
    bme.read(pres, temp, hum, tempUnit, presUnit);
    bme.read(pres, temp, hum, tempUnit, presUnit);
    lastTemp = temp;
    lastHum = hum;
    lastPres = pres;
    previousTime2 = currentMillis;
  }

  if(timeinfo.tm_hour == 5 && timeinfo.tm_min == 30 && timeinfo.tm_sec == 0) {
    if(!output26State) {  // output26state is false.  true = open, false = closed
      // door is closed, let's open it.
      Serial.println("5:30AM, Open the pod bay door, HAL. DOOR IS OPEN");
      output26State = true;
      digitalWrite(LEDPIN, HIGH);
      // Move the DC motor forward at maximum speed
      Serial.println("Moving Forward");
      digitalWrite(motor1Pin1, HIGH);
      digitalWrite(motor1Pin2, LOW);
      delay(2000);

      // Stop the DC motor
      Serial.println("Motor stopped");
      digitalWrite(motor1Pin1, LOW);
      digitalWrite(motor1Pin2, LOW);
      delay(1000);
    }
  } else if(timeinfo.tm_hour == 21 && timeinfo.tm_min == 0 && timeinfo.tm_sec == 0) {
    if(output26State) { // output26state is true.  true = open, false = closed
      //door is open, it's late, close the door
      Serial.println("9:00 PM Close the door: DOOR IS CLOSED");
      output26State = false;
      digitalWrite(LEDPIN, LOW);
      // Move DC motor backwards at maximum speed
      Serial.println("Moving Backwards");
      digitalWrite(motor1Pin1, LOW);
      digitalWrite(motor1Pin2, HIGH);
      delay(2000);

      // Stop the DC motor
      Serial.println("Motor stopped");
      digitalWrite(motor1Pin1, LOW);
      digitalWrite(motor1Pin2, LOW);
      delay(1000);
    }
  }
}
