#pragma once

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

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")

// FMOD Types and Constants
typedef int FMOD_RESULT;
#define FMOD_OK 0
#define FMOD_DEFAULT 0x00000000
#define FMOD_LOOP_OFF 0x00000001
#define FMOD_2D 0x00000008
#define FMOD_CREATESTREAM 0x00000080
#define FMOD_UNICODE      0x00000200

// Function pointer signatures for FMOD C API
typedef FMOD_RESULT (__stdcall *t_FMOD_System_Create)(void** system);
typedef FMOD_RESULT (__stdcall *t_FMOD_System_Init)(void* system, int maxchannels, unsigned int flags, void* extradriverdata);
typedef FMOD_RESULT (__stdcall *t_FMOD_System_Close)(void* system);
typedef FMOD_RESULT (__stdcall *t_FMOD_System_Release)(void* system);
typedef FMOD_RESULT (__stdcall *t_FMOD_System_CreateStream)(void* system, const char* name_or_data, unsigned int mode, void* exinfo, void** sound);
typedef FMOD_RESULT (__stdcall *t_FMOD_System_PlaySound)(void* system, void* sound, void* channelgroup, int paused, void** channel);
typedef FMOD_RESULT (__stdcall *t_FMOD_System_Update)(void* system);
typedef FMOD_RESULT (__stdcall *t_FMOD_Channel_Stop)(void* channel);
typedef FMOD_RESULT (__stdcall *t_FMOD_Channel_SetPaused)(void* channel, int paused);
typedef FMOD_RESULT (__stdcall *t_FMOD_Channel_IsPlaying)(void* channel, int* isplaying);
typedef FMOD_RESULT (__stdcall *t_FMOD_Channel_SetVolume)(void* channel, float volume);
typedef FMOD_RESULT (__stdcall *t_FMOD_Sound_Release)(void* sound);

extern t_FMOD_System_Create       pfn_FMOD_System_Create;
extern t_FMOD_System_Init         pfn_FMOD_System_Init;
extern t_FMOD_System_Close        pfn_FMOD_System_Close;
extern t_FMOD_System_Release      pfn_FMOD_System_Release;
extern t_FMOD_System_CreateStream pfn_FMOD_System_CreateStream;
extern t_FMOD_System_PlaySound    pfn_FMOD_System_PlaySound;
extern t_FMOD_System_Update       pfn_FMOD_System_Update;
extern t_FMOD_Channel_Stop        pfn_FMOD_Channel_Stop;
extern t_FMOD_Channel_SetPaused   pfn_FMOD_Channel_SetPaused;
extern t_FMOD_Channel_IsPlaying   pfn_FMOD_Channel_IsPlaying;
extern t_FMOD_Channel_SetVolume   pfn_FMOD_Channel_SetVolume;
extern t_FMOD_Sound_Release       pfn_FMOD_Sound_Release;

extern void* g_fmodSystem;
extern void* g_currentSound;
extern void* g_currentChannel;
extern HMODULE g_hFmod;

extern HINSTANCE g_hInstance;
extern ULONG_PTR g_gdiplusToken;
extern HANDLE g_hRadioActiveEvent;
extern HANDLE g_hRadioModActiveEvent;

struct Station {
    std::string name;
    std::string folderPath;
    std::vector<std::string> tracks;
};

// Playlist Order Modes
enum PlayOrder {
    ORDER_DEFAULT = 0,
    ORDER_REPEAT_PLAYLIST,
    ORDER_REPEAT_TRACK,
    ORDER_SHUFFLE,
    ORDER_COUNT
};

// Player States
enum PlayerState {
    STATE_PLAYING = 0,
    STATE_PAUSED  = 1,
    STATE_STATION = 2,
    STATE_VOLUME  = 3,
    STATE_ORDER   = 4,
    STATE_COUNT   = 5
};

extern PlayOrder g_playOrder;
extern std::vector<Station> g_stations;
extern int g_currentStation;
extern int g_currentTrack;
extern bool g_isPlaying;
extern bool g_isStopped;
extern bool g_autoPaused;
extern float g_volume;

extern bool g_receivedUserInput;

extern std::string g_currentTrackTitle;
extern std::string g_currentArtist;

// Mouse Hook Globals
extern HHOOK g_hMouseHook;
extern DWORD g_mouseHookThreadId;
extern HANDLE g_hMouseThread;

// Configurable Keybindings
extern int g_cfgRadioKey;
extern int g_cfgNextStationMod;
extern int g_cfgTogglePlaybackMod;
extern int g_cfgPlaylistOrderMod;
extern int g_cfgSkinMod;
extern int g_cfgVolumeMod;

// Persistent Screen Position
extern int g_popupX;
extern int g_popupY;
extern bool g_cfgShowInGameMusicPopUp;
extern std::string g_cfgMusicPath;

// Skin Structure & Layout Coordinates
struct RectCoords {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int fontSize = 0;
    COLORREF color = RGB(255, 255, 255);
    bool hasColor = false;
    bool enabled = true;

    // Custom text formatting & bar configuration
    std::string format = "";
    bool hasFormat = false;
    std::string prefix = "";
    bool hasPrefix = false;
    std::string type = "text";     // "text" or "bar"

    // Background configuration
    bool drawBackground = false;
    COLORREF bgColor = RGB(35, 35, 40);
    bool hasBgColor = false;

