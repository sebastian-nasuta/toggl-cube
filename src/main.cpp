#include <M5Atom.h>
#include <M5UnitRFID.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h" // Tu są Twoje hasła

// --- KONFIGURACJA MAPOWANIA ---
struct ProjectMapping {
    String uid;             // UID Taga (np. "04a1b2c3")
    unsigned long projectId;// ID Projektu w Toggl (np. 123456)
    String name;            // Nazwa dla logów (np. "DEV")
};

// TU BĘDZIEMY WPISYWAĆ TWOJE TAGI
// Na razie zostaw puste lub wpisz losowe, uzupełnisz po skanowaniu
ProjectMapping knownTags[] = {
    {"", 000000, "DEV"},   
    {"", 000000, "CALL"},
    {"", 000000, "PRIV"} 
};
const int tagsCount = sizeof(knownTags) / sizeof(knownTags[0]);

// --- OBIEKTY ---
M5UnitRFID rfid;

// --- POMOCNICZE: Czas NTP (ISO 8601) ---
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
        M5.dis.drawpix(0, 0xFF0000); // Czerwony - brak sieci
        return;
    }

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // Pomijamy certyfikaty SSL (wystarczy dla DIY)

    String url = "https://api.track.toggl.com/api/v9/time_entries";
    
    Serial.printf(">> Startuje timer: %s...\n", description.c_str());
    M5.dis.drawpix(0, 0x0000FF); // Niebieski - Przetwarzanie

    if (http.begin(client, url)) {
        // Auth: Token jako user, hasło "api_token"
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
            M5.dis.drawpix(0, 0xFFFFFF); // BIAŁY - Sukces
            delay(1000);
            M5.dis.drawpix(0, 0x00FF00); // Powrót do zielonego
        } else {
            Serial.printf(">> BLAD HTTP: %d\n", httpCode);
            Serial.println(http.getString());
            M5.dis.drawpix(0, 0xFF0000); // CZERWONY - Błąd API
            delay(2000);
            M5.dis.drawpix(0, 0x00FF00);
        }
        http.end();
    } else {
        Serial.println(">> Blad polaczenia z URL");
    }
}

void setup() {
    M5.begin(true, false, true); // Init Atom
    Serial.begin(115200);
    rfid.begin();

    // 1. WiFi
    Serial.print("Laczenie z WiFi: ");
    Serial.println(WIFI_SSID);
    M5.dis.drawpix(0, 0x000055); // Ciemny niebieski
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nPolaczono!");

    // 2. Czas NTP (Kluczowe dla Toggla!)
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
    M5.update(); // Obsługa przycisku (jeśli kiedyś użyjesz)

    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        // Konwersja UID na String
        String uid = "";
        for (byte i = 0; i < rfid.uid.size; i++) {
            if(rfid.uid.uidByte[i] < 0x10) uid += "0";
            uid += String(rfid.uid.uidByte[i], HEX);
        }
        
        Serial.print(">>> Wykryto TAG UID: ");
        Serial.println(uid);

        // Szukanie w bazie
        bool found = false;
        for(int i=0; i<tagsCount; i++) {
            if(knownTags[i].uid == uid) {
                sendTogglRequest(knownTags[i].projectId, knownTags[i].name);
                found = true;
                break;
            }
        }

        if(!found) {
            Serial.println(">>> Nieznany tag. Skopiuj UID i dodaj do kodu!");
            M5.dis.drawpix(0, 0xFF00FF); // FIOLETOWY - Nieznany tag
            delay(500);
            M5.dis.drawpix(0, 0x00FF00);
        }

        // Blokada przed wielokrotnym odczytem
        delay(1000); 
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
    }
}