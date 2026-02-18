#pragma once
#include <Arduino.h>

struct ProjectMapping {
    String uid;             // UID Taga z RFID
    unsigned long projectId;// ID Projektu w Toggl (0 = TAG STOPU)
    String name;            // Nazwa do logów
    uint32_t color;         // Kolor diody dla tego projektu
};
