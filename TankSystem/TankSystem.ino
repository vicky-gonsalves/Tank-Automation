#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

/*----------------Custom Init----------------------*/
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SocketIoClient.h>
#include <Wire.h>
#include <NTPClient.h>
/*-------------------------------------------------*/

#ifndef STASSID
#define STASSID "XXXX"
#define STAPSK  "XXXX"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;


/*----------------Custom Init----------------------*/
#define I2C_ADDRESS_OTHER 9
#define I2C_ADDRESS_ME 1

long duration, cm, inches, totalTankfilledAtStart;
String lastStatus, websocketStatus;
uint32_t ts1, loopMillis, ts2, pingTime; //millis
const int trigPin = 12;
const int echoPin = 13;
const int totalTankHeight = 49;
const int cutOff = 30;  //Minutes
const int noWaterCutoff = 2; //minutes
long totalHeightFilled = 0;
long totalTankfilled = 0;
long cutOffStarted = 0;
int tankHeightOffset = 3;
bool motorOn = false;
bool automate = false;
bool isCutoff = false;
int cutOffRelease = 60; //Minutes
uint32_t oneMin = 60000; //millis
uint32_t ms1 = 0;
bool skipCutoff = false;
int quietHourStart = 19;
int quietHourEnd = 2;
int confirmMotorOnCounter = 0;
int confirmMotorOffCounter = 0;
int confirmCutoffOffCounter = 0;
int heightArray[20];
int tankFillArray[20];
int loopCount = 0;

SocketIoClient webSocket;
WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP, "time.nist.gov", 3600, 60000);
/*-------------------------------------------------*/

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("TankSystem");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

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
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  /*----------------Custom Setup----------------------*/
  Wire.begin();
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT); // Sets the echoPin as an Input

  ts1 = millis();
  loopMillis = millis();
  webSocket.on("connect", connectedEV);
  webSocket.on("disconnect", disconnected);
  webSocket.on("welcome", event);
  webSocket.on("get-status:init", event);
  webSocket.on("get-status:save", event);
  webSocket.on("get-status:remove", event);
  webSocket.begin("somedomain.com");
  // use HTTP Basic Authorization this is optional remove if not needed
  webSocket.setAuthorization("tank00000000001", "password123");
  timeClient.begin();
  /*----------------Custom Setup----------------------*/
}

void loop() {
  ts2 = millis();
  ArduinoOTA.handle();
  webSocket.loop();
  timeClient.update();
  /*----------------Custom Loop----------------------*/
  if ((ts2 - loopMillis) >= 250) {
    int thisHour = (int)(timeClient.getHours());
    if (thisHour < quietHourStart && thisHour >= quietHourEnd) {
      Serial.println("Not Quiet hour");
    } else {
      Serial.println("Quiet hour");
    }
    // Clears the trigPin
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    // Sets the trigPin on HIGH state for 10 micro seconds
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    // Convert the time into a distance
    duration = pulseIn(echoPin, HIGH);
    processData(duration);
    loopMillis = millis();
  }
  /*----------------Custom Loop----------------------*/
}

void cutOffMotor() {
  ms1 = 0;
  cutOffStarted = millis();
  isCutoff = true;
  emitMotorToggle(false);
  transmit("{\"w\":\"d\"}");  //Hack: Turn on Orange LED
}


void releaseCutOffMotor() {
  cutOffStarted = 0;
  isCutoff = false;
  totalTankfilledAtStart = 0;
  transmit("{\"w\":\"c\"}");  //Hack: Turn off Orange LED
  char buf[50];
  sprintf(buf, "{\"cutOff\":%d,\"updatedByDevice\":true}", isCutoff);
  webSocket.emit("get-status:put", buf);
}

