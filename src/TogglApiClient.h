#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "NetworkManager.h"
#include "Models.h"

class TogglApiClient {
private:
    const char* _token;
    String _workspaceId;

    // Helper do requestów
    int sendRequest(String method, String url, String payload, String& response) {
        if (!NetworkManager::isConnected()) return -1;

        HTTPClient http;
        WiFiClientSecure client;
        client.setInsecure();

        if (!http.begin(client, url)) return -1;

        http.setAuthorization(_token, "api_token");
        http.addHeader("Content-Type", "application/json");

        int httpCode = http.sendRequest(method.c_str(), payload);
        if (httpCode > 0) {
            response = http.getString();
        }
        http.end();
        return httpCode;
    }

public:
    TogglApiClient(const char* token, String workspaceId) : _token(token), _workspaceId(workspaceId) {}

    bool getCurrentEntry(String& entryId, String& workspaceId, unsigned long& projectId) {
        String url = "https://api.track.toggl.com/api/v9/me/time_entries/current";
        String payload;
        
        int code = sendRequest("GET", url, "", payload);
        
        if (code != 200 || payload == "null" || payload.length() < 5) {
            return false;
        }

        StaticJsonDocument<2048> doc;
        if (deserializeJson(doc, payload)) return false;

        entryId = doc["id"].as<String>();
        workspaceId = doc["workspace_id"].as<String>();
        projectId = doc["project_id"] | 0; // Może być null jeśli nie przypisano
        return true;
    }

    bool stopEntry(String workspaceId, String entryId) {
        String url = "https://api.track.toggl.com/api/v9/workspaces/" + workspaceId + "/time_entries/" + entryId + "/stop";
        String response;
        int code = sendRequest("PATCH", url, "{}", response);
        return code == 200;
    }

    bool startEntry(unsigned long projectId, String description) {
        String url = "https://api.track.toggl.com/api/v9/time_entries";
        
        StaticJsonDocument<300> doc;
        doc["created_with"] = "M5Atom_Cube";
        doc["description"] = description;
        doc["tags"] = JsonArray();
        doc["billable"] = false;
        doc["workspace_id"] = atol(_workspaceId.c_str()); // lub przekonwertuj w konfigu
        doc["project_id"] = projectId;
        doc["duration"] = -1;
        doc["start"] = NetworkManager::getISOTime();
        doc["stop"] = nullptr;

        String requestBody;
        serializeJson(doc, requestBody);
        
        String response;
        int code = sendRequest("POST", url, requestBody, response);
        return code == 200;
    }
};
