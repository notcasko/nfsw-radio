#pragma once

#include "RadioCommon.h"

// Settings Directory Resolution
std::string GetSettingsDir();
std::string GetIniPath();
std::string GetSkinsDir();

// Configuration saving
void SaveConfigVolume();
void SaveConfigSkin(const std::string& skinName);
void SaveConfigPosition(int x, int y);

// Key string parser
int ParseKeyString(const std::string& rawKey, int defaultKey);

// INI file utilities and loading
void CreateDefaultConfig(const std::string& iniPath);
std::string ReadIniStringUtf8(const std::string& iniPath, const std::string& section, const std::string& key, const std::string& defaultVal);
std::string ReadIniKeyFallback(const std::string& iniPath, const std::string& section, const std::string& key1, const std::string& key2, const std::string& defaultVal = "");
void LoadConfig();

// Lightweight JSON Parser
void SkipWhitespaceAndComments(const std::string& str, size_t& pos);
std::string ParseJsonString(const std::string& str, size_t& pos);
void ParseJsonObject(const std::string& str, size_t& pos, const std::string& prefix, std::map<std::string, std::string>& out);
