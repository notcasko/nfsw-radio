#include "RadioConfig.h"
#include "RadioSkin.h"

std::string GetSettingsDir()
{
    static std::string cachedDir = "";
    if (!cachedDir.empty()) return cachedDir;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = GetDirectoryOfFile(exePath);

    char modPath[MAX_PATH];
    GetModuleFileNameA(g_hInstance, modPath, MAX_PATH);
    std::string modDir = GetDirectoryOfFile(modPath);

    std::vector<std::string> candidates = {
        exeDir + "\\Settings\\Radio",
        modDir + "\\..\\Settings\\Radio",
        modDir + "\\Settings\\Radio",
        ".\\Settings\\Radio"
    };

    for (const auto& cand : candidates) {
        std::string testIni1 = cand + "\\Radio.ini";
        std::string testIni2 = cand + "\\RadioMod.ini";
        if (GetFileAttributesW(StringToWide(testIni1).c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(StringToWide(testIni2).c_str()) != INVALID_FILE_ATTRIBUTES) {
            cachedDir = cand;
            return cachedDir;
        }
    }

    for (const auto& cand : candidates) {
        DWORD attr = GetFileAttributesW(StringToWide(cand).c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            cachedDir = cand;
            return cachedDir;
        }
    }

    std::string settingsParent = exeDir + "\\Settings";
    std::string radioSettings = settingsParent + "\\Radio";

    CreateDirectoryA(settingsParent.c_str(), NULL);
    CreateDirectoryA(radioSettings.c_str(), NULL);

    cachedDir = radioSettings;
    return cachedDir;
}

std::string GetIniPath()
{
    static std::string cachedPath = "";
    if (cachedPath.empty()) {
        std::string dir = GetSettingsDir();
        std::string cand1 = dir + "\\Radio.ini";
        std::string cand2 = dir + "\\RadioMod.ini";
        if (GetFileAttributesW(StringToWide(cand1).c_str()) != INVALID_FILE_ATTRIBUTES) {
            cachedPath = cand1;
        } else if (GetFileAttributesW(StringToWide(cand2).c_str()) != INVALID_FILE_ATTRIBUTES) {
            cachedPath = cand2;
        } else {
            cachedPath = cand1;
        }
    }
    return cachedPath;
}

void SaveConfigVolume()
{
    int volPercent = (int)(g_volume * 100.0f + 0.5f);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", volPercent);
    WritePrivateProfileStringW(L"General", L"Volume", StringToWide(buf).c_str(), StringToWide(GetIniPath()).c_str());
}

void SaveConfigSkin(const std::string& skinName)
{
    WritePrivateProfileStringW(L"General", L"Skin", StringToWide(skinName).c_str(), StringToWide(GetIniPath()).c_str());
}

void SaveConfigPosition(int x, int y)
{
    char bufX[32], bufY[32];
    snprintf(bufX, sizeof(bufX), "%d", x);
    snprintf(bufY, sizeof(bufY), "%d", y);
    WritePrivateProfileStringW(L"General", L"PosX", StringToWide(bufX).c_str(), StringToWide(GetIniPath()).c_str());
    WritePrivateProfileStringW(L"General", L"PosY", StringToWide(bufY).c_str(), StringToWide(GetIniPath()).c_str());
    LogMsg("Saved radio window position to INI: X=%d, Y=%d", x, y);
}

std::string GetSkinsDir()
{
    std::string skinsDir = GetSettingsDir() + "\\skins";
    CreateDirectoryA(skinsDir.c_str(), NULL);
    return skinsDir;
}

void SkipWhitespaceAndComments(const std::string& str, size_t& pos)
{
    while (pos < str.size()) {
        char c = str[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pos++;
        } else if (c == '/' && pos + 1 < str.size() && str[pos + 1] == '/') {
            pos += 2;
            while (pos < str.size() && str[pos] != '\n') pos++;
        } else {
            break;
        }
    }
}

std::string ParseJsonString(const std::string& str, size_t& pos)
{
    if (pos >= str.size() || str[pos] != '\"') return "";
    pos++;
    std::string res;
    while (pos < str.size()) {
        char c = str[pos++];
        if (c == '\"') break;
        if (c == '\\' && pos < str.size()) {
            char esc = str[pos++];
            if (esc == 'n') res += '\n';
            else if (esc == 't') res += '\t';
            else if (esc == 'r') res += '\r';
            else res += esc;
        } else {
            res += c;
        }
    }
    return res;
}

void ParseJsonObject(const std::string& str, size_t& pos, const std::string& prefix, std::map<std::string, std::string>& out)
{
    SkipWhitespaceAndComments(str, pos);
    if (pos >= str.size() || str[pos] != '{') return;
    pos++;

    while (pos < str.size()) {
        SkipWhitespaceAndComments(str, pos);
        if (pos >= str.size() || str[pos] == '}') {
            if (pos < str.size()) pos++;
            break;
        }

        if (str[pos] == ',') {
            pos++;
            continue;
        }

        if (str[pos] != '\"') {
            pos++;
            continue;
        }

        std::string key = ParseJsonString(str, pos);
        SkipWhitespaceAndComments(str, pos);
        if (pos < str.size() && str[pos] == ':') pos++;
        SkipWhitespaceAndComments(str, pos);

        std::string fullKey = prefix.empty() ? key : (prefix + "." + key);
        std::transform(fullKey.begin(), fullKey.end(), fullKey.begin(), ::tolower);

        if (pos < str.size() && str[pos] == '{') {
            ParseJsonObject(str, pos, fullKey, out);
        } else if (pos < str.size() && str[pos] == '\"') {
            std::string val = ParseJsonString(str, pos);
            out[fullKey] = val;
        } else if (pos < str.size() && str[pos] == '[') {
            size_t arrStart = pos;
            while (pos < str.size() && str[pos] != ']') pos++;
            if (pos < str.size()) pos++;
            out[fullKey] = TrimString(str.substr(arrStart, pos - arrStart));
        } else {
            size_t valStart = pos;
            while (pos < str.size() && str[pos] != ',' && str[pos] != '}' && str[pos] != '\n' && str[pos] != '\r') {
                pos++;
            }
            std::string val = TrimString(str.substr(valStart, pos - valStart));
            out[fullKey] = val;
        }
    }
}

std::string ReadIniStringUtf8(const std::string& iniPath, const std::string& section, const std::string& key, const std::string& defaultVal)
{
    FILE* f = _wfopen(StringToWide(iniPath).c_str(), L"rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 1024 * 1024) {
            std::string content(sz, '\0');
            fread(&content[0], 1, sz, f);
            fclose(f);

            size_t pos = 0;
            if (content.size() >= 2 && (unsigned char)content[0] == 0xFF && (unsigned char)content[1] == 0xFE) {
                const wchar_t* wchars = (const wchar_t*)(content.data() + 2);
                size_t wlen = (content.size() - 2) / 2;
                std::wstring wcontent(wchars, wlen);
                content = WideToUtf8(wcontent);
            } else if (content.size() >= 3 && (unsigned char)content[0] == 0xEF && (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) {
                pos = 3;
            }

            std::string currentSec = "";
            while (pos < content.size()) {
                size_t nextLine = content.find_first_of("\r\n", pos);
                if (nextLine == std::string::npos) nextLine = content.size();
                std::string line = TrimString(content.substr(pos, nextLine - pos));
                pos = (nextLine < content.size()) ? nextLine + 1 : nextLine;
                if (pos < content.size() && content[pos - 1] == '\r' && content[pos] == '\n') pos++;

                if (line.empty() || line[0] == ';' || line[0] == '#') continue;

                if (line.front() == '[' && line.back() == ']') {
                    currentSec = TrimString(line.substr(1, line.size() - 2));
                    continue;
                }

                if (_stricmp(currentSec.c_str(), section.c_str()) == 0) {
                    size_t eq = line.find('=');
                    if (eq != std::string::npos) {
                        std::string k = TrimString(line.substr(0, eq));
                        if (_stricmp(k.c_str(), key.c_str()) == 0) {
                            std::string v = TrimString(line.substr(eq + 1));
                            if (!v.empty() && (v.front() == '"' || v.front() == '\'')) {
                                char quote = v.front();
                                size_t closeQuote = v.find(quote, 1);
                                if (closeQuote != std::string::npos) {
                                    v = v.substr(1, closeQuote - 1);
                                } else {
                                    v = v.substr(1);
                                }
                            } else {
                                size_t sc = v.find(';');
                                if (sc != std::string::npos) v = v.substr(0, sc);
                                size_t hc = v.find('#');
                                if (hc != std::string::npos) v = v.substr(0, hc);
                            }
                            v = TrimString(v);
                            while (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
                                v = TrimString(v.substr(1, v.size() - 2));
                            }
                            return v;
                        }
                    }
                }
            }
        } else if (f) {
            fclose(f);
        }
    }

    wchar_t wbuf[1024] = {0};
    GetPrivateProfileStringW(StringToWide(section).c_str(), StringToWide(key).c_str(), L"", wbuf, 1024, StringToWide(iniPath).c_str());
    if (wbuf[0] != L'\0') {
        std::string val = TrimString(WideToUtf8(wbuf));
        while (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
            val = TrimString(val.substr(1, val.size() - 2));
        }
        return val;
    }

    return defaultVal;
}

int ParseKeyString(const std::string& rawKey, int defaultKey)
{
    std::string key = TrimString(rawKey);
    if (key.size() >= 2 && ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\''))) {
        key = TrimString(key.substr(1, key.size() - 2));
    }
    if (key.empty()) return defaultKey;

    std::string upperKey = key;
    std::transform(upperKey.begin(), upperKey.end(), upperKey.begin(), ::toupper);

    if (upperKey == "NONE" || upperKey == "0") return 0;

    if (upperKey == "CAPSLOCK" || upperKey == "CAPS LOCK" || upperKey == "CAPS_LOCK" || upperKey == "CAPS" || upperKey == "VK_CAPITAL") return VK_CAPITAL;
    if (upperKey == "SHIFT" || upperKey == "VK_SHIFT") return VK_SHIFT;
    if (upperKey == "CTRL" || upperKey == "CONTROL" || upperKey == "VK_CONTROL") return VK_CONTROL;
    if (upperKey == "ALT" || upperKey == "MENU" || upperKey == "VK_MENU") return VK_MENU;
    if (upperKey == "SPACE" || upperKey == "VK_SPACE") return VK_SPACE;
    if (upperKey == "TAB" || upperKey == "VK_TAB") return VK_TAB;
    if (upperKey == "ENTER" || upperKey == "RETURN" || upperKey == "VK_RETURN") return VK_RETURN;
    if (upperKey == "ESC" || upperKey == "ESCAPE" || upperKey == "VK_ESCAPE") return VK_ESCAPE;
    if (upperKey == "BACKSPACE" || upperKey == "BACK" || upperKey == "VK_BACK") return VK_BACK;
    if (upperKey == "INSERT" || upperKey == "VK_INSERT") return VK_INSERT;
    if (upperKey == "DELETE" || upperKey == "DEL" || upperKey == "VK_DELETE") return VK_DELETE;
    if (upperKey == "HOME" || upperKey == "VK_HOME") return VK_HOME;
    if (upperKey == "END" || upperKey == "VK_END") return VK_END;
    if (upperKey == "PAGEUP" || upperKey == "PGUP" || upperKey == "VK_PRIOR") return VK_PRIOR;
    if (upperKey == "PAGEDOWN" || upperKey == "PGDN" || upperKey == "VK_NEXT") return VK_NEXT;
    if (upperKey == "LEFT" || upperKey == "VK_LEFT") return VK_LEFT;
    if (upperKey == "RIGHT" || upperKey == "VK_RIGHT") return VK_RIGHT;
    if (upperKey == "UP" || upperKey == "VK_UP") return VK_UP;
    if (upperKey == "DOWN" || upperKey == "VK_DOWN") return VK_DOWN;

    if (upperKey.size() >= 2 && upperKey[0] == 'F') {
        int fNum = atoi(upperKey.substr(1).c_str());
        if (fNum >= 1 && fNum <= 24) return VK_F1 + (fNum - 1);
    }

    if (upperKey.size() == 4 && upperKey.substr(0, 3) == "VK_") return (int)upperKey[3];

    if (upperKey.size() > 2 && upperKey[0] == '0' && upperKey[1] == 'X') {
        char* endPtr = NULL;
        long val = strtol(upperKey.c_str(), &endPtr, 16);
        if (endPtr != upperKey.c_str()) return (int)val;
    }

    char* endPtr = NULL;
    long val = strtol(upperKey.c_str(), &endPtr, 10);
    if (endPtr != upperKey.c_str() && *endPtr == '\0') return (int)val;

    if (upperKey.size() == 1) {
        char c = upperKey[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (int)c;
    }

    return defaultKey;
}

