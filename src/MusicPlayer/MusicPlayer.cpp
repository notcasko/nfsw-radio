#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <stdint.h>
#include <ole2.h>
#include <gdiplus.h>
#include <tlhelp32.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// Playlist Order Modes
enum PlayOrder {
    ORDER_DEFAULT = 0,
    ORDER_REPEAT_PLAYLIST,
    ORDER_REPEAT_TRACK,
    ORDER_SHUFFLE,
    ORDER_COUNT
};

static const char* GetPlayOrderName(PlayOrder order)
{
    switch (order) {
    case ORDER_DEFAULT:         return "Default";
    case ORDER_REPEAT_PLAYLIST: return "Repeat Playlist";
    case ORDER_REPEAT_TRACK:    return "Repeat Track";
    case ORDER_SHUFFLE:         return "Shuffle";
    default:                    return "Default";
    }
}

// Popup Display States
enum PopupState {
    POPUP_IDLE,
    POPUP_HOLD,
    POPUP_FADE
};

// In-Game Track Entry Structure (264 bytes = 0x108)
#pragma pack(push, 1)
struct InGameTrackEntry {
    uint32_t trackId;
    uint32_t param1;
    char path[256];
};
#pragma pack(pop)

// In-Game Playlist Representation
struct InGamePlaylist {
    std::string name;
    std::vector<int> trackIndices;
};

// Global Mod State
static HINSTANCE g_hInstance = NULL;
static ULONG_PTR g_gdiplusToken = 0;

// Mouse Hook Globals
static HHOOK g_hMouseHook = NULL;
static DWORD g_mouseHookThreadId = 0;
static HANDLE g_hMouseThread = NULL;

// In-Game Music Logic Globals
static void* g_pMusicLogic = NULL;
typedef void (__thiscall *t_MusicLogic_Update)(void* thisPtr);
static t_MusicLogic_Update g_origMusicLogic_Update = NULL;

// Playlists and Playback State
static std::vector<InGamePlaylist> g_inGamePlaylists;
static int g_currentInGamePlaylist = 0;
static int g_currentInGameTrackInPlaylist = 0;
static int g_currentInGameTrackIndex = -1;
static PlayOrder g_inGamePlayOrder = ORDER_DEFAULT;
static float g_inGameMusicVolume = 1.0f;
static bool g_isStopped = false;

// Shuffle Deck State
static std::vector<int> g_inGameShuffleDeck;
static int g_shuffleDeckIndex = 0;

// Popup Window Globals
static const int INGAME_POPUP_W = 800;
static const int INGAME_POPUP_H = 44;
static bool g_cfgShowInGameMusicPopUp = false;
static HWND g_hInGamePopupWnd = NULL;
static CRITICAL_SECTION g_inGamePopupCs;
static PopupState g_inGamePopupState = POPUP_IDLE;
static DWORD g_inGamePopupHoldStart = 0;
static DWORD g_inGamePopupFadeStart = 0;
static std::string g_inGamePopupTrackName = "";

// Configurable Keybindings (NextTrack: Tab, NextPlaylist: CapsLock, Toggle: Q, Order: F, Volume: X)
static int g_cfgRadioKey = VK_TAB;                 // Base key (Next Track) - Default: Tab
static int g_cfgNextStationMod = VK_CAPITAL;       // Next Playlist modifier - Default: Caps Lock
static int g_cfgTogglePlaybackMod = 'Q';          // Stop / Resume modifier - Default: Q
static int g_cfgPlaylistOrderMod = 'F';            // Playlist Order modifier - Default: F
static int g_cfgVolumeMod = 'X';                  // Volume scroll modifier - Default: X

// Utility Functions
static std::string GetDirectoryOfFile(const std::string& path)
{
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash);
    }
    return ".";
}

static std::string GetFilenameWithoutExtension(const std::string& path)
{
    size_t lastSlash = path.find_last_of("\\/");
    std::string filename = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos && lastDot > 0) {
        filename = filename.substr(0, lastDot);
    }
    return filename;
}

static std::string TrimString(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static std::string WideToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (len > 0) {
        std::string res(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &res[0], len, NULL, NULL);
        return res;
    }
    return "";
}

static std::wstring StringToWide(const std::string& str)
{
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), (int)str.size(), NULL, 0);
    if (len > 0) {
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], len);
        return wstr;
    }
    // Try Japanese Shift-JIS (CP932) before fallback to system ANSI
    len = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, str.c_str(), (int)str.size(), NULL, 0);
    if (len > 0) {
        std::wstring wstr(len, 0);
        MultiByteToWideChar(932, 0, str.c_str(), (int)str.size(), &wstr[0], len);
        return wstr;
    }
    len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0);
    if (len > 0) {
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), &wstr[0], len);
        return wstr;
    }
    return std::wstring(str.begin(), str.end());
}

#if defined(_DEBUG) || defined(RADIOMOD_ENABLE_LOGGING)

static void LogMsg(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    static std::wstring s_logFileW;
    static bool s_logInit = false;
    if (!s_logInit) {
        char modPath[MAX_PATH];
        GetModuleFileNameA(g_hInstance, modPath, MAX_PATH);
        std::string modDir = GetDirectoryOfFile(modPath);
        std::string logFile = modDir + "\\RadioMod.log";
        s_logFileW = StringToWide(logFile);
        s_logInit = true;
    }

    FILE* f = _wfopen(s_logFileW.c_str(), L"a");
    if (!f) {
        f = _wfopen(L"RadioMod.log", L"a");
    }
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
}

#else

#define LogMsg(...) ((void)0)

#endif

// Shuffle Deck Helper Functions
static void ReshuffleDeck(int playlistIdx, int lastPlayedTrackInPl = -1)
{
    if (playlistIdx < 0 || playlistIdx >= (int)g_inGamePlaylists.size()) {
        g_inGameShuffleDeck.clear();
        g_shuffleDeckIndex = 0;
        return;
    }

    const InGamePlaylist& pl = g_inGamePlaylists[playlistIdx];
    int count = (int)pl.trackIndices.size();
    if (count <= 0) {
        g_inGameShuffleDeck.clear();
        g_shuffleDeckIndex = 0;
        return;
    }

    g_inGameShuffleDeck.resize(count);
    for (int i = 0; i < count; ++i) {
        g_inGameShuffleDeck[i] = i;
    }

    for (int i = count - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        std::swap(g_inGameShuffleDeck[i], g_inGameShuffleDeck[j]);
    }

    // Ensure the new deck does not immediately repeat the track that just finished
    if (count > 1 && lastPlayedTrackInPl >= 0 && g_inGameShuffleDeck[0] == lastPlayedTrackInPl) {
        int swapIdx = 1 + (rand() % (count - 1));
        std::swap(g_inGameShuffleDeck[0], g_inGameShuffleDeck[swapIdx]);
    }

    g_shuffleDeckIndex = 0;
}

static void SyncShuffleDeckToCurrentTrack(int playlistIdx, int currentTrackInPl)
{
    if (playlistIdx < 0 || playlistIdx >= (int)g_inGamePlaylists.size()) {
        g_inGameShuffleDeck.clear();
        g_shuffleDeckIndex = 0;
        return;
    }

    const InGamePlaylist& pl = g_inGamePlaylists[playlistIdx];
    int count = (int)pl.trackIndices.size();
    if (count <= 0) {
        g_inGameShuffleDeck.clear();
        g_shuffleDeckIndex = 0;
        return;
    }

    if (currentTrackInPl < 0 || currentTrackInPl >= count) {
        currentTrackInPl = 0;
    }

    g_inGameShuffleDeck.clear();
    g_inGameShuffleDeck.reserve(count);
    g_inGameShuffleDeck.push_back(currentTrackInPl);

    std::vector<int> others;
    others.reserve(count > 1 ? count - 1 : 0);
    for (int i = 0; i < count; ++i) {
        if (i != currentTrackInPl) {
            others.push_back(i);
        }
    }

    for (int i = (int)others.size() - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        std::swap(others[i], others[j]);
    }

    for (int idx : others) {
        g_inGameShuffleDeck.push_back(idx);
    }

    g_shuffleDeckIndex = 0;
}

