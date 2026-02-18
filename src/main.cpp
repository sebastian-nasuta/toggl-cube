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
    {"04db363dc12a81", 216923300, "BREAK"},   
    {"04e5363dc12a81", 216923282, "CALL"},
    {"04e4363dc12a81", 216923280, "DEV"},
    {"04e3363dc12a81", 216923294, "INTERNAL"},
    {"04e2363dc12a81", 216923286, "PRIV"},
    {"04dc363dc12a81", 0, "STOP"}
};
const int tagsCount = sizeof(knownTags) / sizeof(knownTags[0]);

// --- OBIEKTY ---
MFRC522_I2C rfid(0x28, -1); 

// --- POMOCNICZE ---
String getISOTime() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)) return "";
    char timeStringBuff[30];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(timeStringBuff);
}

// --- LOGIKA ZATRZYMYWANIA (NOWA) ---
void stopRunningTimer() {
    if(WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();

    Serial.println(">> Sprawdzam biegnący timer...");
    M5.dis.drawpix(0, 0xFFA500); // POMARAŃCZOWY (Szukanie)

    // KROK 1: Pobierz ID aktualnego timera
    String currentUrl = "https://api.track.toggl.com/api/v9/me/time_entries/current";
    if (http.begin(client, currentUrl)) {
        http.setAuthorization(TOGGL_TOKEN, "api_token");
        int httpCode = http.GET();

        if (httpCode == 200) {
            String payload = http.getString();
            StaticJsonDocument<1024> doc;
            deserializeJson(doc, payload);

            // Sprawdź czy cokolwiek biegnie (API zwraca null jeśli nic nie ma)
            if (doc.isNull() || doc["id"].isNull()) {
                Serial.println(">> Nic nie biegnie. Nie ma co zatrzymywać.");
                M5.dis.drawpix(0, 0x00FF00); // Zielony
                http.end();
                return;
            }

            long entryId = doc["id"];
            long workspaceId = doc["workspace_id"];
            Serial.printf(">> Zatrzymuje timer ID: %ld\n", entryId);

            http.end(); // Zamykamy GET, otwieramy PATCH

            // KROK 2: Zatrzymaj timer (PATCH)
            String stopUrl = "https://api.track.toggl.com/api/v9/workspaces/" + String(workspaceId) + "/time_entries/" + String(entryId) + "/stop";
            
            if(http.begin(client, stopUrl)) {
                http.setAuthorization(TOGGL_TOKEN, "api_token");
                http.addHeader("Content-Type", "application/json"); // Wymagane
                
                // Toggl wymaga PATCH. Biblioteka HTTPClient obsługuje to przez sendRequest
                int patchCode = http.sendRequest("PATCH", ""); 

                if (patchCode == 200) {
                    Serial.println(">> ZATRZYMANO!");
                    M5.dis.drawpix(0, 0xFF0000); // CZERWONY (Stop)
                    delay(2000);
                    M5.dis.drawpix(0, 0x00FF00); // Zielony
                } else {
                    Serial.printf(">> Blad zatrzymywania: %d\n", patchCode);
                    M5.dis.drawpix(0, 0xFF00FF); // Błąd
                }
            }
        }
        http.end();
    }
}

// --- LOGIKA STARTOWANIA (STARA) ---
void startTimer(unsigned long projectId, String description) {
    if(WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); 

    String url = "https://api.track.toggl.com/api/v9/time_entries";
    
    Serial.printf(">> Startuje: %s...\n", description.c_str());
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
            Serial.println(">> START OK!");
            M5.dis.drawpix(0, 0xFFFFFF); // BIAŁY
            delay(1000);
            M5.dis.drawpix(0, 0x00FF00); 
        } else {
            M5.dis.drawpix(0, 0xFF0000); 
        }
        http.end();
    }
}

void setup() {
    M5.begin(true, false, true);
    Serial.begin(115200);
    Wire.begin(26, 32); 
    rfid.PCD_Init(); // Init RFID

    // WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        M5.dis.drawpix(0, 0x000055); 
    }
    Serial.println(" OK!");

    // Czas
    configTime(0, 0, "pool.ntp.org", "time.nist.gov"); 
    struct tm timeinfo;
    while(!getLocalTime(&timeinfo)) delay(500);
    Serial.println("Czas OK!");
    
    M5.dis.drawpix(0, 0x00FF00); // GOTOWY
}

void loop() {
    M5.update();

    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        String uid = "";
        for (byte i = 0; i < rfid.uid.size; i++) {
            if(rfid.uid.uidByte[i] < 0x10) uid += "0";
            uid += String(rfid.uid.uidByte[i], HEX);
        }
        
        Serial.print("TAG: "); Serial.println(uid);

        bool found = false;
        for(int i=0; i<tagsCount; i++) {
            if(knownTags[i].uid == uid) {
                // TU JEST LOGIKA DECYZYJNA
                if (knownTags[i].projectId == 0) {
                    stopRunningTimer();
                } else {
                    startTimer(knownTags[i].projectId, knownTags[i].name);
                }
                found = true;
                break;
            }
        }

        if(!found) {
            M5.dis.drawpix(0, 0xFF00FF); // Nieznany
            delay(500);
            M5.dis.drawpix(0, 0x00FF00);
        }

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        delay(2000); 
    }
}