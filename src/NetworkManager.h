#pragma once
#include <WiFi.h>
#include <WiFiMulti.h>
#include <time.h>
#include "secrets.h"

class NetworkManager {
private:
    static WiFiMulti wifiMulti;

public:
    static void connect() {
        if (WiFi.status() == WL_CONNECTED) return;

        // Dodaj wszystkie zdefiniowane sieci
        for (int i = 0; i < WIFI_NETWORKS_COUNT; i++) {
            wifiMulti.addAP(WIFI_NETWORKS[i].ssid, WIFI_NETWORKS[i].pass);
        }

        Serial.println(">> [WiFi] Connecting...");
        
        // Próba połączenia z którąkolwiek siecią
        while (wifiMulti.run() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("\n>> [WiFi] Connected to: " + WiFi.SSID());
    }

    static bool isConnected() {
        // wifiMulti.run() zarządza ponownym łączeniem w tle jeśli zerwie
        return wifiMulti.run() == WL_CONNECTED;
    }

    static void syncTime() {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov"); 
        struct tm timeinfo;
        // Czekamy na czas (można dodać timeout w produkcji)
        while(!getLocalTime(&timeinfo)) {
            delay(500);
        }
    }

    static String getISOTime() {
        struct tm timeinfo;
        if(!getLocalTime(&timeinfo)) return "";
        char timeStringBuff[30];
        strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        return String(timeStringBuff);
    }
};
