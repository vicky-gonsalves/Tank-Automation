#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

/*----------------Custom Init----------------------*/
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SocketIoClient.h>
#include <Wire.h>
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

const int trigPin = 12;
const int echoPin = 13;
const int totalTankHeight = 49;
long totalHeightFilled = 0;
long totalTankfilled = 0;
int tankHeightOffset = 3;
bool motorOn = false;
bool automate = false;
long duration, cm, inches;
String lastStatus;
uint32_t ts1;
String websocketStatus;
long ms1 = 0;
long cutOffStarted = 0;
int cutOff = 30;  //Minutes
int noWaterCutoff = 2; //minutes
int pingTime; //millis
bool isCutoff = false;
int cutOffRelease = 60; //Minutes
long totalTankfilledAtStart;
long oneMin = 60000; //millis

SocketIoClient webSocket;
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
  // ArduinoOTA.setHostname("myesp8266");

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
  webSocket.on("connect", connectedEV);
  webSocket.on("disconnect", disconnected);
  webSocket.on("welcome", event);
  webSocket.on("get-status:init", event);
  webSocket.on("get-status:save", event);
  webSocket.on("get-status:remove", event);
  webSocket.begin("somedomain.com");
  // use HTTP Basic Authorization this is optional remove if not needed
  //    webSocket.setAuthorization("username", "password");
  /*----------------Custom Setup----------------------*/
}

void loop() {
  ArduinoOTA.handle();

  /*----------------Custom Loop----------------------*/
  webSocket.loop();
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

  uint32_t ts2 = millis();


  if (motorOn == true) {
    pingTime = 5000;
  } else {
    pingTime = 60000;
  }

  //Emit tank water level
  if ((ts2 - ts1) >= pingTime && totalTankfilled > -1) {
    emitTankFillStatus();
  }


  if (motorOn && ms1 > 0) {
    //Cutoff motor if running for more than cutoff time
    if (!isCutoff && ((millis() - ms1) >= (oneMin * cutOff))) {
      cutOffMotor();
    }

    //Cutoff motor if running for more than noWaterCutoff time and no raise in water level
    if (!isCutoff && (totalTankfilled > -1) && ((millis() - ms1) >= (oneMin * noWaterCutoff))  && (totalTankfilled - totalTankfilledAtStart) <= 2) {
      cutOffMotor();
    }
  }

  //Release Cutoff
  if (isCutoff && (ts2 - cutOffStarted) >= (oneMin * cutOffRelease)) {
    releaseCutOffMotor();
  }

  delay(250);
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
}

void emitTankFillStatus() {
  char buf[32];
  sprintf(buf, "{\"tankFilled\":%lu, \"waterHeight\":%lu}", totalTankfilled, totalHeightFilled);
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
  lastStatus = status;
  automate = doc["automate"];
  if (status == "on" && !motorOn) {
    motorToggle(true);
  } else if (status == "off" && motorOn) {
    motorToggle(false);
  }
}

void processData(long d) {
  cm = (duration / 2) / 29.1;   // Divide by 29.1 or multiply by 0.0343
  inches = (duration / 2) / 74; // Divide by 74 or multiply by 0.0135
  Serial.print(inches);
  Serial.print("in, ");
  Serial.print(cm);
  Serial.print("cm");
  Serial.println();
  totalTankfilled = (totalTankHeight - (inches - tankHeightOffset)) * 100 / totalTankHeight;
  totalHeightFilled = 2.54 * (totalTankHeight - (inches - tankHeightOffset));
  Serial.println(totalTankfilled);
  transmit("{\"l\":" + (String)totalTankfilled + ", \"m\":\"" + lastStatus + "\",\"w\":\"" + websocketStatus + "\"}");

  if (automate && totalTankfilled > 0) {
    if (totalTankfilled <= 70) {
      emitMotorToggle(true);
    }

    if (totalTankfilled >= 100) {
      emitMotorToggle(false);
      emitTankFillStatus();
    }
  } else if (automate && totalTankfilled < 0) {
    emitMotorToggle(false);
  }
}

void motorToggle(bool flag) {
  if (flag && !isCutoff) {
    motorOn = true;
    if (ms1 == 0) {
      ms1 = millis();
    }
    totalTankfilledAtStart = totalTankfilled;
    transmit("{\"motor\":\"on\"}");
  } else {
    motorOn = false;
    transmit("{\"motor\":\"off\"}");
  }
}

void emitMotorToggle(bool flag) {
  if (flag && !motorOn && !isCutoff) {
    motorToggle(true);
    webSocket.emit("get-status:put", "{\"motor\":\"on\"}");
  } else if (!flag && motorOn) {
    motorToggle(false);
    webSocket.emit("get-status:put", "{\"motor\":\"off\"}");
  }
}

void transmit(String message) {
  Wire.beginTransmission(I2C_ADDRESS_OTHER);
  Wire.write(message.c_str());
  Wire.endTransmission();
}

void processSlaveData(String data) {
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, data);
  if (error) {
    return;
  }
  String action = doc["action"];
  if (action == "init") {
    motorOn = false;
    webSocket.emit("get-status:put", "{\"motor\":\"off\"}");
  }
}


void disconnected(const char * payload, size_t length) {
  websocketStatus = "d";
  webSocket.emit("get-status:put", "{\"websocket\":\"disconnected\"}");
  delay(oneMin);
  ESP.restart();
}


void connectedEV(const char * payload, size_t length) {
  websocketStatus = "c";
  webSocket.emit("get-status:put", "{\"websocket\":\"connected\",\"motor\":\"off\"}");
  emitTankFillStatus();
  transmit("{\"w\":\"c\",\"motor\":\"off\"}");
}


void receiveI2C(int howMany) {
  String response = "";
  while (Wire.available() > 0) {
    char c = Wire.read();
    response += c;
  }
  Serial.println(response);
}