    std::string align = "near";    // "near" (left), "center", "far" (right)
    bool showText = false;
};

struct CoverCoords : public RectCoords {
    std::string zOrder = "below";
    bool keepAspect = true;
};

struct StateDisplayConfig {
    bool configured = false;
    std::string imageName = "";
    Gdiplus::Bitmap* pStateBitmap = NULL;

    bool showStation = false;
    bool showTrack   = false;
    bool showArtist  = false;
    bool showStatus  = false;
    bool showOrder   = false;
    bool showVolume  = false;
    bool showCover   = false;

    bool hasStationRect = false; RectCoords stationRect;
    bool hasTrackRect   = false; RectCoords trackRect;
    bool hasArtistRect  = false; RectCoords artistRect;
    bool hasStatusRect  = false; RectCoords statusRect;
    bool hasOrderRect   = false; RectCoords orderRect;
    bool hasVolumeRect  = false; RectCoords volumeRect;
    bool hasCoverRect   = false; CoverCoords coverRect;
};

struct SkinConfig {
    std::string name = "Default";
    std::string folderPath;
    std::string imageName;
    std::string fontName = "Segoe UI";
    int fontSize = 14;
    float volumeStep = 0.05f;
    int width = 400;
    int height = 150;

    RectCoords stationRect;
    RectCoords trackRect;
    RectCoords artistRect;
    RectCoords statusRect;
    RectCoords orderRect;
    RectCoords volumeRect;
    CoverCoords coverRect;

    StateDisplayConfig states[STATE_COUNT];

    Gdiplus::Bitmap* orderIcons[ORDER_COUNT] = { NULL };

    std::string loadedFontPath;
    bool fontLoaded = false;
};

extern SkinConfig g_skin;
extern Gdiplus::Bitmap* g_pSkinBitmap;
extern Gdiplus::Bitmap* g_pCurrentCover;

// Popup Window States & Lifecycle
enum PopupState {
    POPUP_IDLE,
    POPUP_HOLD,
    POPUP_FADE
};

extern HWND g_hPopupWnd;
extern CRITICAL_SECTION g_popupCs;
extern PlayerState g_popupPlayerState;
extern PlayOrder g_popupPlayOrder;
extern std::string g_popupStationName;
extern std::string g_popupTrackTitle;
extern std::string g_popupArtistName;
extern std::string g_popupStatus;
extern std::string g_popupOrder;
extern std::string g_popupVolume;

extern PopupState g_popupState;
extern DWORD g_popupHoldStart;
extern DWORD g_popupFadeStart;

// In-Game Music Popup Globals
extern HWND g_hInGamePopupWnd;
extern CRITICAL_SECTION g_inGamePopupCs;
extern PopupState g_inGamePopupState;
extern DWORD g_inGamePopupHoldStart;
extern DWORD g_inGamePopupFadeStart;
extern std::string g_inGamePopupTrackName;

// Reusable Double-Buffered Surface
struct DIBBuffer {
    HDC memDC = NULL;
    HBITMAP memBmp = NULL;
    HGDIOBJ oldBmp = NULL;
    void* pBits = NULL;
    int width = 0;
    int height = 0;

    void Ensure(HDC hdcScreen, int w, int h) {
        if (memDC && memBmp && width == w && height == h) {
            if (pBits) memset(pBits, 0, w * h * 4);
            return;
        }
        Cleanup();
        width = w;
        height = h;
        memDC = CreateCompatibleDC(hdcScreen);
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        memBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
        oldBmp = SelectObject(memDC, memBmp);
        if (pBits) memset(pBits, 0, w * h * 4);
    }

    void Cleanup() {
        if (memDC) {
            if (oldBmp) SelectObject(memDC, oldBmp);
            DeleteDC(memDC);
            memDC = NULL;
            oldBmp = NULL;
        }
        if (memBmp) {
            DeleteObject(memBmp);
            memBmp = NULL;
        }
        pBits = NULL;
        width = 0;
        height = 0;
    }
};

extern DIBBuffer g_popupDIB;
extern DIBBuffer g_inGamePopupDIB;

// Common Utility Functions
const char* GetPlayOrderName(PlayOrder order);
std::string GetDirectoryOfFile(const std::string& path);
std::string GetFilenameWithoutExtension(const std::string& path);
bool HasAudioExtension(const std::string& filename);
std::string TrimString(const std::string& str);
std::wstring StringToWide(const std::string& str);
std::string WideToUtf8(const std::wstring& wstr);
#if defined(_DEBUG) || defined(RADIOMOD_ENABLE_LOGGING)
    void LogMsg(const char* fmt, ...);
#else
    #define LogMsg(...) ((void)0)
#endif

bool ContainsCjk(const std::wstring& text);
bool IsKnownCjkFont(const std::wstring& fontName);
bool DoesFontSupportWideString(const std::wstring& fontName, const std::wstring& text);
std::wstring GetBestFontForText(const std::wstring& primaryFont, const std::wstring& text);
Gdiplus::Font* CreateSafeGdiplusFont(const Gdiplus::FontFamily* pFam, float size, bool bold);

std::string NormalizePath(std::string p);
Gdiplus::Bitmap* LoadBitmapFromMemory(const void* data, size_t size);
Gdiplus::Bitmap* LoadBitmapFromFile(const std::string& path);

HWND FindGameWindow();
void EnsurePopupParented(HWND hWnd);