void emitTankFillStatus() {
  char buf[150];
  if (isCutoff) {
    sprintf(buf, "{\"motor\":\"off\",\"tankFilled\":%lu, \"waterHeight\":%lu,\"websocket\":\"connected\", \"cutOff\":%d, \"updatedByDevice\":true}", totalTankfilled, totalHeightFilled, isCutoff);
  } else {
    sprintf(buf, "{\"tankFilled\":%lu, \"waterHeight\":%lu,\"websocket\":\"connected\", \"cutOff\":%d, \"updatedByDevice\":true}", totalTankfilled, totalHeightFilled, isCutoff);
  }
  webSocket.emit("get-status:put", buf);
  ts1 = millis();
}

void event(const char * payload, size_t length) {
  Serial.printf("got message: %s\n", payload);
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    return;
  }
  String status = doc["motor"];
  skipCutoff = doc["skipCutoff"];
  automate = doc["automate"];
  lastStatus = status;
  bool updatedByDevice = doc["updatedByDevice"];
  if (!updatedByDevice) {
    if (ms1 == 0 && skipCutoff != doc["skipCutoff"] && skipCutoff == false) {
      ms1 = millis();
    } else if (skipCutoff != doc["skipCutoff"] && skipCutoff == true) {
      ms1 = 0;
    }
    if (status == "on" && !motorOn) {
      motorToggle(true);
    } else if (status == "off" && motorOn) {
      motorToggle(false);
    }
  }
}

void processData(long d) {
  if (loopCount < 20) {
    cm = (duration / 2) / 29.1;   // Divide by 29.1 or multiply by 0.0343
    inches = (duration / 2) / 74; // Divide by 74 or multiply by 0.0135
    Serial.print(inches);
    Serial.print("in, ");
    Serial.print(cm);
    Serial.print("cm");
    Serial.println();
    tankFillArray[loopCount] = (totalTankHeight - (inches - tankHeightOffset)) * 100 / totalTankHeight;
    heightArray[loopCount] = ((totalTankHeight * 2.54) - (cm - (tankHeightOffset * 2.54)));
    loopCount++;
  } else {
    loopCount = 0;
    totalTankfilled = mostFrequent(tankFillArray, 20);
    totalHeightFilled = mostFrequent(heightArray, 20);
    transmit("{\"l\":" + (String)totalTankfilled + ", \"m\":\"" + lastStatus + "\",\"w\":\"" + websocketStatus + "\"}");
    int thisHour = (int)(timeClient.getHours());

    if (automate && totalTankfilled > 0) {
      if (totalTankfilled <= 70  && (thisHour < quietHourStart && thisHour >= quietHourEnd)) {
        confirmMotorOnCounter++;
        emitMotorToggle(true);
      } else if (totalTankfilled >= 100 && totalTankfilled < 110) {
        confirmMotorOffCounter++;
        emitMotorToggle(false);
      } else {
        confirmMotorOnCounter = 0;
        confirmMotorOffCounter = 0;
      }
    } else if (automate && totalTankfilled < 0) {
      emitMotorToggle(false);
    }


    if (motorOn == true) {
      pingTime = 5000;
    } else {
      pingTime = 5000;
    }

    //Emit tank water level
    if ((ts2 - ts1) >= pingTime && totalTankfilled > -1) {
      emitTankFillStatus();
    }

    //Release Cutoff
    if (isCutoff && (ts2 - cutOffStarted) >= (oneMin * cutOffRelease)) {
      releaseCutOffMotor();
    }

    //  if (!skipCutoff) {
    //    if (motorOn && ms1 > 0) {
    //      //Cutoff motor if running for more than cutoff time
    //      if (!isCutoff && ((ts2 - ms1) >= (oneMin * cutOff))) {
    //        cutOffMotor();
    //      }
    //

    //Cutoff motor if running for more than noWaterCutoff time and no raise in water level
    if (!isCutoff && (totalHeightFilled > -1) && ((ts2 - ms1) >= (oneMin * noWaterCutoff))) {
      if ((totalHeightFilled - totalTankfilledAtStart) > 0 && (totalHeightFilled - totalTankfilledAtStart) <= 2) {
        confirmCutoffOffCounter++;
        if (confirmCutoffOffCounter >= 10) {
          confirmCutoffOffCounter = 0;
          confirmMotorOffCounter = 10;
          cutOffMotor();
        }
      } else {
        totalTankfilledAtStart = totalHeightFilled;
      }
    }

    //    }
    //  }
    //
  }
}

