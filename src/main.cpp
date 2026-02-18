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
    String uid;               // UID z tagu RFID
    unsigned long projectId;  // ID projektu w Toggl Track (0 dla STOP)
    String name;              // Nazwa projektu (do wyświetlania) - opcjonalna, ale przydatna
    uint32_t color;           // Kolor do wyświetlania na M5Atom (HEX, np. 0xFF0000 dla czerwonego)
};

// --- DEFINICJE KOLORÓW (HEX) ---
#define COL_RED    0xFF0000
#define COL_GREEN  0x00FF00
#define COL_BLUE   0x0000FF
#define COL_PURPLE 0x800080
#define COL_YELLOW 0xFFFF00
#define COL_CYAN   0x00FFFF
#define COL_WHITE  0xFFFFFF
#define COL_OFF    0x000000

// TU WPISZ SWOJE TAGI
ProjectMapping knownTags[] = {
    {"04db363dc12a81", 216923300, "BREAK", COL_RED},   
    {"04e5363dc12a81", 216923282, "CALL", COL_BLUE},
    {"04e4363dc12a81", 216923280, "DEV", COL_GREEN},
    {"04e3363dc12a81", 216923294, "INTERNAL", COL_YELLOW},
    {"04e2363dc12a81", 216923286, "PRIV", COL_PURPLE},
    {"04dc363dc12a81", 0, "STOP", COL_OFF}
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

// Funkcja ustawiająca kolor diody
void setStatusLed(uint32_t color) {
    M5.dis.drawpix(0, color);
}

// --- LOGIKA ZATRZYMYWANIA ---
void stopRunningTimer(uint32_t successColor) {
    if(WiFi.status() != WL_CONNECTED) return;

    HTTPClient httpGet;
    WiFiClientSecure clientGet;
    clientGet.setInsecure();

    // Miganie na biało podczas przetwarzania
    M5.dis.drawpix(0, COL_WHITE); 

    // KROK 1: GET
    String currentUrl = "https://api.track.toggl.com/api/v9/me/time_entries/current";
    String entryId = "";
    String workspaceId = "";
    
    Serial.println(">> [1/2] Pobieram ID...");
    if (httpGet.begin(clientGet, currentUrl)) {
        httpGet.setAuthorization(TOGGL_TOKEN, "api_token");
        int httpCode = httpGet.GET();

        if (httpCode == 200) {
            String payload = httpGet.getString();
            if (payload == "null" || payload.length() < 5) {
                Serial.println(">> Nic nie biegnie.");
                setStatusLed(successColor); // Ustawiamy kolor STOPU mimo że nic nie biegło
                httpGet.end();
                return;
            }

            StaticJsonDocument<2048> doc;
            deserializeJson(doc, payload);
            entryId = doc["id"].as<String>();
            workspaceId = doc["workspace_id"].as<String>();
        } 
        httpGet.end(); 
    }

    if (entryId == "") return;

    // KROK 2: PATCH
    HTTPClient httpPatch;
    WiFiClientSecure clientPatch;
    clientPatch.setInsecure();
    String stopUrl = "https://api.track.toggl.com/api/v9/workspaces/" + workspaceId + "/time_entries/" + entryId + "/stop";
    
    Serial.println(">> [2/2] Zatrzymuje...");
    
    if(httpPatch.begin(clientPatch, stopUrl)) {
        httpPatch.setAuthorization(TOGGL_TOKEN, "api_token");
        httpPatch.addHeader("Content-Type", "application/json");
        int patchCode = httpPatch.sendRequest("PATCH", "{}"); 

        if (patchCode == 200) {
            Serial.println(">> ZATRZYMANO.");
            // TU JEST ZMIANA: Ustawiamy kolor przypisany do taga STOP
            setStatusLed(successColor); 
        } else {
            // W razie błędu mrugnij na czerwono i wróć
            M5.dis.drawpix(0, COL_OFF); delay(200); M5.dis.drawpix(0, COL_RED);
        }
        httpPatch.end();
    }
}

// --- LOGIKA STARTOWANIA ---
void startTimer(unsigned long projectId, String description, uint32_t successColor) {
    if(WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); 

    // Miganie na biało podczas przetwarzania
    M5.dis.drawpix(0, COL_WHITE);

    String url = "https://api.track.toggl.com/api/v9/time_entries";
    Serial.printf(">> Startuje: %s...\n", description.c_str());

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
            // TU JEST ZMIANA: Ustawiamy kolor przypisany do taga PROJEKTU
            setStatusLed(successColor);
        } else {
            M5.dis.drawpix(0, COL_OFF); delay(200); M5.dis.drawpix(0, COL_RED); // Błąd = Mrugnięcie
        }
        http.end();
    }
}

void setup() {
    M5.begin(true, false, true);
    Serial.begin(115200);
    Wire.begin(26, 32); 
    rfid.PCD_Init();

    // WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi");
    M5.dis.drawpix(0, COL_WHITE); // Świeci na biało podczas bootowania
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        M5.dis.drawpix(0, COL_OFF); delay(100); M5.dis.drawpix(0, COL_WHITE);
    }
    Serial.println(" OK!");

    configTime(0, 0, "pool.ntp.org", "time.nist.gov"); 
    struct tm timeinfo;
    while(!getLocalTime(&timeinfo)) delay(500);
    Serial.println("Czas OK!");
    
    // Po starcie gasimy diodę (czeka na pierwszą akcję)
    // Lub ustawiamy np. na Zielony (Gotowy)
    M5.dis.drawpix(0, 0x001100); // Bardzo ciemny zielony (Dim Green)
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
                // Przekazujemy kolor z konfiguracji do funkcji
                if (knownTags[i].projectId == 0) {
                    stopRunningTimer(knownTags[i].color);
                } else {
                    startTimer(knownTags[i].projectId, knownTags[i].name, knownTags[i].color);
                }
                found = true;
                break;
            }
        }

        if(!found) {
            // Nieznany tag - szybkie mrugnięcie na żółto
            M5.dis.drawpix(0, COL_YELLOW); 
            delay(500);
            M5.dis.drawpix(0, COL_OFF); // Gasimy
        }

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        
        // Ważne: opóźnienie, żeby nie czytał w kółko
        delay(2000); 
    }
}