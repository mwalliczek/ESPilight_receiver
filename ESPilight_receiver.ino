/*
 ESPilight receiver
 
Copyright 2021 Matthias Walliczek

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
 
 Based on the "Basic ESPilight receive example" at https://github.com/puuu/espilight
*/

#include <ESPiLight.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "DHTesp.h"

#define RECEIVER_PIN D2  // any intterupt able pin
#define TRANSMITTER_PIN -1

const char* ssid = "example";
const char* password =  "password";

/** Initialize DHT sensor */
DHTesp dht;

ESPiLight rf(TRANSMITTER_PIN);  // use -1 to disable transmitter

WiFiClient client;

// callback function. It is called on successfully received and parsed rc signal
void rfCallback(const String &protocol, const String &message, int status,
                size_t repeats, const String &deviceID) {
  // check if message is valid and process it
  Serial.print("RF signal arrived [");
  Serial.print(protocol);  // protocoll used to parse
  Serial.print("][");
  Serial.print(deviceID);  // value of id key in json message
  Serial.print("] (");
  Serial.print(status);  // status of message, depending on repeat, either:
                         // FIRST   - first message of this protocoll within the
                         //           last 0.5 s
                         // INVALID - message repeat is not equal to the
                         //           previous message
                         // VALID   - message is equal to the previous message
                         // KNOWN   - repeat of a already valid message
  Serial.print(") ");
  Serial.print(message);  // message in json format
  Serial.println();
  if (status == VALID) {
    Serial.print("Valid message: [");
    Serial.print(protocol);
    Serial.print("] ");
    Serial.print(message);
    Serial.println();
    if (client.connected()) {
      client.printf("{\"message\": %s,\"origin\":\"receiver\",\"protocol\":\"%s\",\"uuid\":\"%s\",\"action\":\"update\"}\n\n", message.c_str(), protocol.c_str(), WiFi.macAddress().c_str());
    }
  }
}

void connecting() {
  int general_count = 0;
  while (!client.connected()) {
    Serial.println("Start connection");
    if (++general_count == 10) {
      Serial.println("Could not connect, reset");
      delay(60000);
      ESP.restart();
    }
    delay(500);
    int wlan_count = 0;
    while (WiFi.status() != WL_CONNECTED) {
      if (++wlan_count == 100) {
        Serial.println("Could not connect to wlan, reset");
        ESP.restart();
      }
      delay(500);
      Serial.println("Connecting to WiFi..");
    }
    Serial.println("Connected to the WiFi network");
  
    WiFiUDP udp;
    udp.begin(0);
    udp.beginPacket("239.255.255.250", 1900);
    udp.write("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nST: {urn:schemas-upnp-org:service:pilight:1}\r\nMX: 3\r\n\r\n");
    udp.endPacket();
    Serial.println("Send discovery");
    
    int packetSize;
    int count = 0;
    while ((packetSize = udp.parsePacket()) == 0) { 
      Serial.println("Waiting for response");
      delay(100);
      if (++count == 100) {
        break;
      }
    }
    if (count == 100) {
      Serial.println("No response");
      continue;
    }
  
    // receive incoming UDP packets
    Serial.printf("Received %d bytes from %s, port %d\n", packetSize, udp.remoteIP().toString().c_str(), udp.remotePort());
    char incomingPacket[2048];  // buffer for incoming packets
    int len = udp.read(incomingPacket, 2048);
    if (len > 0) {
      incomingPacket[len] = 0;
    }
    Serial.printf("UDP packet contents: %s\n", incomingPacket);
    char *location = strstr(incomingPacket, "Location:");
    if (location) {
      location += 9;
      while (*location == ' ') {
          location++;
      }
      char *portPos = strstr(location, ":");
      if (portPos) {
        char host[128];
        strncpy(host, location, portPos - location);
        host[portPos - location] = '\0';
        int port = atoi(&portPos[1]);
        Serial.printf("\nLocation: %s:%d\n", host, port);
        Serial.printf("Connecting to %s ... ", host);
        client.connect(host, port);
        Serial.println("connected");
      
        client.printf("{\"action\": \"identify\", \"options\": {\"core\": 1, \"stats\": 1, \"receiver\": 0, \"config\": 0, \"forward\": 0}, \"uuid\": \"%s\"}\n\n", WiFi.macAddress().c_str());
        count = 0;
        while (client.connected() && !client.available()) { 
          delay(100);
          if (++count == 1000) {
            break;
          }
        }
        if (count == 1000) {
          Serial.println("No response");
          client.stop();
          delay(60000);
          continue;
        }
        while (client.available()) {
          String line = client.readStringUntil('\n');
          Serial.print("Received response: ");
          Serial.println(line);
          yield();
        }
        Serial.print("Client connected: ");
        Serial.println(client.connected());
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(4000);
  WiFi.begin(ssid, password);
  Serial.println();
  Serial.println("setup");

  // Initialize temperature sensor
  dht.setup(D3, DHTesp::DHT22);

  connecting();

  // set callback funktion
  rf.setCallback(rfCallback);
  // inittilize receiver
  rf.initReceiver(RECEIVER_PIN);
}

int lastTemp = 0;

void loop() {
  connecting();
  if (millis() - lastTemp > 60000) {
    Serial.println("Free HEAP: " + String(ESP.getFreeHeap()));
    Serial.println("Read DHT22");
    TempAndHumidity newValues = dht.getTempAndHumidity();
    if (dht.getStatus() != 0) {
      const char* status = dht.getStatusString();
      Serial.println("DHT22 error status: " + String(status));
    } else {
      lastTemp = millis();
      char messagebuffer[1024];
      snprintf(messagebuffer, 1024, "{\"message\": {\"gpio\":3,\"temperature\":%.2f,\"humidity\":%.2f},\"origin\":\"receiver\",\"protocol\":\"dht22\",\"uuid\":\"%s\",\"action\":\"update\"}\n\n", newValues.temperature, newValues.humidity, WiFi.macAddress().c_str());
      Serial.printf("Sending %s", messagebuffer);
      client.print(messagebuffer);
      yield();
    }
  }
  if (client.available())  {
    Serial.println("client available");
    String line = client.readStringUntil('\n');
    Serial.print("Received response: ");
    Serial.println(line);
  }
  // process input queue and may fire calllback
  rf.loop();
  delay(10);
}