void motorToggle(bool flag) {
  if (flag && !motorOn && !isCutoff) {
    motorOn = true;
    if (ms1 == 0 && !skipCutoff) {
      ms1 = millis();
    }
    totalTankfilledAtStart = totalHeightFilled;
    transmit("{\"m\":\"on\"}");
    sendLog("motor on", "true");
  } else if (!flag && motorOn) {
    if (!isCutoff && !skipCutoff)  {
      ms1 = 0;
    }
    motorOn = false;
    transmit("{\"m\":\"off\"}");
    sendLog("motor off", "true");
  }
}

void emitMotorToggle(bool flag) {
  if (flag && !motorOn && !isCutoff && confirmMotorOnCounter >= 10) {
    confirmMotorOnCounter = 0;
    motorToggle(true);
    webSocket.emit("get-status:put", "{\"motor\":\"on\", \"updatedByDevice\":true}");
  } else if (!flag && motorOn && confirmMotorOffCounter >= 10) {
    confirmMotorOffCounter = 0;
    motorToggle(false);
    char buf[50];
    sprintf(buf, "{\"motor\":\"off\",\"cutOff\":%d,\"updatedByDevice\":true}", isCutoff);
    webSocket.emit("get-status:put", buf);
    emitTankFillStatus();
  }
}

void sendLog(char * action, char * wStatus) {
  char buf[200];
  sprintf(buf, "{\"action\":\"%s\", \"motorOn\":%d,\"cutOff\":%d,\"automate\":%d,\"tankFilled\":%lu,\"waterHeight\":%lu,\"websocket\":\"%s\", \"skipCutoff\":%d, \"updatedByDevice\":1}" , action, motorOn, isCutoff, automate, totalTankfilled , totalHeightFilled, wStatus, skipCutoff);
  webSocket.emit("log:save", buf);
}

void transmit(String message) {
  Wire.beginTransmission(I2C_ADDRESS_OTHER);
  Wire.write(message.c_str());
  Wire.endTransmission();
}

void disconnected(const char * payload, size_t length) {
  motorToggle(false);
  websocketStatus = "d";
  webSocket.emit("get-status:put", "{\"websocket\":\"disconnected\", \"updatedByDevice\":true}");
  delay(oneMin);
  ESP.restart();
}


void connectedEV(const char * payload, size_t length) {
  websocketStatus = "c";
  webSocket.emit("get-status:put", "{\"websocket\":\"connected\",\"motor\":\"off\", \"updatedByDevice\":true}");
  emitTankFillStatus();
  transmit("{\"w\":\"c\",\"m\":\"off\"}");
}

int mostFrequent(int arr[], int n)
{
  // Sort the array
  sort(arr, n);

  // find the max frequency using linear traversal
  int max_count = 1, res = arr[0], curr_count = 1;
  for (int i = 1; i < n; i++) {
    if (arr[i] == arr[i - 1])
      curr_count++;
    else {
      if (curr_count > max_count) {
        max_count = curr_count;
        res = arr[i - 1];
      }
      curr_count = 1;
    }
  }

  // If last element is most frequent
  if (curr_count > max_count)
  {
    max_count = curr_count;
    res = arr[n - 1];
  }

  return res;
}

void sort(int a[], int size) {
  for (int i = 0; i < (size - 1); i++) {
    for (int o = 0; o < (size - (i + 1)); o++) {
      if (a[o] > a[o + 1]) {
        int t = a[o];
        a[o] = a[o + 1];
        a[o + 1] = t;
      }
    }
  }
}
