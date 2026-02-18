#pragma once
#include <M5Atom.h>
#include <Wire.h>
#include <MFRC522_I2C.h>
#include "Config.h"

class Hardware {
private:
    MFRC522_I2C _rfid;

public:
    Hardware() : _rfid(0x28, -1) {}

    void init() {
        M5.begin(true, false, true);
        Wire.begin(26, 32);
        _rfid.PCD_Init();
        setLed(COL_WHITE); // Sygnał startu
    }

    void update() {
        M5.update();
    }

    void setLed(uint32_t color) {
        M5.dis.drawpix(0, color);
    }

    // Prosta animacja błędu
    void signalError() {
        setLed(COL_YELLOW); delay(200); 
        setLed(COL_OFF); delay(200); 
        setLed(COL_YELLOW); delay(200);
        setLed(COL_OFF);
    }

    // Sygnalizacja sukcesu
    void signalSuccess(uint32_t color) {
        setLed(color);
        // Uwaga: w oryginalnym kodzie było delay tylko przy stopie, 
        // ale dla czytelności UX warto dać chwilę na zobaczenie koloru
        // Można to przenieść wyżej jeśli ma być asynchroniczne
    }

    bool readTag(String& outUid) {
        if (!_rfid.PICC_IsNewCardPresent() || !_rfid.PICC_ReadCardSerial()) {
            return false;
        }

        outUid = "";
        for (byte i = 0; i < _rfid.uid.size; i++) {
            if(_rfid.uid.uidByte[i] < 0x10) outUid += "0";
            outUid += String(_rfid.uid.uidByte[i], HEX);
        }

        _rfid.PICC_HaltA();
        _rfid.PCD_StopCrypto1();
        return true;
    }
};
