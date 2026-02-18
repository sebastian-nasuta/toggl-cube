#include <M5Atom.h>
#include <Wire.h>
#include <MFRC522_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h" // Plik z hasłami (WiFi, Token, WorkspaceID)

// --- KONFIGURACJA ---
#define SYNC_INTERVAL 30000  // Co ile ms sprawdzać stan w chmurze (30 sek)
#define BLINK_INTERVAL 500   // Szybkość mrugania przy nieznanym projekcie

// --- KOLORY DIODY ---
#define COL_RED    0xFF0000
#define COL_GREEN  0x00FF00
#define COL_BLUE   0x0000FF
#define COL_PURPLE 0x800080
#define COL_YELLOW 0xFFFF00
#define COL_CYAN   0x00FFFF
#define COL_WHITE  0xFFFFFF
#define COL_OFF    0x000000
#define COL_STANDBY 0x001100 // Bardzo ciemny zielony (Czuwanie)

// --- STRUKTURA PROJEKTU ---
struct ProjectMapping {
    String uid;             // UID Taga z RFID
    unsigned long projectId;// ID Projektu w Toggl (0 = TAG STOPU)
    String name;            // Nazwa do logów
    uint32_t color;         // Kolor diody dla tego projektu
};

// !!! TUTAJ SKONFIGURUJ SWOJE TAGI !!!
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

// --- ZMIENNE STANU ---
unsigned long lastSyncTime = 0;
unsigned long lastBlinkTime = 0;
bool isUnknownProject = false;
bool blinkState = false;

// --- POMOCNICZE ---
String getISOTime() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)) return "";
    char timeStringBuff[30];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(timeStringBuff);
}

void setLed(uint32_t color) {
    M5.dis.drawpix(0, color);
}

// ==========================================
// LOGIKA API TOGGL
// ==========================================

void stopRunningTimer(uint32_t successColor) {
    if(WiFi.status() != WL_CONNECTED) return;

    HTTPClient httpGet;
    WiFiClientSecure clientGet;
    clientGet.setInsecure();

    Serial.println(">> [STOP] Sprawdzam co biegnie...");
    setLed(COL_WHITE); 

    String currentUrl = "https://api.track.toggl.com/api/v9/me/time_entries/current";
    String entryId = "";
    String workspaceId = "";

    // KROK A: Pobierz ID
    if (httpGet.begin(clientGet, currentUrl)) {
        httpGet.setAuthorization(TOGGL_TOKEN, "api_token");
        int httpCode = httpGet.GET();

        if (httpCode == 200) {
            String payload = httpGet.getString();
            if (payload == "null" || payload.length() < 5) {
                // Nic nie biegnie -> wygaś diodę
                isUnknownProject = false;
                setLed(COL_OFF); 
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

    // KROK B: Zatrzymaj (PATCH)
    HTTPClient httpPatch;
    WiFiClientSecure clientPatch;
    clientPatch.setInsecure();
    
    String stopUrl = "https://api.track.toggl.com/api/v9/workspaces/" + workspaceId + "/time_entries/" + entryId + "/stop";
    Serial.println(">> [STOP] Wysylam zadanie zatrzymania...");

    if(httpPatch.begin(clientPatch, stopUrl)) {
        httpPatch.setAuthorization(TOGGL_TOKEN, "api_token");
        httpPatch.addHeader("Content-Type", "application/json");
        
        int patchCode = httpPatch.sendRequest("PATCH", "{}"); 

        if (patchCode == 200) {
            Serial.println(">> [STOP] Zatrzymano.");
            isUnknownProject = false;
            
            // Sygnalizacja sukcesu (Kolor STOPU przez 2 sekundy)
            setLed(successColor); 
            delay(2000);
            
            // Finalnie: WYGASZENIE
            setLed(COL_OFF);
        } else {
            setLed(COL_YELLOW); // Błąd
        }
        httpPatch.end();
    }
}

void startTimer(unsigned long projectId, String description, uint32_t successColor) {
    if(WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); 

    Serial.printf(">> [START] Uruchamiam: %s...\n", description.c_str());
    setLed(COL_WHITE); 

    String url = "https://api.track.toggl.com/api/v9/time_entries";
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
            Serial.println(">> [START] Sukces!");
            isUnknownProject = false;
            setLed(successColor); 
        } else {
            setLed(COL_YELLOW); // Błąd
        }
        http.end();
    }
}

void syncWithToggl() {
    if(WiFi.status() != WL_CONNECTED) return;

    Serial.println(">> [SYNC] Aktualizacja stanu...");
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();

    String url = "https://api.track.toggl.com/api/v9/me/time_entries/current";
    
    if (http.begin(client, url)) {
        http.setAuthorization(TOGGL_TOKEN, "api_token");
        int httpCode = http.GET();

        if (httpCode == 200) {
            String payload = http.getString();
            
            // A. Nic nie biegnie
            if (payload == "null" || payload.length() < 5) {
                isUnknownProject = false;
                setLed(COL_OFF); // !!! ZMIANA: PEŁNE WYGASZENIE !!!
                http.end();
                return;
            }

            // B. Coś biegnie
            StaticJsonDocument<2048> doc;
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                unsigned long currentPid = doc["project_id"].as<unsigned long>();
                
                bool matchFound = false;
                for(int i=0; i<tagsCount; i++) {
                    if (knownTags[i].projectId == currentPid && currentPid != 0) {
                        setLed(knownTags[i].color);
                        isUnknownProject = false;
                        matchFound = true;
                        break;
                    }
                }

                if (!matchFound) {
                    isUnknownProject = true; // Będzie mrugać w loop()
                }
            }
        }
        http.end();
    }
}

// ==========================================
// SETUP & LOOP
// ==========================================

void setup() {
    M5.begin(true, false, true);
    Serial.begin(115200);
    Wire.begin(26, 32); 
    rfid.PCD_Init();

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    setLed(COL_WHITE);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); 
        setLed(COL_OFF); delay(100); setLed(COL_WHITE);
    }
    
    configTime(0, 0, "pool.ntp.org", "time.nist.gov"); 
    struct tm timeinfo;
    while(!getLocalTime(&timeinfo)) delay(500);
    
    syncWithToggl();
}

void loop() {
    M5.update();
    unsigned long currentMillis = millis();

    // 1. MRUGANIE (tylko nieznane projekty)
    if (isUnknownProject) {
        if (currentMillis - lastBlinkTime >= BLINK_INTERVAL) {
            lastBlinkTime = currentMillis;
            blinkState = !blinkState;
            setLed(blinkState ? COL_RED : COL_OFF);
        }
    }

    // 2. SYNCHRONIZACJA
    if (currentMillis - lastSyncTime >= SYNC_INTERVAL) {
        lastSyncTime = currentMillis;
        syncWithToggl();
    }

    // 3. RFID
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        String uid = "";
        for (byte i = 0; i < rfid.uid.size; i++) {
            if(rfid.uid.uidByte[i] < 0x10) uid += "0";
            uid += String(rfid.uid.uidByte[i], HEX);
        }
        Serial.println(">>> SCAN TAG: " + uid);

        bool found = false;
        for(int i=0; i<tagsCount; i++) {
            if(knownTags[i].uid == uid) {
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
            setLed(COL_YELLOW); delay(200); setLed(COL_OFF); delay(200); setLed(COL_YELLOW);
            delay(500);
            setLed(COL_OFF); // Po błędzie gasimy
            lastSyncTime = 0; // Wymuś sync
        } else {
            lastSyncTime = millis();
        }

        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        delay(1500); 
    }
}