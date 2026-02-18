#pragma once
#include <WiFi.h>
#include <time.h>
#include "secrets.h"

class NetworkManager {
public:
    static void connect() {
        if (WiFi.status() == WL_CONNECTED) return;

        WiFi.begin(WIFI_SSID, WIFI_PASS);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500); 
        }
    }

    static bool isConnected() {
        return WiFi.status() == WL_CONNECTED;
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
