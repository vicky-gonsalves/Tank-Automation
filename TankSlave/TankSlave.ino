#include <Wire.h>
#include <ArduinoJson.h>

#define I2C_ADDRESS_OTHER 1
#define I2C_ADDRESS_ME 9

const int GREEN_LED = 2;
const int RED_LED = 3;
const int RELAY = 4;

const int BLUE_LED1 = 5;
const int BLUE_LED2 = 6;
const int BLUE_LED3 = 7;
const int BLUE_LED4 = 8;
const int BLUE_LED5 = 9;
const int ORANGE_LED = 10;

void setup() {
  Wire.begin(I2C_ADDRESS_ME);   // join i2c bus
  Wire.onReceive(receiveI2C); // register event
  Serial.begin(9600);           // start serial for output
  Serial.println("Slave");
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED1, OUTPUT);
  pinMode(BLUE_LED2, OUTPUT);
  pinMode(BLUE_LED3, OUTPUT);
  pinMode(BLUE_LED4, OUTPUT);
  pinMode(BLUE_LED5, OUTPUT);
  pinMode(ORANGE_LED, OUTPUT);
  pinMode(RELAY, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(BLUE_LED1, LOW);
  digitalWrite(BLUE_LED2, LOW);
  digitalWrite(BLUE_LED3, LOW);
  digitalWrite(BLUE_LED4, LOW);
  digitalWrite(BLUE_LED5, LOW);
  digitalWrite(ORANGE_LED, LOW);
  digitalWrite(RELAY, HIGH);
}

void loop() {
  delay(100);
}

void receiveI2C(int howMany) {
  String response = "";
  while (Wire.available() > 0) {
    char c = Wire.read();
    response += c;
  }
  Serial.println(response);
  processData(response);
}

void processData(String data) {
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, data);
  if (error) { 
    return;
  }
  String status = doc["m"];
  int level = doc["l"];
  String websocketStatus = doc["w"];
  
  if (websocketStatus == "c") {
    digitalWrite(ORANGE_LED, LOW);
  } else if (websocketStatus == "d") {
    digitalWrite(ORANGE_LED, HIGH);
  }
  
  if (status == "on") {
    motorToggle(true);
  } else if (status == "off") {
    motorToggle(false);
  }

  if (level >= 20) {
    digitalWrite(BLUE_LED1, HIGH);
  } else {
    digitalWrite(BLUE_LED1, LOW);
  }


  if (level >= 40) {
    digitalWrite(BLUE_LED2, HIGH);
  } else {
    digitalWrite(BLUE_LED2, LOW);
  }


  if (level >= 60) {
    digitalWrite(BLUE_LED3, HIGH);
  } else {
    digitalWrite(BLUE_LED3, LOW);
  }

  if (level >=  80) {
    digitalWrite(BLUE_LED4, HIGH);
  } else {
    digitalWrite(BLUE_LED4, LOW);
  }


  if (level >= 95) {
    digitalWrite(BLUE_LED5, HIGH);
  } else {
    digitalWrite(BLUE_LED5, LOW);
  }
}

void motorToggle(bool flag) {
  if (flag) {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, LOW);
    digitalWrite(RELAY, LOW);
  } else {
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RELAY, HIGH);
  }
}


void transmit(String message) {
  Wire.beginTransmission(I2C_ADDRESS_OTHER);
  Wire.write(message.c_str());
  Wire.endTransmission();
}
