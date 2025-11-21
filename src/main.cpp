#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>

// --- CONFIGURATION LORA ---
#define DEVICE_ID           0x77 
#define LORA_FREQUENCY      433E6
#define LORA_SF             12
#define LORA_BW             125E3
#define LORA_CR             8
#define LORA_TX_POWER       20
#define LORA_SYNC_WORD      0xF3

#define CMD_RELAY_ON        0xA1
#define CMD_RELAY_OFF       0xB2
#define CMD_GET_STATUS      0xC3
#define ACK_RELAY_IS_ON     0xD4
#define ACK_RELAY_IS_OFF    0xE5

// --- PINOUT XIAO ESP32C6 (Headers Latéraux) ---
#define PIN_LORA_RST    D0
#define PIN_LORA_NSS    D1
#define PIN_LORA_DIO0   D2
#define PIN_BUTTON      D3
#define PIN_LORA_MOSI   D4
#define PIN_LORA_MISO   D5
#define PIN_LORA_SCK    D6
#define PIN_LED         LED_BUILTIN
#define PIN_BATTERY     A0 

// --- VARIABLES ---
WebServer server(80);
volatile unsigned long lastActivityTime = 0;
String webStatus = "EN ATTENTE...";

// --- PAGE WEB ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>Remote</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{font-family:sans-serif;background:#121212;color:#eee;text-align:center;padding:20px}
.btn{width:100%;padding:20px;margin:10px 0;font-size:20px;border:none;border-radius:5px;color:#fff;cursor:pointer}
.on{background:#27ae60}.off{background:#c0392b}.ref{background:#2980b9}</style>
</head><body><h1>CONTROLE TERRAIN</h1><p>Status: <b>%STATUS%</b></p><p>Batterie: <b>%BATTERY%</b></p>
<a href="/on"><button class="btn on">ALLUMER</button></a><a href="/off"><button class="btn off">ETEINDRE</button></a>
<a href="/status"><button class="btn ref">ACTUALISER</button></a></body></html>)rawliteral";

// --- FONCTIONS ---
void ledSignal(int count, int ms) {
    for(int i=0; i<count; i++) { digitalWrite(PIN_LED, LOW); delay(ms); digitalWrite(PIN_LED, HIGH); delay(ms); }
}

void goToDeepSleep() {
    LoRa.sleep(); WiFi.mode(WIFI_OFF);
    esp_deep_sleep_enable_gpio_wakeup(BIT(D3), ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

String getBattery() { return "Non Cable"; }

void initLoRa() {
    // IMPORTANT : On force les pins SPI latérales ici
    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
    LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);
    
    if (!LoRa.begin(LORA_FREQUENCY)) { ledSignal(10, 50); goToDeepSleep(); }
    
    LoRa.setSpreadingFactor(LORA_SF); LoRa.setSignalBandwidth(LORA_BW);
    LoRa.setCodingRate4(LORA_CR); LoRa.setSyncWord(LORA_SYNC_WORD); LoRa.setTxPower(LORA_TX_POWER);
}

bool sendCmd(uint8_t cmd) {
    for(int i=0; i<2; i++) {
        LoRa.beginPacket(); LoRa.write(DEVICE_ID); LoRa.write(cmd); LoRa.endPacket(); delay(20);
    }
    unsigned long s = millis();
    while(millis() - s < 4000) {
        if(LoRa.parsePacket() == 2) {
            uint8_t id = LoRa.read(); uint8_t r = LoRa.read();
            if(id == DEVICE_ID) {
                if(r == ACK_RELAY_IS_ON) { webStatus = "ALLUME"; ledSignal(2, 200); return true; }
                if(r == ACK_RELAY_IS_OFF) { webStatus = "ETEINT"; ledSignal(3, 200); return true; }
            }
        }
    }
    webStatus = "ERREUR: Pas de Reponse"; ledSignal(5, 50); return false;
}

void setup() {
    pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, HIGH);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    
    // Le Serial peut mettre du temps à monter sur le C6
    Serial.begin(115200);
    
    unsigned long start = millis();

    // MODE MAINTENANCE WIFI (Appui > 5s)
    while(digitalRead(PIN_BUTTON) == LOW) {
        if(millis() - start > 5000) {
            ledSignal(1, 1000); initLoRa();
            WiFi.softAP("TELECOMMANDE_SPORT", "admin1234");
            server.on("/", [](){ String s=index_html; s.replace("%STATUS%", webStatus); s.replace("%BATTERY%", getBattery()); server.send(200, "text/html", s); lastActivityTime=millis(); });
            server.on("/on", [](){ sendCmd(CMD_RELAY_ON); server.sendHeader("Location","/"); server.send(303); lastActivityTime=millis(); });
            server.on("/off", [](){ sendCmd(CMD_RELAY_OFF); server.sendHeader("Location","/"); server.send(303); lastActivityTime=millis(); });
            server.on("/status", [](){ sendCmd(CMD_GET_STATUS); server.sendHeader("Location","/"); server.send(303); lastActivityTime=millis(); });
            server.begin(); sendCmd(CMD_GET_STATUS); lastActivityTime = millis();
            while(true) { server.handleClient(); if(millis()-lastActivityTime > 300000) goToDeepSleep(); delay(5); }
        } delay(10);
    }

    // MODE PHYSIQUE
    if(millis() - start > 50) {
        initLoRa();
        if(millis() - start > 1500) sendCmd(CMD_RELAY_OFF);
        else sendCmd(CMD_RELAY_ON);
    }
    goToDeepSleep();
}

void loop() {}