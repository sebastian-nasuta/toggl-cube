#pragma once
#include <Arduino.h>

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
#define COL_STANDBY 0x001100 
