#include <M5Atom.h>
#include <Wire.h>
#include <MFRC522_I2C.h> // Nowa biblioteka
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"

// --- KONFIGURACJA MAPOWANIA ---
struct ProjectMapping {
    String uid;
    unsigned long projectId;
    String name;
};

// TU WPISZ SWOJE TAGI
ProjectMapping knownTags[] = {
    {"", 000000, "DEV"},   
    {"", 000000, "CALL"},
    {"", 000000, "PRIV"} 
};
const int tagsCount = sizeof(knownTags) / sizeof(knownTags[0]);

// --- OBIEKTY ---
// Adres I2C dla M5Stack Unit RFID to zazwyczaj 0x28
MFRC522_I2C rfid(0x28, -1); 

// --- POMOCNICZE: Czas NTP ---
String getISOTime() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)) return "";
    char timeStringBuff[30];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(timeStringBuff);
}

// --- KOMUNIKACJA Z TOGGLEM ---
void sendTogglRequest(unsigned long projectId, String description) {
    if(WiFi.status() != WL_CONNECTED) {
        M5.dis.drawpix(0, 0xFF0000); // Czerwony
        return;
    }

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); 

    String url = "https://api.track.toggl.com/api/v9/time_entries";
    
    Serial.printf(">> Startuje timer: %s...\n", description.c_str());
    M5.dis.drawpix(0, 0x0000FF); // Niebieski

    if (http.begin(client, url)) {
        http.setAuthorization(TOGGL_TOKEN, "api_token");
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<300> doc;
        doc["created_with"] = "M5Atom_Cube";
        doc["description"] = description;
        doc["tags"] = JsonArray();
        doc["billable"] = false;
        doc["workspace_id"] = atol(WORKSPACE_ID);
        doc["project_id"] = projectId;
        doc["duration"] = -1;
        doc["start"] = getISOTime();
        doc["stop"] = nullptr;

        String requestBody;
        serializeJson(doc, requestBody);

        int httpCode = http.POST(requestBody);

        if (httpCode == 200) {
            Serial.printf(">> SUKCES! (Kod 200)\n");
            M5.dis.drawpix(0, 0xFFFFFF); // BIAŁY
            delay(1000);
            M5.dis.drawpix(0, 0x00FF00); 
        } else {
            Serial.printf(">> BLAD HTTP: %d\n", httpCode);
            Serial.println(http.getString());
            M5.dis.drawpix(0, 0xFF0000); 
            delay(2000);
            M5.dis.drawpix(0, 0x00FF00);
        }
        http.end();
    }
}

void setup() {
    M5.begin(true, false, true);
    Serial.begin(115200);
    
    // Inicjalizacja I2C dla Atom Lite (Grove Port)
    // SDA = 26, SCL = 32
    Wire.begin(26, 32); 
    rfid.begin(); // Init RFID

    // WiFi
    Serial.print("Laczenie z WiFi: ");
    Serial.println(WIFI_SSID);
    M5.dis.drawpix(0, 0x000055); 
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nPolaczono!");

    // Czas NTP
    configTime(0, 0, "pool.ntp.org", "time.nist.gov"); 
    Serial.print("Synchronizacja czasu");
    struct tm timeinfo;
    while(!getLocalTime(&timeinfo)){
        Serial.print(".");
        delay(500);
    }
    Serial.println("\nCzas OK!");
    
    M5.dis.drawpix(0, 0x00FF00); // ZIELONY = GOTOWY
}

void loop() {
    M5.update();

    // Sprawdzenie czy jest nowa karta
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        String uid = "";
        for (byte i = 0; i < rfid.uid.size; i++) {
            if(rfid.uid.uidByte[i] < 0x10) uid += "0";
            uid += String(rfid.uid.uidByte[i], HEX);
        }
        
        Serial.print(">>> Wykryto TAG UID: ");
        Serial.println(uid);

        bool found = false;
        for(int i=0; i<tagsCount; i++) {
            if(knownTags[i].uid == uid) {
                sendTogglRequest(knownTags[i].projectId, knownTags[i].name);
                found = true;
                break;
            }
        }

        if(!found) {
            Serial.println(">>> Nieznany tag!");
            M5.dis.drawpix(0, 0xFF00FF); // Fiolet
            delay(500);
            M5.dis.drawpix(0, 0x00FF00);
        }

        // Halt i StopCrypto
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        
        // Czekaj chwilę, żeby nie spamować requestami
        delay(2000); 
    }
}