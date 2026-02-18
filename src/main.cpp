#include <Arduino.h>
#include "Hardware.h"
#include "NetworkManager.h"
#include "TogglApiClient.h"
#include "Config.h"
#include "Models.h"
#include "secrets.h" 

// --- DANE PROJEKTÓW ---
// Przeniesione do stałej tablicy, łatwiej edytować
const ProjectMapping knownTags[] = {
    {"04db363dc12a81", 216923300, "BREAK", COL_RED},   
    {"04e5363dc12a81", 216923282, "CALL", COL_BLUE},
    {"04e4363dc12a81", 216923280, "DEV", COL_GREEN},
    {"04e3363dc12a81", 216923294, "INTERNAL", COL_YELLOW},
    {"04e2363dc12a81", 216923286, "PRIV", COL_PURPLE},
    {"04dc363dc12a81", 0, "STOP", COL_OFF}
};
const int tagsCount = sizeof(knownTags) / sizeof(knownTags[0]);

// --- SYSTEM ---
Hardware hw;
TogglApiClient toggl(TOGGL_TOKEN, WORKSPACE_ID);

// --- STAN ---
unsigned long lastSyncTime = 0;
unsigned long lastBlinkTime = 0;
bool isUnknownProject = false;
bool blinkState = false;

// --- LOGIKA APLIKACJI ---

const ProjectMapping* findProjectByUid(const String& uid) {
    for (int i = 0; i < tagsCount; i++) {
        if (knownTags[i].uid == uid) return &knownTags[i];
    }
    return nullptr;
}

const ProjectMapping* findProjectById(unsigned long id) {
    for (int i = 0; i < tagsCount; i++) {
        if (knownTags[i].projectId == id) return &knownTags[i];
    }
    return nullptr;
}

void handleSync() {
    Serial.println(">> [SYNC] Checking status...");
    
    String entryId, workspaceId;
    unsigned long projectId = 0;

    if (!toggl.getCurrentEntry(entryId, workspaceId, projectId)) {
        // Nic nie biegnie
        isUnknownProject = false;
        hw.setLed(COL_OFF);
        return;
    }

    // Coś biegnie, sprawdzamy czy znamy ten projekt
    const ProjectMapping* project = findProjectById(projectId);
    
    if (project) {
        hw.setLed(project->color);
        isUnknownProject = false;
    } else {
        isUnknownProject = true; // Nieznany projekt -> mruganie
    }
}

void handleTagScan(String uid) {
    Serial.println(">>> SCAN TAG: " + uid);
    const ProjectMapping* project = findProjectByUid(uid);

    if (!project) {
        Serial.println(">> Unknown TAG");
        hw.signalError();
        lastSyncTime = 0; // Wymuś odświeżenie
        return;
    }

    hw.setLed(COL_WHITE); // Feedback: "Przetwarzam..."

    if (project->projectId == 0) {
        // --- STOP ---
        Serial.println(">> [STOP] Stop requested via tag");
        String runningId, runningWs;
        unsigned long runningPid;

        // Najpierw musimy wiedzieć co zatrzymać
        if (toggl.getCurrentEntry(runningId, runningWs, runningPid)) {
           if (toggl.stopEntry(runningWs, runningId)) {
               Serial.println(">> [STOP] Success");
               isUnknownProject = false;
               hw.signalSuccess(project->color); // Kolor STOPU
               delay(2000); // Nie blokuj loopa na 2s w idealnym świecie, ale tu KISS
               hw.setLed(COL_OFF);
           } else {
               hw.signalError();
           }
        } else {
            // Nic nie biegnie, więc STOP to po prostu zgaszenie
            hw.setLed(COL_OFF);
            isUnknownProject = false;
        }

    } else {
        // --- START ---
        Serial.printf(">> [START] Requested: %s\n", project->name.c_str());
        if (toggl.startEntry(project->projectId, project->name)) {
            Serial.println(">> [START] Success");
            isUnknownProject = false;
            hw.signalSuccess(project->color);
        } else {
            hw.signalError();
        }
    }
    
    // Po akcji zaktualizujmy czas synchronizacji, żeby nie odpytywać API od razu
    lastSyncTime = millis();
}

void setup() {
    Serial.begin(115200);
    hw.init();
    
    NetworkManager::connect();
    // Po połączeniu mignij na biało
    hw.setLed(COL_OFF); delay(100); hw.setLed(COL_WHITE);

    NetworkManager::syncTime();
    
    // Pierwsza synchronizacja
    handleSync();
}

void loop() {
    hw.update();
    unsigned long currentMillis = millis();

    // 1. RFID SCAN
    String uid;
    if (hw.readTag(uid)) {
        handleTagScan(uid);
        delay(1500); // Debounce po odczycie
    }

    // 2. SYNCHRONIZACJA (co jakiś czas)
    if (currentMillis - lastSyncTime >= SYNC_INTERVAL) {
        lastSyncTime = currentMillis;
        if (NetworkManager::isConnected()) {
            handleSync();
        }
    }

    // 3. MRUGANIE (tylko gdy nieznany projekt jest aktywny)
    if (isUnknownProject) {
        if (currentMillis - lastBlinkTime >= BLINK_INTERVAL) {
            lastBlinkTime = currentMillis;
            blinkState = !blinkState;
            hw.setLed(blinkState ? COL_RED : COL_OFF);
        }
    }
}