void CreateDefaultConfig(const std::string& iniPath)
{
    FILE* f = _wfopen(StringToWide(iniPath).c_str(), L"w");
    if (!f) {
        LogMsg("Failed to create default config file at: %s", iniPath.c_str());
        return;
    }

    const char* defaultContent =
        "[General]\n"
        "; Active skin folder inside Settings\\Radio\\skins\n"
        "Skin = Default\n"
        "; Radio playback volume (0% - 100%)\n"
        "Volume = 50%\n"
        "; Popup screen coordinates (-1 for automatic default placement)\n"
        "PosX = -1\n"
        "PosY = -1\n"
        "; Show pop-up notification when switching in-game music tracks (1 = enabled, 0 = disabled)\n"
        "ShowInGameMusicPopup = 0\n"
        "; Custom music folder path (optional: leave empty to use game Sound\\Radio folder)\n"
        "; Supports paths with spaces (with or without double quotes), e.g.:\n"
        "; MusicPath = \"D:\\music\\Avenged Sevenfold\"\n"
        "MusicPath = \n\n"
        "[Keybindings]\n"
        "; Keybindings configuration for Radio Mod\n"
        "; Next track in current station (Default: Tab)\n"
        "RadioKey = Tab\n\n"
        "; Modifier to switch to next station when pressed with RadioKey (Default: Caps Lock)\n"
        "NextStationModifier = Caps Lock\n\n"
        "; Modifier to toggle playback (stop/resume) when pressed with RadioKey (Default: Q)\n"
        "TogglePlaybackModifier = Q\n\n"
        "; Modifier to scroll playlist order when pressed with RadioKey (Default: F)\n"
        "PlaylistOrderModifier = F\n\n"
        "; Modifier to switch skin when pressed with RadioKey (Default: G)\n"
        "SkinModifier = G\n\n"
        "; Modifier held while scrolling mouse wheel to adjust volume (Default: X)\n"
        "VolumeModifier = X\n";

    fputs(defaultContent, f);
    fclose(f);
    LogMsg("Created default configuration file: %s", iniPath.c_str());
}