// CJK and Font Support Helpers
static bool ContainsCjk(const std::wstring& text)
{
    for (wchar_t c : text) {
        if ((c >= 0x3000 && c <= 0x303F) || // CJK symbols and punctuation
            (c >= 0x3040 && c <= 0x309F) || // Hiragana
            (c >= 0x30A0 && c <= 0x30FF) || // Katakana
            (c >= 0x31F0 && c <= 0x31FF) || // Katakana Phonetic Extensions
            (c >= 0x3200 && c <= 0x32FF) || // Enclosed CJK Letters and Months
            (c >= 0x3400 && c <= 0x4DBF) || // CJK Unified Ideographs Extension A
            (c >= 0x4E00 && c <= 0x9FFF) || // CJK Unified Ideographs
            (c >= 0xF900 && c <= 0xFAFF) || // CJK Compatibility Ideographs
            (c >= 0xFF00 && c <= 0xFFEF) || // Halfwidth and Fullwidth Forms
            (c >= 0xAC00 && c <= 0xD7AF))   // Hangul Syllables
        {
            return true;
        }
    }
    return false;
}

static bool IsKnownCjkFont(const std::wstring& fontName)
{
    std::wstring lower = fontName;
    for (wchar_t& c : lower) c = towlower(c);
    if (lower.find(L"gothic") != std::wstring::npos ||
        lower.find(L"meiryo") != std::wstring::npos ||
        lower.find(L"mincho") != std::wstring::npos ||
        lower.find(L"yahei") != std::wstring::npos ||
        lower.find(L"jhenghei") != std::wstring::npos ||
        lower.find(L"malgun") != std::wstring::npos ||
        lower.find(L"simsun") != std::wstring::npos ||
        lower.find(L"simhei") != std::wstring::npos ||
        lower.find(L"mingliu") != std::wstring::npos ||
        lower.find(L"yu ") != std::wstring::npos ||
        lower.find(L"arial unicode") != std::wstring::npos) {
        return true;
    }
    return false;
}

static bool DoesFontSupportWideString(const std::wstring& fontName, const std::wstring& text)
{
    if (text.empty()) return true;

    bool hasNonAscii = false;
    for (wchar_t c : text) {
        if (c > 127) {
            hasNonAscii = true;
            break;
        }
    }
    if (!hasNonAscii) return true;

    if (ContainsCjk(text)) {
        if (!IsKnownCjkFont(fontName)) return false;
        Gdiplus::FontFamily fam(fontName.c_str());
        return fam.IsAvailable();
    }

    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) return true;

    HFONT hFont = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        fontName.c_str()
    );
    if (!hFont) {
        DeleteDC(hdc);
        return false;
    }

    HGDIOBJ oldFont = SelectObject(hdc, hFont);

    std::wstring toTest;
    toTest.reserve(text.size());
    for (wchar_t c : text) {
        if (c > 32) toTest.push_back(c);
    }

    bool allSupported = true;
    if (!toTest.empty()) {
        std::vector<WORD> glyphs(toTest.size());
        DWORD res = GetGlyphIndicesW(hdc, toTest.c_str(), (int)toTest.size(), glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS);
        if (res != GDI_ERROR) {
            for (WORD g : glyphs) {
                if (g == 0xFFFF) {
                    allSupported = false;
                    break;
                }
            }
        }
    }

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
    DeleteDC(hdc);

    return allSupported;
}

static std::wstring GetBestFontForText(const std::wstring& primaryFont, const std::wstring& text)
{
    if (text.empty()) return primaryFont;

    bool hasCjk = ContainsCjk(text);

    if (hasCjk) {
        if (IsKnownCjkFont(primaryFont)) {
            Gdiplus::FontFamily fam(primaryFont.c_str());
            if (fam.IsAvailable()) return primaryFont;
        }

        static const wchar_t* cjkCandidates[] = {
            L"Yu Gothic UI",
            L"Meiryo UI",
            L"Meiryo",
            L"MS UI Gothic",
            L"MS Gothic",
            L"Yu Gothic",
            L"Microsoft YaHei UI",
            L"Microsoft YaHei",
            L"Microsoft JhengHei UI",
            L"Microsoft JhengHei",
            L"Malgun Gothic",
            L"Segoe UI Symbol",
            L"Arial Unicode MS"
        };

        for (const wchar_t* cand : cjkCandidates) {
            Gdiplus::FontFamily fam(cand);
            if (fam.IsAvailable()) {
                return cand;
            }
        }
    }

    {
        Gdiplus::FontFamily fam(primaryFont.c_str());
        if (fam.IsAvailable()) {
            if (DoesFontSupportWideString(primaryFont, text)) {
                return primaryFont;
            }
        }
    }

    static const wchar_t* generalCandidates[] = {
        L"Segoe UI",
        L"Segoe UI Symbol",
        L"Arial",
        L"Tahoma",
        L"Yu Gothic UI",
        L"Meiryo",
        L"MS Gothic"
    };

    for (const wchar_t* cand : generalCandidates) {
        Gdiplus::FontFamily fam(cand);
        if (fam.IsAvailable()) {
            if (DoesFontSupportWideString(cand, text)) {
                return cand;
            }
        }
    }

    for (const wchar_t* cand : generalCandidates) {
        Gdiplus::FontFamily fam(cand);
        if (fam.IsAvailable()) {
            return cand;
        }
    }

    return primaryFont;
}

