// storage.h - settings (NVS) and barograph history (LittleFS) persistence.
#pragma once
#include "app_state.h"

void storageBegin();
void loadSettings(Settings &s);
void saveSettings(const Settings &s);
bool loadHistory(History &h);
bool saveHistory(const History &h);
