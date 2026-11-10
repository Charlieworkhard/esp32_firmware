#include "protocol.h"

#include <cJSON.h>

bool Protocol::SendStartListening(const std::string& mode) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "start");
    cJSON_AddStringToObject(root, "mode", mode.c_str());
    bool ok = SendJson(root);
    cJSON_Delete(root);
    return ok;
}

bool Protocol::SendStopListening() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "stop");
    bool ok = SendJson(root);
    cJSON_Delete(root);
    return ok;
}

bool Protocol::SendAbortSpeaking() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "abort");
    cJSON_AddStringToObject(root, "reason", "user_interrupt");
    bool ok = SendJson(root);
    cJSON_Delete(root);
    return ok;
}