// INI Configuration Handling
static std::string GetSettingsDir()
{
    static std::string cachedDir = "";
    if (!cachedDir.empty()) return cachedDir;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = GetDirectoryOfFile(exePath);

    char modPath[MAX_PATH];
    GetModuleFileNameA(g_hInstance, modPath, MAX_PATH);
    std::string modDir = GetDirectoryOfFile(modPath);
    std::string modBase = GetFilenameWithoutExtension(modPath);
    if (modBase.empty()) modBase = "MusicPlayer";

    std::vector<std::string> candidates = {
        exeDir + "\\Settings\\MusicPlayer",
        exeDir + "\\Settings\\" + modBase,
        exeDir + "\\Settings\\InGameMusic",
        exeDir + "\\Settings\\InGameMusicMod",
        exeDir + "\\Settings\\Radio",
        modDir + "\\..\\Settings\\MusicPlayer",
        modDir + "\\..\\Settings\\" + modBase,
        modDir + "\\..\\Settings\\InGameMusic",
        modDir + "\\..\\Settings\\InGameMusicMod",
        modDir + "\\..\\Settings\\Radio",
        modDir + "\\Settings\\MusicPlayer",
        modDir + "\\Settings\\" + modBase,
        modDir + "\\Settings\\InGameMusic",
        modDir + "\\Settings\\InGameMusicMod",
        modDir + "\\Settings\\Radio"
    };

    for (const auto& cand : candidates) {
        std::string testIni0 = cand + "\\MusicPlayer.ini";
        std::string testIni1 = cand + "\\" + modBase + ".ini";
        std::string testIni2 = cand + "\\InGameMusic.ini";
        std::string testIni3 = cand + "\\InGameMusicMod.ini";
        std::string testIni4 = cand + "\\RadioMod.ini";
        std::string testIni5 = cand + "\\Radio.ini";
        if (GetFileAttributesW(StringToWide(testIni0).c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(StringToWide(testIni1).c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(StringToWide(testIni2).c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(StringToWide(testIni3).c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(StringToWide(testIni4).c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(StringToWide(testIni5).c_str()) != INVALID_FILE_ATTRIBUTES) {
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
    std::string modSettings = settingsParent + "\\MusicPlayer";

    CreateDirectoryA(settingsParent.c_str(), NULL);
    CreateDirectoryA(modSettings.c_str(), NULL);

    cachedDir = modSettings;
    return cachedDir;
}

static std::string GetIniPath()
{
    static std::string cachedPath = "";
    if (!cachedPath.empty()) return cachedPath;

    std::string dir = GetSettingsDir();
    char modPath[MAX_PATH];
    GetModuleFileNameA(g_hInstance, modPath, MAX_PATH);
    std::string modBase = GetFilenameWithoutExtension(modPath);
    if (modBase.empty()) modBase = "MusicPlayer";

    std::string cand0 = dir + "\\MusicPlayer.ini";
    std::string cand1 = dir + "\\" + modBase + ".ini";
    std::string cand2 = dir + "\\InGameMusic.ini";
    std::string cand3 = dir + "\\InGameMusicMod.ini";
    std::string cand4 = dir + "\\RadioMod.ini";
    std::string cand5 = dir + "\\Radio.ini";

    if (GetFileAttributesW(StringToWide(cand0).c_str()) != INVALID_FILE_ATTRIBUTES) {
        cachedPath = cand0;
    } else if (GetFileAttributesW(StringToWide(cand1).c_str()) != INVALID_FILE_ATTRIBUTES) {
        cachedPath = cand1;
    } else if (GetFileAttributesW(StringToWide(cand2).c_str()) != INVALID_FILE_ATTRIBUTES) {
        cachedPath = cand2;
    } else if (GetFileAttributesW(StringToWide(cand3).c_str()) != INVALID_FILE_ATTRIBUTES) {
        cachedPath = cand3;
    } else if (GetFileAttributesW(StringToWide(cand4).c_str()) != INVALID_FILE_ATTRIBUTES) {
        cachedPath = cand4;
    } else if (GetFileAttributesW(StringToWide(cand5).c_str()) != INVALID_FILE_ATTRIBUTES) {
        cachedPath = cand5;
    } else {
        cachedPath = cand0; // Name config file MusicPlayer.ini when creating
    }

    return cachedPath;
}

static std::string ReadIniStringUtf8(const std::string& iniPath, const std::string& section, const std::string& key, const std::string& defaultVal)
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

static int ParseKeyString(const std::string& rawKey, int defaultKey)
{
    std::string key = TrimString(rawKey);
    while (key.size() >= 2 && ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\''))) {
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

static void SaveConfigVolume()
{
    int volPercent = (int)std::round(g_inGameMusicVolume * 100.0f);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", volPercent);
    WritePrivateProfileStringW(L"General", L"Volume", StringToWide(buf).c_str(), StringToWide(GetIniPath()).c_str());
}

static void CreateDefaultConfig(const std::string& iniPath)
{
    FILE* f = _wfopen(StringToWide(iniPath).c_str(), L"w");
    if (!f) {
        LogMsg("Failed to create default config file at: %s", iniPath.c_str());
        return;
    }

    const char* defaultContent =
        "[General]\n"
        "; In-game music playback volume (0% - 100%)\n"
        "Volume = 100%\n"
        "; Show pop-up notification when switching in-game music tracks (1 = enabled, 0 = disabled)\n"
        "ShowInGameMusicPopup = 0\n\n"
        "[Keybindings]\n"
        "; Keybindings configuration for Music Player\n"
        "; Next track in current playlist (Default: Tab)\n"
        "RadioKey = Tab\n\n"
        "; Modifier to switch to next playlist when pressed with RadioKey (Default: Caps Lock)\n"
        "NextStationModifier = Caps Lock\n\n"
        "; Modifier to toggle playback (stop/resume) when pressed with RadioKey (Default: Q)\n"
        "TogglePlaybackModifier = Q\n\n"
        "; Modifier to scroll playlist order when pressed with RadioKey (Default: F)\n"
        "PlaylistOrderModifier = F\n\n"
        "; Modifier held while scrolling mouse wheel to adjust volume (Default: X)\n"
        "VolumeModifier = X\n";

    fputs(defaultContent, f);
    fclose(f);
    LogMsg("Created default configuration file: %s", iniPath.c_str());
}

static void LoadConfig()
{
    std::string iniPath = GetIniPath();

    DWORD attr = GetFileAttributesW(StringToWide(iniPath).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        LogMsg("Config file not found. Creating default: %s", iniPath.c_str());
        CreateDefaultConfig(iniPath);
    } else {
        LogMsg("Found config file: %s", iniPath.c_str());
    }

    // 1. Volume
    std::string volStr = ReadIniStringUtf8(iniPath, "General", "Volume", "");
    if (!volStr.empty()) {
        float val = (float)atof(volStr.c_str());
        if (volStr.find('%') != std::string::npos || val > 1.0f) {
            g_inGameMusicVolume = val / 100.0f;
        } else if (val >= 0.0f) {
            g_inGameMusicVolume = val;
        }
        if (g_inGameMusicVolume < 0.0f) g_inGameMusicVolume = 0.0f;
        if (g_inGameMusicVolume > 1.0f) g_inGameMusicVolume = 1.0f;
    } else {
        g_inGameMusicVolume = 1.0f;
    }

    // 2. In-Game Music Popup Setting
    g_cfgShowInGameMusicPopUp = (GetPrivateProfileIntA("General", "ShowInGameMusicPopup", 0, iniPath.c_str()) != 0);
    int igSetting = GetPrivateProfileIntA("InGameMusic", "ShowPopup", -1, iniPath.c_str());
    if (igSetting != -1) {
        g_cfgShowInGameMusicPopUp = (igSetting != 0);
    }

    // 3. Keybindings
    std::string radioKeyStr = ReadIniStringUtf8(iniPath, "Keybindings", "RadioKey", "");
    if (radioKeyStr.empty()) radioKeyStr = ReadIniStringUtf8(iniPath, "Keybindings", "NextTrackKey", "");
    g_cfgRadioKey = ParseKeyString(radioKeyStr, VK_TAB);

    std::string nextStationStr = ReadIniStringUtf8(iniPath, "Keybindings", "NextStationModifier", "");
    if (nextStationStr.empty()) nextStationStr = ReadIniStringUtf8(iniPath, "Keybindings", "NextStationKey", "");
    g_cfgNextStationMod = ParseKeyString(nextStationStr, VK_CAPITAL);

    std::string toggleStr = ReadIniStringUtf8(iniPath, "Keybindings", "TogglePlaybackModifier", "");
    if (toggleStr.empty()) toggleStr = ReadIniStringUtf8(iniPath, "Keybindings", "TogglePlaybackKey", "");
    g_cfgTogglePlaybackMod = ParseKeyString(toggleStr, 'Q');

    std::string orderStr = ReadIniStringUtf8(iniPath, "Keybindings", "PlaylistOrderModifier", "");
    if (orderStr.empty()) orderStr = ReadIniStringUtf8(iniPath, "Keybindings", "PlaylistOrderKey", "");
    g_cfgPlaylistOrderMod = ParseKeyString(orderStr, 'F');

    std::string volModStr = ReadIniStringUtf8(iniPath, "Keybindings", "VolumeModifier", "");
    if (volModStr.empty()) volModStr = ReadIniStringUtf8(iniPath, "Keybindings", "VolumeKey", "");
    g_cfgVolumeMod = ParseKeyString(volModStr, 'X');

    LogMsg("Active Keybindings: NextTrack=0x%02X, NextPlaylistMod=0x%02X, ToggleMod=0x%02X, OrderMod=0x%02X, VolumeMod=0x%02X, ShowPopup=%d",
           g_cfgRadioKey, g_cfgNextStationMod, g_cfgTogglePlaybackMod, g_cfgPlaylistOrderMod, g_cfgVolumeMod, g_cfgShowInGameMusicPopUp ? 1 : 0);
}

// Built-in Game Engine Interaction (Sound::MusicLogic)
static t_MusicLogic_Update g_chainMusicLogic_Update = NULL;

static void __fastcall Hooked_MusicLogic_Update(void* thisPtr, void* edx)
{
    g_pMusicLogic = thisPtr;
    t_MusicLogic_Update target = g_origMusicLogic_Update ? g_origMusicLogic_Update : g_chainMusicLogic_Update;
    if (target) {
        target(thisPtr);
    }
}

static void InstallMusicLogicHook()
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t delta = base - 0x00400000;
    void** pVtableEntry = (void**)(0x00bd8a30 + delta);

    DWORD oldProt = 0;
    if (VirtualProtect(pVtableEntry, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
        g_origMusicLogic_Update = (t_MusicLogic_Update)*pVtableEntry;
        g_chainMusicLogic_Update = g_origMusicLogic_Update;
        *pVtableEntry = (void*)&Hooked_MusicLogic_Update;
        VirtualProtect(pVtableEntry, sizeof(void*), oldProt, &oldProt);
        LogMsg("Hooked MusicLogic::Update vtable at 0x%p (orig: 0x%p)", pVtableEntry, g_origMusicLogic_Update);
    }
}

static void* GetMusicLogicInstance()
{
    if (g_pMusicLogic) return g_pMusicLogic;

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t delta = base - 0x00400000;
    uintptr_t targetVtable = 0x00bd8a0c + delta;

    uintptr_t addr = 0x10000;
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < 0x7fff0000) {
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & PAGE_GUARD) &&
            mbi.RegionSize <= 32 * 1024 * 1024) {
            uintptr_t* pWords = (uintptr_t*)mbi.BaseAddress;
            size_t count = mbi.RegionSize / sizeof(uintptr_t);
            for (size_t i = 0; i < count; ++i) {
                if (pWords[i] == targetVtable) {
                    uintptr_t candidate = (uintptr_t)&pWords[i];
                    if (candidate != targetVtable && (i + (0x68 / sizeof(uintptr_t)) < count)) {
                        __try {
                            uintptr_t tStart = *(uintptr_t*)(candidate + 0x64);
                            uintptr_t tEnd   = *(uintptr_t*)(candidate + 0x68);
                            if (tStart != 0 && tEnd > tStart && ((tEnd - tStart) % 0x108 == 0)) {
                                g_pMusicLogic = (void*)candidate;
                                LogMsg("Discovered Sound::MusicLogic instance via memory scan: 0x%p", g_pMusicLogic);
                                return g_pMusicLogic;
                            }
                        } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
            }
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return g_pMusicLogic;
}

static uint32_t SafeGetMusicLogicState(void* pLogic)
{
    if (!pLogic) return 0;
    __try {
        return *(uint32_t*)((uintptr_t)pLogic + 0x50);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static bool SafeReadTablePointers(void* pLogic, uintptr_t* pStart, uintptr_t* pEnd)
{
    if (!pLogic || !pStart || !pEnd) return false;
    __try {
        *pStart = *(uintptr_t*)((uintptr_t)pLogic + 0x64);
        *pEnd   = *(uintptr_t*)((uintptr_t)pLogic + 0x68);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeGetTrackPath(const InGameTrackEntry* pTrack, char* outBuf, size_t maxLen)
{
    if (!pTrack || !outBuf || maxLen == 0) return false;
    __try {
        strncpy_s(outBuf, maxLen, pTrack->path, _TRUNCATE);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeInvokeStopTrack(void* pLogic, uintptr_t stopFn)
{
    if (!pLogic || !stopFn) return;
    __try {
        uint32_t state = *(uint32_t*)((uintptr_t)pLogic + 0x50);
        uintptr_t pVoice28 = *(uintptr_t*)((uintptr_t)pLogic + 0x28);
        uintptr_t pVoice30 = *(uintptr_t*)((uintptr_t)pLogic + 0x30);
        uintptr_t pVoice38 = *(uintptr_t*)((uintptr_t)pLogic + 0x38);
        if (state == 4 && pVoice28 != 0 && pVoice30 != 0 && pVoice38 != 0) {
            typedef void (__thiscall *t_StopTrack)(void* thisPtr);
            ((t_StopTrack)stopFn)(pLogic);
            LogMsg("StopGameMusic: Successfully stopped active in-game music stream.");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void SafeInvokeCrossfade(void* pLogic, uintptr_t crossfadeFn, float vol)
{
    if (!pLogic || !crossfadeFn) return;
    __try {
        uintptr_t pVoice40 = *(uintptr_t*)((uintptr_t)pLogic + 0x40);
        uintptr_t pVoice38 = *(uintptr_t*)((uintptr_t)pLogic + 0x38);
        if (pVoice40 != 0 && pVoice38 != 0) {
            typedef void (__thiscall *t_SetCrossfade)(void* thisPtr, float a1, float a2, int a3, int a4);
            ((t_SetCrossfade)crossfadeFn)(pLogic, 0.002f, vol, 0, 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void SafeInvokePlayback(void* pLogic, uint32_t trackId, float vol, uintptr_t stopFn, uintptr_t selectFn, uintptr_t playFn, uintptr_t crossfadeFn)
{
    if (!pLogic) return;
    __try {
        uint32_t state = *(uint32_t*)((uintptr_t)pLogic + 0x50);
        uintptr_t pVoice28 = *(uintptr_t*)((uintptr_t)pLogic + 0x28);
        uintptr_t pVoice30 = *(uintptr_t*)((uintptr_t)pLogic + 0x30);
        uintptr_t pVoice38 = *(uintptr_t*)((uintptr_t)pLogic + 0x38);
        uintptr_t pVoice40 = *(uintptr_t*)((uintptr_t)pLogic + 0x40);
        if (state == 4 && pVoice28 != 0 && pVoice30 != 0 && pVoice38 != 0 && stopFn) {
            typedef void (__thiscall *t_StopTrack)(void* thisPtr);
            ((t_StopTrack)stopFn)(pLogic);
        }
        if (selectFn) {
            typedef bool (__thiscall *t_SelectTrack)(void* thisPtr, uint32_t trackId);
            ((t_SelectTrack)selectFn)(pLogic, trackId);
        }
        if (playFn && pVoice40 != 0 && pVoice38 != 0) {
            typedef void (__thiscall *t_PlayTrack)(void* thisPtr);
            ((t_PlayTrack)playFn)(pLogic);
        }
        if (crossfadeFn && pVoice40 != 0 && pVoice38 != 0) {
            typedef void (__thiscall *t_SetCrossfade)(void* thisPtr, float a1, float a2, int a3, int a4);
            ((t_SetCrossfade)crossfadeFn)(pLogic, 0.002f, vol, 0, 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void StopGameMusic()
{
    void* pLogic = g_pMusicLogic ? g_pMusicLogic : GetMusicLogicInstance();
    if (!pLogic) return;

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t delta = base - 0x00400000;
    SafeInvokeStopTrack(pLogic, 0x00816b90 + delta);
}

static void SetInGameMusicBusVolume(float vol)
{
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t delta = base - 0x00400000;

    typedef int (__cdecl *t_SetAudioOptionVol)(float* pVol);
    t_SetAudioOptionVol pfnSetInteractiveMusic = (t_SetAudioOptionVol)(0x00437280 + delta);
    t_SetAudioOptionVol pfnSetFrontendMusic    = (t_SetAudioOptionVol)(0x004372d0 + delta);

    float v = vol;
    __try {
        if (pfnSetInteractiveMusic) pfnSetInteractiveMusic(&v);
        if (pfnSetFrontendMusic)    pfnSetFrontendMusic(&v);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void SetInGameMusicVolume(float vol)
{
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    g_inGameMusicVolume = vol;

    SetInGameMusicBusVolume(vol);

    void* pLogic = g_pMusicLogic ? g_pMusicLogic : GetMusicLogicInstance();
    if (pLogic) {
        uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
        uintptr_t delta = base - 0x00400000;
        SafeInvokeCrossfade(pLogic, 0x00816390 + delta, vol);
    }
}

static std::string ExtractInGamePlaylistName(const char* rawPath)
{
    if (!rawPath || !*rawPath) return "OTHER";
    std::string s = rawPath;

    size_t lastSlash = s.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        s = s.substr(lastSlash + 1);
    }

    size_t underscore = s.find('_');
    if (underscore != std::string::npos && underscore > 0) {
        s = s.substr(0, underscore);
    } else {
        size_t lastDot = s.rfind('.');
        if (lastDot != std::string::npos) {
            s = s.substr(0, lastDot);
        }
    }

    std::string result = "";
    for (char c : s) {
        result += (char)toupper((unsigned char)c);
    }
    return result.empty() ? "OTHER" : result;
}

static bool BuildInGamePlaylists()
{
    void* pLogic = g_pMusicLogic ? g_pMusicLogic : GetMusicLogicInstance();
    if (!pLogic) return false;

    uintptr_t tableStart = 0;
    uintptr_t tableEnd = 0;
    if (!SafeReadTablePointers(pLogic, &tableStart, &tableEnd) || !tableStart || tableEnd <= tableStart) return false;

    int totalTracks = (int)((tableEnd - tableStart) / sizeof(InGameTrackEntry));
    if (totalTracks <= 0) return false;

    InGameTrackEntry* tracks = (InGameTrackEntry*)tableStart;

    g_inGamePlaylists.clear();
    std::map<std::string, size_t> playlistMap;

    for (int i = 0; i < totalTracks; ++i) {
        char pathBuf[256] = {0};
        if (!SafeGetTrackPath(&tracks[i], pathBuf, sizeof(pathBuf))) continue;
        std::string plName = ExtractInGamePlaylistName(pathBuf);
        auto it = playlistMap.find(plName);
        if (it == playlistMap.end()) {
            InGamePlaylist pl;
            pl.name = plName;
            pl.trackIndices.push_back(i);
            playlistMap[plName] = g_inGamePlaylists.size();
            g_inGamePlaylists.push_back(pl);
        } else {
            g_inGamePlaylists[it->second].trackIndices.push_back(i);
        }
    }

    LogMsg("Discovered %d in-game playlists with %d total tracks:", (int)g_inGamePlaylists.size(), totalTracks);
    for (size_t i = 0; i < g_inGamePlaylists.size(); ++i) {
        LogMsg("  Playlist [%d] '%s': %d tracks", (int)i, g_inGamePlaylists[i].name.c_str(), (int)g_inGamePlaylists[i].trackIndices.size());
    }

    return !g_inGamePlaylists.empty();
}

static void EnsureInGamePlaylists()
{
    if (g_inGamePlaylists.empty()) {
        BuildInGamePlaylists();
    }
}

static std::string FormatInGameTrackName(const char* rawPath)
{
    if (!rawPath || !*rawPath) return "Unknown Track";
    std::string s = rawPath;

    size_t lastSlash = s.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        s = s.substr(lastSlash + 1);
    }

    size_t lastDot = s.rfind('.');
    if (lastDot != std::string::npos) {
        s = s.substr(0, lastDot);
    }

    size_t loopPos = s.rfind("_Loopable");
    if (loopPos == std::string::npos) loopPos = s.rfind("_loopable");
    if (loopPos != std::string::npos) {
        s = s.substr(0, loopPos);
    }

    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '_') s[i] = ' ';
    }

    bool newWord = true;
    for (size_t i = 0; i < s.length(); ++i) {
        if (isspace((unsigned char)s[i])) {
            newWord = true;
        } else if (newWord) {
            s[i] = (char)toupper((unsigned char)s[i]);
            newWord = false;
        }
    }

    return s;
}

// In-Game Music Pop-Up Window Rendering
static void RenderInGamePopup(int alpha)
{
    if (!g_hInGamePopupWnd) return;

    const int w = INGAME_POPUP_W;
    const int h = INGAME_POPUP_H;

    HDC hdcScreen = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP memBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    memset(pBits, 0, w * h * 4);

    {
        Gdiplus::Bitmap dibBmp(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)pBits);
        Gdiplus::Graphics g(&dibBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

        EnterCriticalSection(&g_inGamePopupCs);
        std::string trackName = g_inGamePopupTrackName;
        LeaveCriticalSection(&g_inGamePopupCs);

        std::wstring wTrack = StringToWide(trackName);
        for (wchar_t& wc : wTrack) {
            wc = towupper(wc);
        }

        std::wstring igFontName = GetBestFontForText(L"Segoe UI", wTrack);
        Gdiplus::FontFamily fontFamily(igFontName.c_str());
        const Gdiplus::FontFamily* pFontFam = fontFamily.IsAvailable() ? &fontFamily : NULL;
        Gdiplus::FontFamily fallbackFam(L"Arial");
        const Gdiplus::FontFamily* pUseFam = pFontFam ? pFontFam : &fallbackFam;

        Gdiplus::FontStyle igStyle = Gdiplus::FontStyleRegular;
        if (pUseFam && pUseFam->IsStyleAvailable(Gdiplus::FontStyleItalic)) {
            igStyle = Gdiplus::FontStyleItalic;
        }

        Gdiplus::Font* pTitleFont = NULL;
        if (pUseFam) {
            pTitleFont = new Gdiplus::Font(pUseFam, 14.0f, igStyle, Gdiplus::UnitPoint);
            if (!pTitleFont || pTitleFont->GetLastStatus() != Gdiplus::Ok) {
                delete pTitleFont;
                pTitleFont = NULL;
            }
        }
        if (!pTitleFont) {
            pTitleFont = new Gdiplus::Font(L"Arial", 14.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        }

        Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));

        Gdiplus::RectF titleRect(0.0f, 0.0f, (Gdiplus::REAL)w, (Gdiplus::REAL)h);
        Gdiplus::StringFormat strFormat;
        strFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
        strFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        strFormat.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        strFormat.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        g.DrawString(wTrack.c_str(), -1, pTitleFont, titleRect, &strFormat, &titleBrush);
        delete pTitleFont;
    }

    POINT ptSrc = {0, 0};
    SIZE sizeWnd = {w, h};
    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = (BYTE)alpha;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(g_hInGamePopupWnd, NULL, NULL, &sizeWnd, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, hdcScreen);
}

static void TriggerInGameMusicPopup(const std::string& trackName, bool force = false)
{
    if (!force && !g_cfgShowInGameMusicPopUp) return;

    EnterCriticalSection(&g_inGamePopupCs);
    g_inGamePopupTrackName = trackName;
    g_inGamePopupState = POPUP_HOLD;
    g_inGamePopupHoldStart = GetTickCount();
    LeaveCriticalSection(&g_inGamePopupCs);
    
    if (g_hInGamePopupWnd) {
        HWND hGame = FindWindowA("GameFrame", "GameFrame");
        if (!hGame) hGame = GetForegroundWindow();

        RECT rc = {0};
        int gameW = 0, gameH = 0;
        if (hGame && GetWindowRect(hGame, &rc)) {
            gameW = rc.right - rc.left;
            gameH = rc.bottom - rc.top;
        } else {
            rc.left = 0; rc.top = 0;
            gameW = GetSystemMetrics(SM_CXSCREEN);
            gameH = GetSystemMetrics(SM_CYSCREEN);
        }

        const int popW = INGAME_POPUP_W;
        const int popH = INGAME_POPUP_H;
        int posX = rc.left + (gameW - popW) / 2;
        int posY = rc.top + (int)(gameH * 0.18f);

        SetWindowPos(g_hInGamePopupWnd, HWND_TOPMOST, posX, posY, popW, popH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RenderInGamePopup(245);
    }
}

// In-Game Music Playback Actions
static void PlayInGameTrackByIndex(int globalIdx, const std::string& customPrefix = "")
{
    void* pLogic = g_pMusicLogic ? g_pMusicLogic : GetMusicLogicInstance();
    if (!pLogic) {
        LogMsg("PlayInGameTrackByIndex: MusicLogic instance not available.");
        return;
    }

    uintptr_t tableStart = 0;
    uintptr_t tableEnd = 0;
    if (!SafeReadTablePointers(pLogic, &tableStart, &tableEnd) || !tableStart || tableEnd <= tableStart) {
        LogMsg("PlayInGameTrackByIndex: MusicLogic track table is empty or invalid.");
        return;
    }

    int totalTracks = (int)((tableEnd - tableStart) / sizeof(InGameTrackEntry));
    if (totalTracks <= 0 || globalIdx < 0 || globalIdx >= totalTracks) return;

    InGameTrackEntry* tracks = (InGameTrackEntry*)tableStart;
    const InGameTrackEntry& nextTrack = tracks[globalIdx];
    g_currentInGameTrackIndex = globalIdx;

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t delta = base - 0x00400000;

    SafeInvokePlayback(pLogic, nextTrack.trackId, g_inGameMusicVolume,
                       0x00816b90 + delta, 0x0081dbb0 + delta, 0x008169b0 + delta, 0x00816390 + delta);

    SetInGameMusicVolume(g_inGameMusicVolume);
    g_isStopped = false;

    std::string formattedName = FormatInGameTrackName(nextTrack.path);
    LogMsg("Switched in-game track [%d/%d]: ID=0x%08X, Path='%s', Title='%s'",
           globalIdx + 1, totalTracks, nextTrack.trackId, nextTrack.path, formattedName.c_str());

    std::string msg = "";
    if (!customPrefix.empty()) {
        msg = customPrefix + " - " + formattedName;
    } else {
        if (!g_inGamePlaylists.empty() && g_currentInGamePlaylist >= 0 && g_currentInGamePlaylist < (int)g_inGamePlaylists.size()) {
            const std::string& plName = g_inGamePlaylists[g_currentInGamePlaylist].name;
            std::string upperFormatted = formattedName;
            for (char& c : upperFormatted) c = (char)toupper((unsigned char)c);
            if (upperFormatted.find(plName) != 0) {
                msg = plName + " - " + formattedName;
            } else {
                msg = formattedName;
            }
        } else {
            msg = formattedName;
        }
    }
    TriggerInGameMusicPopup(msg);
}

static void SwitchInGameTrack()
{
    EnsureInGamePlaylists();
    if (g_inGamePlaylists.empty()) {
        LogMsg("SwitchInGameTrack: No in-game playlists found.");
        return;
    }

    if (g_currentInGamePlaylist < 0 || g_currentInGamePlaylist >= (int)g_inGamePlaylists.size()) {
        g_currentInGamePlaylist = 0;
    }

    InGamePlaylist& pl = g_inGamePlaylists[g_currentInGamePlaylist];
    if (pl.trackIndices.empty()) return;

    if (g_currentInGameTrackInPlaylist < 0 || g_currentInGameTrackInPlaylist >= (int)pl.trackIndices.size()) {
        g_currentInGameTrackInPlaylist = 0;
    }

    int nextIdxInPl = 0;
    if (g_inGamePlayOrder == ORDER_SHUFFLE) {
        if (g_inGameShuffleDeck.empty() || (int)g_inGameShuffleDeck.size() != (int)pl.trackIndices.size()) {
            SyncShuffleDeckToCurrentTrack(g_currentInGamePlaylist, g_currentInGameTrackInPlaylist);
        }

        g_shuffleDeckIndex++;
        if (g_shuffleDeckIndex >= (int)g_inGameShuffleDeck.size()) {
            int lastTrack = g_inGameShuffleDeck.empty() ? -1 : g_inGameShuffleDeck.back();
            ReshuffleDeck(g_currentInGamePlaylist, lastTrack);
        }

        if (!g_inGameShuffleDeck.empty()) {
            nextIdxInPl = g_inGameShuffleDeck[g_shuffleDeckIndex % (int)g_inGameShuffleDeck.size()];
        } else {
            nextIdxInPl = 0;
        }
    } else {
        // ORDER_DEFAULT, ORDER_REPEAT_PLAYLIST, and manual advance during ORDER_REPEAT_TRACK:
        // All loop back silently to track 0 when reaching the end so music never stops.
        nextIdxInPl = (g_currentInGameTrackInPlaylist + 1) % (int)pl.trackIndices.size();
    }

    int prevIdxInPl = g_currentInGameTrackInPlaylist;
    g_currentInGameTrackInPlaylist = nextIdxInPl;
    int globalIdx = pl.trackIndices[nextIdxInPl];

    LogMsg("Switching to next in-game track in playlist '%s' [%d -> %d] (Order: %s)",
           pl.name.c_str(), prevIdxInPl, nextIdxInPl, GetPlayOrderName(g_inGamePlayOrder));
    PlayInGameTrackByIndex(globalIdx);
}

static void SwitchInGamePlaylist()
{
    EnsureInGamePlaylists();
    if (g_inGamePlaylists.empty()) {
        LogMsg("SwitchInGamePlaylist: No in-game playlists found.");
        return;
    }

    g_currentInGamePlaylist = (g_currentInGamePlaylist + 1) % (int)g_inGamePlaylists.size();

    InGamePlaylist& pl = g_inGamePlaylists[g_currentInGamePlaylist];
    if (pl.trackIndices.empty()) return;

    int nextIdxInPl = 0;
    if (g_inGamePlayOrder == ORDER_SHUFFLE) {
        ReshuffleDeck(g_currentInGamePlaylist);
        if (!g_inGameShuffleDeck.empty()) {
            nextIdxInPl = g_inGameShuffleDeck[0];
            g_shuffleDeckIndex = 0;
        } else {
            nextIdxInPl = 0;
        }
    } else {
        nextIdxInPl = 0;
    }

    g_currentInGameTrackInPlaylist = nextIdxInPl;
    int globalIdx = pl.trackIndices[nextIdxInPl];
    LogMsg("Switched in-game playlist [%d/%d]: '%s' (Order: %s, Track: %d)",
           g_currentInGamePlaylist + 1, (int)g_inGamePlaylists.size(), pl.name.c_str(),
           GetPlayOrderName(g_inGamePlayOrder), nextIdxInPl);

    PlayInGameTrackByIndex(globalIdx, "PLAYLIST: " + pl.name);
}

static void ToggleInGamePlayback()
{
    if (!g_isStopped) {
        LogMsg("User stopped in-game music playback.");
        StopGameMusic();
        g_isStopped = true;
        TriggerInGameMusicPopup("STOPPED", true);
    } else {
        LogMsg("User resumed in-game music playback.");
        g_isStopped = false;
        EnsureInGamePlaylists();
        if (!g_inGamePlaylists.empty() && g_currentInGamePlaylist >= 0 && g_currentInGamePlaylist < (int)g_inGamePlaylists.size()) {
            InGamePlaylist& pl = g_inGamePlaylists[g_currentInGamePlaylist];
            if (!pl.trackIndices.empty()) {
                if (g_inGamePlayOrder == ORDER_SHUFFLE &&
                    (g_inGameShuffleDeck.empty() || (int)g_inGameShuffleDeck.size() != (int)pl.trackIndices.size())) {
                    SyncShuffleDeckToCurrentTrack(g_currentInGamePlaylist, g_currentInGameTrackInPlaylist);
                }
                int trk = pl.trackIndices[g_currentInGameTrackInPlaylist % pl.trackIndices.size()];
                PlayInGameTrackByIndex(trk);
            }
        }
    }
}

static void ChangeInGameVolume(bool increase)
{
    if (increase) {
        g_inGameMusicVolume += 0.05f;
        if (g_inGameMusicVolume > 1.0f) g_inGameMusicVolume = 1.0f;
    } else {
        g_inGameMusicVolume -= 0.05f;
        if (g_inGameMusicVolume < 0.0f) g_inGameMusicVolume = 0.0f;
    }

    SetInGameMusicVolume(g_inGameMusicVolume);
    SaveConfigVolume();

    int volPercent = (int)std::round(g_inGameMusicVolume * 100.0f);
    LogMsg("In-game music volume changed to: %d%%", volPercent);
    TriggerInGameMusicPopup("VOLUME: " + std::to_string(volPercent) + "%", true);
}

// Low-Level Mouse Hook for Alt/Modifier + Mouse Wheel Volume Control
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == GetCurrentProcessId()) {
            bool isVolModDown = (g_cfgVolumeMod != 0) && ((GetAsyncKeyState(g_cfgVolumeMod) & 0x8000) != 0);
            if (isVolModDown) {
                MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
                short delta = (short)HIWORD(pMouse->mouseData);
                if (delta != 0) {
                    ChangeInGameVolume(delta > 0);
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

static DWORD WINAPI MouseHookThreadProc(LPVOID lpParam)
{
    g_mouseHookThreadId = GetCurrentThreadId();
    HINSTANCE hInst = g_hInstance ? g_hInstance : GetModuleHandleA(NULL);
    g_hMouseHook = SetWindowsHookExA(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0);
    if (!g_hMouseHook) {
        LogMsg("Warning: Failed to install low-level mouse hook! Error: %lu", GetLastError());
    } else {
        LogMsg("Low-level mouse hook installed successfully.");
    }

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_hMouseHook) {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = NULL;
    }
    return 0;
}

// Mod Lifecycle & Radio Interference Prevention
static bool g_bModActive = false;
static bool g_bModInitialized = false;

// Export and signal to declare MusicPlayer as launched
extern "C" __declspec(dllexport) BOOL IsMusicPlayerLoaded(void)
{
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL IsInGameMusicModLoaded(void)
{
    return TRUE;
}

static HANDLE g_hMusicPlayerActiveEvent = NULL;
static HANDLE g_hInGameMusicModActiveEvent = NULL;

static void SignalMusicPlayerLaunched()
{
    SetEnvironmentVariableA("NFSW_MUSICPLAYER_LOADED", "1");
    SetEnvironmentVariableA("NFSW_INGAMEMUSICMOD_LOADED", "1");

    char evtName[128];
    snprintf(evtName, sizeof(evtName), "Local\\NFSW_MusicPlayer_Active_%lu", GetCurrentProcessId());
    if (!g_hMusicPlayerActiveEvent) {
        g_hMusicPlayerActiveEvent = CreateEventA(NULL, TRUE, TRUE, evtName);
    }

    snprintf(evtName, sizeof(evtName), "Local\\NFSW_InGameMusicMod_Active_%lu", GetCurrentProcessId());
    if (!g_hInGameMusicModActiveEvent) {
        g_hInGameMusicModActiveEvent = CreateEventA(NULL, TRUE, TRUE, evtName);
    }
}

static void ClearMusicPlayerLaunched()
{
    SetEnvironmentVariableA("NFSW_MUSICPLAYER_LOADED", "0");
    SetEnvironmentVariableA("NFSW_INGAMEMUSICMOD_LOADED", "0");

    if (g_hMusicPlayerActiveEvent) {
        CloseHandle(g_hMusicPlayerActiveEvent);
        g_hMusicPlayerActiveEvent = NULL;
    }
    if (g_hInGameMusicModActiveEvent) {
        CloseHandle(g_hInGameMusicModActiveEvent);
        g_hInGameMusicModActiveEvent = NULL;
    }
}

static bool IsRadioLaunched()
{
    // 1. Check environment variables set by Radio / RadioMod
    char envBuf[16] = {0};
    if (GetEnvironmentVariableA("NFSW_RADIO_LOADED", envBuf, sizeof(envBuf)) > 0) {
        if (strcmp(envBuf, "1") == 0) return true;
    }
    if (GetEnvironmentVariableA("NFSW_RADIOMOD_LOADED", envBuf, sizeof(envBuf)) > 0) {
        if (strcmp(envBuf, "1") == 0) return true;
    }

    // 2. Check process-specific named synchronization events created by Radio / RadioMod
    char evtName[128];
    snprintf(evtName, sizeof(evtName), "Local\\NFSW_Radio_Active_%lu", GetCurrentProcessId());
    HANDLE hEvent = OpenEventA(SYNCHRONIZE, FALSE, evtName);
    if (hEvent) {
        CloseHandle(hEvent);
        return true;
    }

    snprintf(evtName, sizeof(evtName), "NFSW_Radio_Active_%lu", GetCurrentProcessId());
    hEvent = OpenEventA(SYNCHRONIZE, FALSE, evtName);
    if (hEvent) {
        CloseHandle(hEvent);
        return true;
    }

    snprintf(evtName, sizeof(evtName), "Local\\NFSW_RadioMod_Active_%lu", GetCurrentProcessId());
    hEvent = OpenEventA(SYNCHRONIZE, FALSE, evtName);
    if (hEvent) {
        CloseHandle(hEvent);
        return true;
    }

    snprintf(evtName, sizeof(evtName), "NFSW_RadioMod_Active_%lu", GetCurrentProcessId());
    hEvent = OpenEventA(SYNCHRONIZE, FALSE, evtName);
    if (hEvent) {
        CloseHandle(hEvent);
        return true;
    }

    // 3. Check loaded module handles for Radio / RadioMod
    static const char* radioModuleNames[] = {
        "Radio.asi", "Radio.dll", "Radio",
        "radio.asi", "radio.dll",
        "RadioMod.asi", "RadioMod.dll", "RadioMod",
        "radiomod.asi", "radiomod.dll"
    };
    for (size_t i = 0; i < sizeof(radioModuleNames) / sizeof(radioModuleNames[0]); ++i) {
        if (GetModuleHandleA(radioModuleNames[i]) != NULL) {
            return true;
        }
    }

    // 4. Enumerate loaded modules via Toolhelp snapshot to detect Radio / RadioMod or exported functions
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me;
        me.dwSize = sizeof(MODULEENTRY32);
        if (Module32First(hSnap, &me)) {
            do {
                std::string modName = me.szModule;
                std::transform(modName.begin(), modName.end(), modName.begin(), ::tolower);
                if (modName.find("radio") != std::string::npos) {
                    CloseHandle(hSnap);
                    return true;
                }
                if (GetProcAddress(me.hModule, "IsRadioLoaded") != NULL ||
                    GetProcAddress(me.hModule, "IsRadioModLoaded") != NULL ||
                    GetProcAddress(me.hModule, "IsRadio") != NULL ||
                    GetProcAddress(me.hModule, "IsRadioMod") != NULL) {
                    CloseHandle(hSnap);
                    return true;
                }
            } while (Module32Next(hSnap, &me));
        }
        CloseHandle(hSnap);
    }

    // 5. Check if Radio's popup window exists
    if (FindWindowA("NFSW_Radio_Popup", NULL) != NULL) {
        return true;
    }

    return false;
}

static bool IsRadioModLaunched()
{
    return IsRadioLaunched();
}

static void ShutdownInGameMusicMod()
{
    if (!g_bModInitialized) return;
    g_bModActive = false;

    LogMsg("Stopping MusicPlayer to avoid interference with Radio...");
    ClearMusicPlayerLaunched();
    SaveConfigVolume();

    // 1. Unhook MusicLogic::Update if we hooked it
    if (g_origMusicLogic_Update) {
        uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
        uintptr_t delta = base - 0x00400000;
        void** pVtableEntry = (void**)(0x00bd8a30 + delta);
        DWORD oldProt = 0;
        if (VirtualProtect(pVtableEntry, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
            if (*pVtableEntry == (void*)&Hooked_MusicLogic_Update) {
                *pVtableEntry = (void*)g_origMusicLogic_Update;
                LogMsg("Restored original MusicLogic::Update vtable pointer.");
            }
            VirtualProtect(pVtableEntry, sizeof(void*), oldProt, &oldProt);
            g_origMusicLogic_Update = NULL;
        }
    }

    // 2. Unhook Mouse Hook
    if (g_hMouseHook) {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = NULL;
    }
    if (g_mouseHookThreadId) {
        PostThreadMessageA(g_mouseHookThreadId, WM_QUIT, 0, 0);
        g_mouseHookThreadId = 0;
    }

    // 3. Destroy popup window and unregister class
    if (g_hInGamePopupWnd) {
        DestroyWindow(g_hInGamePopupWnd);
        g_hInGamePopupWnd = NULL;
    }
    UnregisterClassA("NFSW_InGame_Music_Popup", g_hInstance);

    // 4. Shutdown GDI+
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }

    // 5. Delete critical section
    DeleteCriticalSection(&g_inGamePopupCs);

    g_bModInitialized = false;
    LogMsg("MusicPlayer has been completely stopped and cleaned up.");
}

// Popup Display Management Thread
static DWORD WINAPI PopupThreadProc(LPVOID lpParam)
{
    WNDCLASSEXA wcInGame = {0};
    wcInGame.cbSize = sizeof(WNDCLASSEXA);
    wcInGame.style = CS_HREDRAW | CS_VREDRAW;
    wcInGame.lpfnWndProc = DefWindowProcA;
    wcInGame.hInstance = g_hInstance;
    wcInGame.lpszClassName = "NFSW_InGame_Music_Popup";
    RegisterClassExA(&wcInGame);

    g_hInGamePopupWnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        wcInGame.lpszClassName,
        "NFSW InGame Music Popup",
        WS_POPUP,
        0, 0, INGAME_POPUP_W, INGAME_POPUP_H,
        NULL, NULL, g_hInstance, NULL
    );

    if (g_hInGamePopupWnd) {
        ShowWindow(g_hInGamePopupWnd, SW_HIDE);
    }

    MSG msg;
    while (g_bModActive) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                if (g_hInGamePopupWnd) {
                    DestroyWindow(g_hInGamePopupWnd);
                    g_hInGamePopupWnd = NULL;
                }
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        EnterCriticalSection(&g_inGamePopupCs);
        PopupState igSt = g_inGamePopupState;
        DWORD igHoldStart = g_inGamePopupHoldStart;
        DWORD igFadeStart = g_inGamePopupFadeStart;
        LeaveCriticalSection(&g_inGamePopupCs);

        DWORD now = GetTickCount();

        if (igSt == POPUP_HOLD) {
            RenderInGamePopup(245);
            if (now - igHoldStart >= 3000) {
                EnterCriticalSection(&g_inGamePopupCs);
                g_inGamePopupState = POPUP_FADE;
                g_inGamePopupFadeStart = now;
                LeaveCriticalSection(&g_inGamePopupCs);
            }
        } else if (igSt == POPUP_FADE) {
            DWORD fadeElapsed = now - igFadeStart;
            if (fadeElapsed < 250) {
                float t = (float)fadeElapsed / 250.0f;
                int alpha = (int)(245.0f * (1.0f - t));
                if (alpha < 0) alpha = 0;
                RenderInGamePopup(alpha);
            } else {
                ShowWindow(g_hInGamePopupWnd, SW_HIDE);
                EnterCriticalSection(&g_inGamePopupCs);
                g_inGamePopupState = POPUP_IDLE;
                LeaveCriticalSection(&g_inGamePopupCs);
            }
        }

        Sleep(15);
    }

    if (g_hInGamePopupWnd) {
        DestroyWindow(g_hInGamePopupWnd);
        g_hInGamePopupWnd = NULL;
    }

    return 0;
}

// Main Mod Thread
static DWORD WINAPI InGameMusicMainThread(LPVOID lpParam)
{
    srand((unsigned int)time(NULL));
    LogMsg("=== NFSW Music Player Mod Launching ===");

    // Check whether Radio is launched too.
    LogMsg("Checking if Radio is launched...");
    for (int i = 0; i < 20; ++i) {
        if (IsRadioLaunched()) {
            LogMsg("Radio detected as launched! MusicPlayer stops working to avoid interference.");
            return 0;
        }
        Sleep(50);
    }

    LogMsg("Radio not detected. Initializing MusicPlayer...");
    g_bModActive = true;
    InitializeCriticalSection(&g_inGamePopupCs);
    g_bModInitialized = true;
    SignalMusicPlayerLaunched();
    InstallMusicLogicHook();

    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiInput, NULL);

    LoadConfig();

    CreateThread(NULL, 0, PopupThreadProc, NULL, 0, NULL);
    g_hMouseThread = CreateThread(NULL, 0, MouseHookThreadProc, NULL, 0, &g_mouseHookThreadId);

    bool wasBaseKeyDown = false;
    bool wasOrderKeyDown = false;
    bool wasNextStationKeyDown = false;
    bool wasToggleKeyDown = false;

    while (g_bModActive) {
        Sleep(16);

        // Continuous runtime check: if Radio is launched during gameplay, stop working immediately
        if (IsRadioLaunched()) {
            LogMsg("Radio launch detected at runtime! MusicPlayer stops working to avoid interference.");
            ShutdownInGameMusicMod();
            return 0;
        }

        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        bool isGameActive = (fgPid == GetCurrentProcessId());

        if (isGameActive) {
            bool isBaseKeyDown = (g_cfgRadioKey != 0) && ((GetAsyncKeyState(g_cfgRadioKey) & 0x8000) != 0);
            bool isOrderKeyDown = (g_cfgPlaylistOrderMod != 0) && ((GetAsyncKeyState(g_cfgPlaylistOrderMod) & 0x8000) != 0);
            bool isNextStationKeyDown = (g_cfgNextStationMod != 0) && ((GetAsyncKeyState(g_cfgNextStationMod) & 0x8000) != 0);
            bool isToggleKeyDown = (g_cfgTogglePlaybackMod != 0) && ((GetAsyncKeyState(g_cfgTogglePlaybackMod) & 0x8000) != 0);

            bool baseJustPressed = isBaseKeyDown && !wasBaseKeyDown;
            bool orderJustPressed = isOrderKeyDown && !wasOrderKeyDown;
            bool nextStationJustPressed = isNextStationKeyDown && !wasNextStationKeyDown;
            bool toggleJustPressed = isToggleKeyDown && !wasToggleKeyDown;

            if (baseJustPressed || orderJustPressed || nextStationJustPressed || toggleJustPressed) {
                if (isBaseKeyDown && isOrderKeyDown && (baseJustPressed || orderJustPressed)) {
                    // Toggle playlist order (Default: F + Tab)
                    g_inGamePlayOrder = (PlayOrder)((g_inGamePlayOrder + 1) % ORDER_COUNT);
                    if (g_inGamePlayOrder == ORDER_SHUFFLE) {
                        EnsureInGamePlaylists();
                        SyncShuffleDeckToCurrentTrack(g_currentInGamePlaylist, g_currentInGameTrackInPlaylist);
                    }
                    LogMsg("In-game playlist order changed to: %s", GetPlayOrderName(g_inGamePlayOrder));
                    TriggerInGameMusicPopup(std::string("ORDER: ") + GetPlayOrderName(g_inGamePlayOrder), true);
                } else if (isBaseKeyDown && isToggleKeyDown && (baseJustPressed || toggleJustPressed)) {
                    // Toggle playback (Default: Q + Tab)
                    ToggleInGamePlayback();
                } else if (isBaseKeyDown && isNextStationKeyDown && (baseJustPressed || nextStationJustPressed)) {
                    // Switch playlist (Default: CapsLock + Tab)
                    SwitchInGamePlaylist();
                } else if (baseJustPressed) {
                    // Next track (Default: Tab)
                    SwitchInGameTrack();
                }
            }

            wasBaseKeyDown = isBaseKeyDown;
            wasOrderKeyDown = isOrderKeyDown;
            wasNextStationKeyDown = isNextStationKeyDown;
            wasToggleKeyDown = isToggleKeyDown;
        } else {
            wasBaseKeyDown = false;
            wasOrderKeyDown = false;
            wasNextStationKeyDown = false;
            wasToggleKeyDown = false;
        }
    }

    return 0;
}

// DLL Entry Point
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(NULL, 0, InGameMusicMainThread, NULL, 0, NULL);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        ClearMusicPlayerLaunched();
        if (g_bModInitialized) {
            ShutdownInGameMusicMod();
        }
    }
    return TRUE;
}