std::string ReadIniKeyFallback(const std::string& iniPath, const std::string& section, const std::string& key1, const std::string& key2, const std::string& defaultVal)
{
    std::string val = ReadIniStringUtf8(iniPath, section, key1, "");
    if (val.empty() && !key2.empty()) {
        val = ReadIniStringUtf8(iniPath, section, key2, "");
    }
    return val.empty() ? defaultVal : val;
}

void LoadConfig()
{
    std::string iniPath = GetIniPath();

    DWORD attr = GetFileAttributesW(StringToWide(iniPath).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        LogMsg("Config file not found. Creating default: %s", iniPath.c_str());
        CreateDefaultConfig(iniPath);
    } else {
        LogMsg("Found config file: %s", iniPath.c_str());
    }

    // 1. Custom music folder (supports spaces and quotes)
    g_cfgMusicPath = NormalizePath(ReadIniKeyFallback(iniPath, "General", "MusicPath", "MusicFolder", ""));

    // 2. Active skin
    std::string skinName = ReadIniStringUtf8(iniPath, "General", "Skin", "Default");
    LoadSkin(skinName);

    // 3. Volume
    std::string volStr = ReadIniStringUtf8(iniPath, "General", "Volume", "");
    if (!volStr.empty()) {
        float val = (float)atof(volStr.c_str());
        if (volStr.find('%') != std::string::npos || val > 1.0f) {
            g_volume = val / 100.0f;
        } else if (val >= 0.0f) {
            g_volume = val;
        }
        if (g_volume < 0.0f) g_volume = 0.0f;
        if (g_volume > 1.0f) g_volume = 1.0f;
    } else {
        g_volume = 0.50f;
    }

    // 4. Saved Window Position
    g_popupX = GetPrivateProfileIntA("General", "PosX", -1, iniPath.c_str());
    g_popupY = GetPrivateProfileIntA("General", "PosY", -1, iniPath.c_str());

    // 5. In-Game Music Popup Setting
    g_cfgShowInGameMusicPopUp = (GetPrivateProfileIntA("General", "ShowInGameMusicPopup", 0, iniPath.c_str()) != 0);
    int igSetting = GetPrivateProfileIntA("InGameMusic", "ShowPopup", -1, iniPath.c_str());
    if (igSetting != -1) {
        g_cfgShowInGameMusicPopUp = (igSetting != 0);
    }

    g_cfgRadioKey = ParseKeyString(ReadIniKeyFallback(iniPath, "Keybindings", "RadioKey", "NextTrackKey"), VK_TAB);
    g_cfgNextStationMod = ParseKeyString(ReadIniKeyFallback(iniPath, "Keybindings", "NextStationModifier", "NextStationKey"), VK_CAPITAL);
    g_cfgTogglePlaybackMod = ParseKeyString(ReadIniKeyFallback(iniPath, "Keybindings", "TogglePlaybackModifier", "TogglePlaybackKey"), 'Q');
    g_cfgPlaylistOrderMod = ParseKeyString(ReadIniKeyFallback(iniPath, "Keybindings", "PlaylistOrderModifier", "PlaylistOrderKey"), 'F');
    g_cfgSkinMod = ParseKeyString(ReadIniKeyFallback(iniPath, "Keybindings", "SkinModifier", "SkinKey"), 'G');
    g_cfgVolumeMod = ParseKeyString(ReadIniKeyFallback(iniPath, "Keybindings", "VolumeModifier", "VolumeKey"), 'X');

    LogMsg("Active Keybindings: RadioKey=0x%02X, NextStationMod=0x%02X, ToggleMod=0x%02X, OrderMod=0x%02X, SkinMod=0x%02X, VolumeMod=0x%02X, Pos=(%d,%d)",
           g_cfgRadioKey, g_cfgNextStationMod, g_cfgTogglePlaybackMod, g_cfgPlaylistOrderMod, g_cfgSkinMod, g_cfgVolumeMod, g_popupX, g_popupY);
}
