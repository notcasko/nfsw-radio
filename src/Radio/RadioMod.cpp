#include "RadioMod.h"
#include "RadioCommon.h"
#include "MetadataExtractor.h"
#include "RadioConfig.h"
#include "RadioSkin.h"
#include "RadioPopup.h"
#include "InGameMusic.h"
#include <tlhelp32.h>

void PlayTrack(int stationIdx, int trackIdx, PlayerState triggerReason, bool showPopup)
{
    if (g_stations.empty()) return;
    if (stationIdx < 0 || stationIdx >= (int)g_stations.size()) stationIdx = 0;
    Station& st = g_stations[stationIdx];
    if (st.tracks.empty()) return;
    if (trackIdx < 0 || trackIdx >= (int)st.tracks.size()) trackIdx = 0;

    g_currentStation = stationIdx;
    g_currentTrack = trackIdx;

    if (g_currentChannel && pfn_FMOD_Channel_Stop) {
        pfn_FMOD_Channel_Stop(g_currentChannel);
        g_currentChannel = NULL;
    }
    if (g_currentSound && pfn_FMOD_Sound_Release) {
        pfn_FMOD_Sound_Release(g_currentSound);
        g_currentSound = NULL;
    }

    std::string trackPath = st.tracks[trackIdx];
    LogMsg("Playing station '%s' [%d/%d], track '%s' [%d/%d]",
           st.name.c_str(), stationIdx + 1, (int)g_stations.size(),
           trackPath.c_str(), trackIdx + 1, (int)st.tracks.size());

    UpdateCurrentCover(trackPath, st.folderPath);
    ExtractTrackMetadata(trackPath, g_currentArtist, g_currentTrackTitle);

    std::wstring wTrackPath = StringToWide(trackPath);
    FMOD_RESULT res = pfn_FMOD_System_CreateStream(
        g_fmodSystem,
        (const char*)wTrackPath.c_str(),
        FMOD_DEFAULT | FMOD_CREATESTREAM | FMOD_2D | FMOD_UNICODE,
        NULL,
        &g_currentSound
    );

    if (res != FMOD_OK || !g_currentSound) {
        wchar_t shortBuf[MAX_PATH] = {0};
        DWORD sLen = GetShortPathNameW(wTrackPath.c_str(), shortBuf, MAX_PATH);
        if (sLen > 0 && sLen < MAX_PATH) {
            char ansiShort[MAX_PATH] = {0};
            WideCharToMultiByte(CP_ACP, 0, shortBuf, -1, ansiShort, MAX_PATH, NULL, NULL);
            res = pfn_FMOD_System_CreateStream(
                g_fmodSystem,
                ansiShort,
                FMOD_DEFAULT | FMOD_CREATESTREAM | FMOD_2D,
                NULL,
                &g_currentSound
            );
        }
    }

    if (res != FMOD_OK || !g_currentSound) {
        res = pfn_FMOD_System_CreateStream(
            g_fmodSystem,
            trackPath.c_str(),
            FMOD_DEFAULT | FMOD_CREATESTREAM | FMOD_2D,
            NULL,
            &g_currentSound
        );
    }

    if (res != FMOD_OK || !g_currentSound) {
        LogMsg("Failed to create stream for: %s (error code %d)", trackPath.c_str(), res);
        return;
    }

    res = pfn_FMOD_System_PlaySound(
        g_fmodSystem,
        g_currentSound,
        NULL,
        0,
        &g_currentChannel
    );

    if (res == FMOD_OK && g_currentChannel) {
        pfn_FMOD_Channel_SetVolume(g_currentChannel, g_volume);
        g_isPlaying = true;
        g_isStopped = false;

        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid != GetCurrentProcessId()) {
            if (pfn_FMOD_Channel_SetPaused) {
                pfn_FMOD_Channel_SetPaused(g_currentChannel, 1);
            }
            g_autoPaused = true;
        } else {
            g_autoPaused = false;
            StopGameMusic();
            SilenceGameMusic();
        }

        if (showPopup && g_receivedUserInput) {
            std::string statusLabel = (triggerReason == STATE_STATION) ? "STATION" : "PLAYING";
            TriggerPopup(triggerReason, st.name, g_currentTrackTitle, g_currentArtist, statusLabel);
        }
    } else {
        LogMsg("PlaySound failed (error code %d)", res);
    }
}

static void ScanDirectoryForAudioTracks(const std::wstring& folderW, const std::string& folderUtf8, std::vector<std::string>& outTracks, int depth = 0)
{
    if (depth > 5) return;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((folderW + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring nameW = fd.cFileName;
        if (nameW == L"." || nameW == L"..") continue;

        std::string nameUtf8 = WideToUtf8(nameW);
        std::wstring fullPathW = folderW + L"\\" + nameW;
        std::string fullPathUtf8 = folderUtf8 + "\\" + nameUtf8;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanDirectoryForAudioTracks(fullPathW, fullPathUtf8, outTracks, depth + 1);
        } else {
            if (HasAudioExtension(nameUtf8)) {
                outTracks.push_back(fullPathUtf8);
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void ScanRadioFolders()
{
    g_stations.clear();

    if (!g_cfgMusicPath.empty()) {
        std::wstring wMusicPath = StringToWide(g_cfgMusicPath);
        DWORD customAttr = GetFileAttributesW(wMusicPath.c_str());
        if (customAttr != INVALID_FILE_ATTRIBUTES && (customAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            LogMsg("Using custom Radio directory from INI: %s", g_cfgMusicPath.c_str());

            Station rootStation;
            rootStation.name = GetFilenameWithoutExtension(g_cfgMusicPath);
            if (rootStation.name.empty()) rootStation.name = "Custom Radio";
            rootStation.folderPath = g_cfgMusicPath;

            WIN32_FIND_DATAW fdRoot;
            HANDLE hFindRoot = FindFirstFileW((wMusicPath + L"\\*").c_str(), &fdRoot);
            if (hFindRoot != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fdRoot.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        std::string fname = WideToUtf8(fdRoot.cFileName);
                        if (HasAudioExtension(fname)) {
                            rootStation.tracks.push_back(rootStation.folderPath + "\\" + fname);
                        }
                    }
                } while (FindNextFileW(hFindRoot, &fdRoot));
                FindClose(hFindRoot);
            }

            if (!rootStation.tracks.empty()) {
                LogMsg("Discovered custom root station: '%s' (%d tracks)", rootStation.name.c_str(), (int)rootStation.tracks.size());
                g_stations.push_back(rootStation);
            }

            WIN32_FIND_DATAW fdSub;
            HANDLE hFindSub = FindFirstFileW((wMusicPath + L"\\*").c_str(), &fdSub);
            if (hFindSub != INVALID_HANDLE_VALUE) {
                do {
                    if (fdSub.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        std::wstring subNameW = fdSub.cFileName;
                        if (subNameW != L"." && subNameW != L"..") {
                            std::string subName = WideToUtf8(fdSub.cFileName);
                            Station st;
                            st.name = subName;
                            st.folderPath = g_cfgMusicPath + "\\" + subName;
                            std::wstring stFolderW = wMusicPath + L"\\" + subNameW;

                            ScanDirectoryForAudioTracks(stFolderW, st.folderPath, st.tracks, 0);

                            if (!st.tracks.empty()) {
                                LogMsg("Discovered custom station: '%s' (%d tracks)", st.name.c_str(), (int)st.tracks.size());
                                g_stations.push_back(st);
                            }
                        }
                    }
                } while (FindNextFileW(hFindSub, &fdSub));
                FindClose(hFindSub);
            }

            if (!g_stations.empty()) {
                LogMsg("Total active custom stations found: %d", (int)g_stations.size());
                return;
            }
        } else {
            LogMsg("Warning: Configured MusicPath '%s' does not exist or is not a directory. Falling back to default paths.", g_cfgMusicPath.c_str());
        }
    }

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = GetDirectoryOfFile(exePath);

    char modPath[MAX_PATH];
    GetModuleFileNameA(g_hInstance, modPath, MAX_PATH);
    std::string modDir = GetDirectoryOfFile(modPath);

    std::vector<std::string> candidateRoots = {
        exeDir + "\\Sound\\Radio",
        modDir + "\\..\\Sound\\Radio"
    };

    std::string radioPath = "";
    for (const auto& cand : candidateRoots) {
        DWORD attrs = GetFileAttributesW(StringToWide(cand).c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            radioPath = cand;
            break;
        }
    }

    if (radioPath.empty()) {
        std::string soundDir = exeDir + "\\Sound";
        std::string radioDir = soundDir + "\\Radio";

        CreateDirectoryA(soundDir.c_str(), NULL);
        if (CreateDirectoryA(radioDir.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
            LogMsg("Created default Sound\\Radio folder at: %s", radioDir.c_str());
            radioPath = radioDir;
        } else {
            LogMsg("CRITICAL: Sound\\Radio folder not found and failed to create at: %s (Error: %lu)",
                radioDir.c_str(), GetLastError());
            return;
        }
    }

    LogMsg("Using Radio directory: %s", radioPath.c_str());

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((StringToWide(radioPath) + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LogMsg("FindFirstFile failed on: %s", radioPath.c_str());
        return;
    }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring subNameW = fd.cFileName;
            if (subNameW != L"." && subNameW != L"..") {
                std::string subName = WideToUtf8(fd.cFileName);
                Station st;
                st.name = subName;
                st.folderPath = radioPath + "\\" + subName;
                std::wstring stFolderW = StringToWide(st.folderPath);

                ScanDirectoryForAudioTracks(stFolderW, st.folderPath, st.tracks, 0);

                if (!st.tracks.empty()) {
                    LogMsg("Discovered station: '%s' (%d tracks)", st.name.c_str(), (int)st.tracks.size());
                    g_stations.push_back(st);
                }
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    LogMsg("Total active stations found: %d", (int)g_stations.size());
}

static DWORD g_lastRadioRescanTick = 0;
static const DWORD RADIO_RESCAN_INTERVAL_MS = 2500;

// Rebuilds g_stations without disturbing the currently-playing FMOD stream.
// Current station/track is restored by folder path + track path (indices can shift).
static void RescanRadioPreservingPlayback()
{
    std::string oldStationFolder;
    std::string oldTrackPath;
    if (!g_stations.empty() && g_currentStation >= 0 && g_currentStation < (int)g_stations.size()) {
        oldStationFolder = g_stations[g_currentStation].folderPath;
        const Station& st = g_stations[g_currentStation];
        if (g_currentTrack >= 0 && g_currentTrack < (int)st.tracks.size()) {
            oldTrackPath = st.tracks[g_currentTrack];
        }
    }

    ScanRadioFolders(); // clears + rebuilds g_stations

    if (!oldStationFolder.empty() && !g_stations.empty()) {
        bool found = false;
        for (size_t i = 0; i < g_stations.size(); ++i) {
            if (g_stations[i].folderPath == oldStationFolder) {
                g_currentStation = (int)i;
                found = true;
                if (!oldTrackPath.empty()) {
                    for (size_t j = 0; j < g_stations[i].tracks.size(); ++j) {
                        if (g_stations[i].tracks[j] == oldTrackPath) {
                            g_currentTrack = (int)j;
                            break;
                        }
                    }
                }
                break;
            }
        }
        if (!found && g_currentStation >= (int)g_stations.size()) {
            g_currentStation = 0;
            g_currentTrack = 0;
        }
    }
}

bool InitFMOD()
{
    char modPath[MAX_PATH];
    GetModuleFileNameA(g_hInstance, modPath, MAX_PATH);
    std::string modDir = GetDirectoryOfFile(modPath);

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = GetDirectoryOfFile(exePath);

    std::vector<std::string> fmodPaths = {
        modDir + "\\fmod.dll",
        exeDir + "\\scripts\\fmod.dll"
    };

    for (const auto& path : fmodPaths) {
        g_hFmod = LoadLibraryA(path.c_str());
        if (g_hFmod) {
            LogMsg("Loaded fmod.dll from: %s", path.c_str());
            break;
        }
    }

    if (!g_hFmod) {
        LogMsg("CRITICAL: Failed to load fmod.dll!");
        return false;
    }

    pfn_FMOD_System_Create       = (t_FMOD_System_Create)GetProcAddress(g_hFmod, "FMOD_System_Create");
    pfn_FMOD_System_Init         = (t_FMOD_System_Init)GetProcAddress(g_hFmod, "FMOD_System_Init");
    pfn_FMOD_System_Close        = (t_FMOD_System_Close)GetProcAddress(g_hFmod, "FMOD_System_Close");
    pfn_FMOD_System_Release      = (t_FMOD_System_Release)GetProcAddress(g_hFmod, "FMOD_System_Release");
    pfn_FMOD_System_CreateStream = (t_FMOD_System_CreateStream)GetProcAddress(g_hFmod, "FMOD_System_CreateStream");
    pfn_FMOD_System_PlaySound    = (t_FMOD_System_PlaySound)GetProcAddress(g_hFmod, "FMOD_System_PlaySound");
    pfn_FMOD_System_Update       = (t_FMOD_System_Update)GetProcAddress(g_hFmod, "FMOD_System_Update");
    pfn_FMOD_Channel_Stop        = (t_FMOD_Channel_Stop)GetProcAddress(g_hFmod, "FMOD_Channel_Stop");
    pfn_FMOD_Channel_SetPaused   = (t_FMOD_Channel_SetPaused)GetProcAddress(g_hFmod, "FMOD_Channel_SetPaused");
    pfn_FMOD_Channel_IsPlaying   = (t_FMOD_Channel_IsPlaying)GetProcAddress(g_hFmod, "FMOD_Channel_IsPlaying");
    pfn_FMOD_Channel_SetVolume   = (t_FMOD_Channel_SetVolume)GetProcAddress(g_hFmod, "FMOD_Channel_SetVolume");
    pfn_FMOD_Sound_Release       = (t_FMOD_Sound_Release)GetProcAddress(g_hFmod, "FMOD_Sound_Release");

    if (!pfn_FMOD_System_Create || !pfn_FMOD_System_Init || !pfn_FMOD_System_CreateStream) {
        LogMsg("CRITICAL: Missing exported symbols in fmod.dll!");
        return false;
    }

    FMOD_RESULT res = pfn_FMOD_System_Create(&g_fmodSystem);
    if (res != FMOD_OK || !g_fmodSystem) {
        LogMsg("FMOD_System_Create failed (%d)", res);
        return false;
    }

    res = pfn_FMOD_System_Init(g_fmodSystem, 32, FMOD_DEFAULT, NULL);
    if (res != FMOD_OK) {
        LogMsg("FMOD_System_Init failed (%d)", res);
        return false;
    }

    LogMsg("FMOD sound system initialized successfully.");
    return true;
}

// Export and signal to declare Radio/RadioMod as launched
extern "C" __declspec(dllexport) BOOL IsRadioLoaded(void)
{
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL IsRadioModLoaded(void)
{
    return TRUE;
}

void SignalRadioLaunched()
{
    SetEnvironmentVariableA("NFSW_RADIO_LOADED", "1");
    SetEnvironmentVariableA("NFSW_RADIOMOD_LOADED", "1");

    char evtName[128];
    snprintf(evtName, sizeof(evtName), "Local\\NFSW_Radio_Active_%lu", GetCurrentProcessId());
    if (!g_hRadioActiveEvent) {
        g_hRadioActiveEvent = CreateEventA(NULL, TRUE, TRUE, evtName);
    }

    snprintf(evtName, sizeof(evtName), "Local\\NFSW_RadioMod_Active_%lu", GetCurrentProcessId());
    if (!g_hRadioModActiveEvent) {
        g_hRadioModActiveEvent = CreateEventA(NULL, TRUE, TRUE, evtName);
    }
}

void SignalRadioModLaunched()
{
    SignalRadioLaunched();
}

bool IsMusicPlayerLaunched()
{
    // 1. Check environment variables set by MusicPlayer / InGameMusicMod
    char envBuf[16] = {0};
    if (GetEnvironmentVariableA("NFSW_MUSICPLAYER_LOADED", envBuf, sizeof(envBuf)) > 0) {
        if (strcmp(envBuf, "1") == 0) return true;
    }
    if (GetEnvironmentVariableA("NFSW_INGAMEMUSICMOD_LOADED", envBuf, sizeof(envBuf)) > 0) {
        if (strcmp(envBuf, "1") == 0) return true;
    }

    // 2. Check process-specific named synchronization events
    char evtName[128];
    snprintf(evtName, sizeof(evtName), "Local\\NFSW_MusicPlayer_Active_%lu", GetCurrentProcessId());
    HANDLE hEvent = OpenEventA(SYNCHRONIZE, FALSE, evtName);
    if (hEvent) {
        CloseHandle(hEvent);
        return true;
    }

    snprintf(evtName, sizeof(evtName), "NFSW_MusicPlayer_Active_%lu", GetCurrentProcessId());
    hEvent = OpenEventA(SYNCHRONIZE, FALSE, evtName);
    if (hEvent) {
        CloseHandle(hEvent);
        return true;
    }

    snprintf(evtName, sizeof(evtName), "Local\\NFSW_InGameMusicMod_Active_%lu", GetCurrentProcessId());
    hEvent = OpenEventA(SYNCHRONIZE, FALSE, evtName);
    if (hEvent) {
        CloseHandle(hEvent);
        return true;
    }

    snprintf(evtName, sizeof(evtName), "NFSW_InGameMusicMod_Active_%lu", GetCurrentProcessId());
    hEvent = OpenEventA(SYNCHRONIZE, FALSE, evtName);
    if (hEvent) {
        CloseHandle(hEvent);
        return true;
    }

    // 3. Check loaded module handles
    static const char* playerModuleNames[] = {
        "MusicPlayer.asi", "MusicPlayer.dll", "MusicPlayer",
        "musicplayer.asi", "musicplayer.dll",
        "InGameMusicMod.asi", "InGameMusicMod.dll", "InGameMusicMod",
        "ingamemusicmod.asi", "ingamemusicmod.dll"
    };
    for (size_t i = 0; i < sizeof(playerModuleNames) / sizeof(playerModuleNames[0]); ++i) {
        if (GetModuleHandleA(playerModuleNames[i]) != NULL) {
            return true;
        }
    }

    // 4. Enumerate loaded modules via Toolhelp snapshot
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 me;
        me.dwSize = sizeof(MODULEENTRY32);
        if (Module32First(hSnap, &me)) {
            do {
                std::string modName = me.szModule;
                std::transform(modName.begin(), modName.end(), modName.begin(), ::tolower);
                if (modName.find("musicplayer") != std::string::npos ||
                    modName.find("ingamemusicmod") != std::string::npos) {
                    CloseHandle(hSnap);
                    return true;
                }
                if (GetProcAddress(me.hModule, "IsMusicPlayerLoaded") != NULL ||
                    GetProcAddress(me.hModule, "IsInGameMusicModLoaded") != NULL ||
                    GetProcAddress(me.hModule, "IsMusicPlayer") != NULL ||
                    GetProcAddress(me.hModule, "IsInGameMusicMod") != NULL) {
                    CloseHandle(hSnap);
                    return true;
                }
            } while (Module32Next(hSnap, &me));
        }
        CloseHandle(hSnap);
    }

    // 5. Check if popup window exists
    if (FindWindowA("NFSW_InGame_Music_Popup", NULL) != NULL) {
        return true;
    }

    return false;
}

static DWORD WINAPI RadioMainThread(LPVOID lpParam)
{
    SignalRadioLaunched();
    if (IsMusicPlayerLaunched()) {
        LogMsg("MusicPlayer detected! Waiting for it to deactivate...");
        Sleep(60);
    }
    srand((unsigned int)time(NULL));
    LogMsg("=== NFSW Radio Mod Initializing ===");
    InitializeCriticalSection(&g_popupCs);
    InitializeCriticalSection(&g_inGamePopupCs);
    InstallMusicLogicHook();

    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiInput, NULL);

    LoadConfig();

    CreateThread(NULL, 0, PopupThreadProc, NULL, 0, NULL);
    g_hMouseThread = CreateThread(NULL, 0, MouseHookThreadProc, NULL, 0, &g_mouseHookThreadId);

    // Wait for the game window to be created and initialized (especially important for server loading)
    LogMsg("Waiting for GameFrame window...");
    HWND hGame = NULL;
    for (int i = 0; i < 600; ++i) { // up to 60s
        hGame = FindGameWindow();
        if (hGame && IsWindowVisible(hGame)) {
            break;
        }
        Sleep(100);
    }
    if (hGame) {
        LogMsg("GameFrame detected (0x%p). Parented mod windows to game.", hGame);
        EnsurePopupParented(g_hPopupWnd);
        EnsurePopupParented(g_hInGamePopupWnd);
    } else {
        LogMsg("GameFrame not detected within timeout; proceeding with initialization.");
    }

    if (!InitFMOD()) {
        LogMsg("Radio initialization aborted due to FMOD failure.");
        return 1;
    }

    ScanRadioFolders();

    if (g_stations.empty()) {
        LogMsg("Warning: No stations with music files found in Sound\\Radio!");
    } else {
        // Initial playback starts silently without popping up the widget
        PlayTrack(0, 0, STATE_PLAYING, false);
    }

    bool wasBaseKeyDown = false;
    bool wasOrderKeyDown = false;
    bool wasSkinKeyDown = false;
    bool wasNextStationKeyDown = false;
    bool wasToggleKeyDown = false;

    while (true) {
        Sleep(16);

        DWORD nowTick = GetTickCount();
        if (nowTick - g_lastRadioRescanTick >= RADIO_RESCAN_INTERVAL_MS) {
            g_lastRadioRescanTick = nowTick;

            size_t prevStations = g_stations.size();
            size_t prevTracks = 0;
            for (const auto& s : g_stations) prevTracks += s.tracks.size();

            RescanRadioPreservingPlayback();

            size_t newTracks = 0;
            for (const auto& s : g_stations) newTracks += s.tracks.size();

            if (g_stations.size() != prevStations || newTracks != prevTracks) {
                LogMsg("Radio folder change detected: %zu stations / %zu tracks (was %zu / %zu)",
                    g_stations.size(), newTracks, prevStations, prevTracks);

                if (prevStations == 0 && !g_stations.empty() && !g_isPlaying && !g_isStopped) {
                    LogMsg("First tracks appeared - auto-starting playback.");
                    PlayTrack(0, 0, STATE_PLAYING, false);
                }
            }
        }

        if (pfn_FMOD_System_Update && g_fmodSystem) {
            pfn_FMOD_System_Update(g_fmodSystem);
        }

        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        bool isGameActive = (fgPid == GetCurrentProcessId());

        if (!isGameActive) {
            if (g_isPlaying && !g_isStopped && !g_autoPaused) {
                if (g_currentChannel && pfn_FMOD_Channel_SetPaused) {
                    pfn_FMOD_Channel_SetPaused(g_currentChannel, 1);
                }
                g_autoPaused = true;
                LogMsg("Game lost focus: radio paused.");
            }
        } else {
            if (g_autoPaused) {
                if (g_isPlaying && !g_isStopped) {
                    if (g_currentChannel && pfn_FMOD_Channel_SetPaused) {
                        pfn_FMOD_Channel_SetPaused(g_currentChannel, 0);
                    }
                    LogMsg("Game regained focus: radio resumed.");
                }
                g_autoPaused = false;
            }
        }

        if (g_isPlaying && !g_isStopped) {
            SilenceGameMusic();
        }

        // Automatic track advancement when finished (Radio Stations)
        if (isGameActive && g_isPlaying && !g_isStopped && !g_autoPaused && g_currentChannel && pfn_FMOD_Channel_IsPlaying) {
            int isPlaying = 0;
            pfn_FMOD_Channel_IsPlaying(g_currentChannel, &isPlaying);
            if (!isPlaying) {
                LogMsg("Current radio track finished. Advancing track (Order: %s).", GetPlayOrderName(g_playOrder));
                if (!g_stations.empty()) {
                    if (g_currentStation < 0 || g_currentStation >= (int)g_stations.size()) {
                        g_currentStation = 0;
                    }
                    Station& st = g_stations[g_currentStation];
                    if (st.tracks.empty()) {
                        // Station has no tracks
                    } else if (g_playOrder == ORDER_REPEAT_TRACK) {
                        PlayTrack(g_currentStation, g_currentTrack, STATE_PLAYING, true);
                    } else if (g_playOrder == ORDER_SHUFFLE) {
                        int nextTrack = 0;
                        if (st.tracks.size() == 2) {
                            nextTrack = 1 - g_currentTrack;
                        } else if (st.tracks.size() > 2) {
                            int offset = 1 + (rand() % ((int)st.tracks.size() - 1));
                            nextTrack = (g_currentTrack + offset) % (int)st.tracks.size();
                        }
                        PlayTrack(g_currentStation, nextTrack, STATE_PLAYING, true);
                        } else {
                        // ORDER_DEFAULT and ORDER_REPEAT_PLAYLIST:
                        // Default loops back silently just like repeat playlist so music never stops.
                        int nextTrack = (g_currentTrack + 1) % (int)st.tracks.size();
                        PlayTrack(g_currentStation, nextTrack, STATE_PLAYING, true);
                    }
                }
            }
        }

        // Automatic track advancement when finished (In-Game Music)
        if (isGameActive && (g_isStopped || g_stations.empty()) && !g_autoPaused) {
            void* pLogic = g_pMusicLogic;
            if (pLogic && IsMusicLogicVoicesReady(pLogic)) {
                uint32_t state = SafeGetMusicLogicState(pLogic);

                if (state == 4) {
                    if (!g_inGameTrackPlaying) {
                        // Adopt active track if game started playing on its own
                        g_inGameTrackPlaying = true;
                        g_inGameTrackConfirmedPlaying = true;
                        g_inGameTrackPlayStartTick = GetTickCount();
                    } else {
                        g_inGameTrackConfirmedPlaying = true;
                    }
                }

                if (g_inGameTrackPlaying && g_inGameTrackConfirmedPlaying) {
                    DWORD nowTick = GetTickCount();
                    // Grace period of 3000ms after track start to avoid instant re-triggering during transitions
                    if (nowTick - g_inGameTrackPlayStartTick >= 3000) {
                        if (state != 4) {
                            LogMsg("In-game track finished (state=%u). Auto-advancing track (Order: %s).", state, GetPlayOrderName(g_inGamePlayOrder));
                            g_inGameTrackPlaying = false;
                            g_inGameTrackConfirmedPlaying = false;
                            SwitchInGameTrack(false /* isManual = false */);
                        }
                    }
                }
            }
        }

        // Hotkey processing
        if (isGameActive) {
            bool isBaseKeyDown = (g_cfgRadioKey != 0) && ((GetAsyncKeyState(g_cfgRadioKey) & 0x8000) != 0);
            bool isOrderKeyDown = (g_cfgPlaylistOrderMod != 0) && ((GetAsyncKeyState(g_cfgPlaylistOrderMod) & 0x8000) != 0);
            bool isSkinKeyDown = (g_cfgSkinMod != 0) && ((GetAsyncKeyState(g_cfgSkinMod) & 0x8000) != 0);
            bool isNextStationKeyDown = (g_cfgNextStationMod != 0) && ((GetAsyncKeyState(g_cfgNextStationMod) & 0x8000) != 0);
            bool isToggleKeyDown = (g_cfgTogglePlaybackMod != 0) && ((GetAsyncKeyState(g_cfgTogglePlaybackMod) & 0x8000) != 0);

            bool baseJustPressed = isBaseKeyDown && !wasBaseKeyDown;
            bool orderJustPressed = isOrderKeyDown && !wasOrderKeyDown;
            bool skinJustPressed = isSkinKeyDown && !wasSkinKeyDown;
            bool nextStationJustPressed = isNextStationKeyDown && !wasNextStationKeyDown;
            bool toggleJustPressed = isToggleKeyDown && !wasToggleKeyDown;

            if (baseJustPressed || orderJustPressed || skinJustPressed || nextStationJustPressed || toggleJustPressed) {
                g_receivedUserInput = true;
                if (isBaseKeyDown && isSkinKeyDown && (baseJustPressed || skinJustPressed)) {
                    // Next Skin (Default: G + Tab) - disabled when radio is paused
                    if (!g_isStopped) {
                        SwitchToNextSkin();
                    }
                } else if (isBaseKeyDown && isOrderKeyDown && (baseJustPressed || orderJustPressed)) {
                    if (!g_isStopped) {
                        // Radio Playlist order toggle
                        g_playOrder = (PlayOrder)((g_playOrder + 1) % ORDER_COUNT);
                        LogMsg("Playlist order changed to: %s", GetPlayOrderName(g_playOrder));

                        std::string stName = "RADIO";
                        if (!g_stations.empty() && g_currentStation >= 0 && g_currentStation < (int)g_stations.size()) {
                            stName = g_stations[g_currentStation].name;
                        }
                        TriggerPopup(STATE_ORDER, stName, g_currentTrackTitle, g_currentArtist, "ORDER", GetPlayOrderName(g_playOrder));
                    } else {
                        // In-game playlist order toggle
                        g_inGamePlayOrder = (PlayOrder)((g_inGamePlayOrder + 1) % ORDER_COUNT);
                        if (g_inGamePlayOrder == ORDER_SHUFFLE) {
                            EnsureInGamePlaylists();
                            SyncShuffleDeckToCurrentTrack(g_currentInGamePlaylist, g_currentInGameTrackInPlaylist);
                        }
                        LogMsg("In-game playlist order changed to: %s", GetPlayOrderName(g_inGamePlayOrder));
                        TriggerInGameMusicPopup(std::string("ORDER: ") + GetPlayOrderName(g_inGamePlayOrder), true);
                    }
                } else if (isBaseKeyDown && isToggleKeyDown && (baseJustPressed || toggleJustPressed)) {
                    // Toggle playback (Default: Q + Tab)
                    if (!g_isStopped) {
                        LogMsg("User paused radio playback.");
                        if (g_currentChannel && pfn_FMOD_Channel_SetPaused) {
                            pfn_FMOD_Channel_SetPaused(g_currentChannel, 1);
                        }
                        g_isStopped = true;
                        RestoreGameMusic();

                        // Start in-game track if none currently playing
                        void* pLogic = g_pMusicLogic ? g_pMusicLogic : GetMusicLogicInstance();
                        if (pLogic && IsMusicLogicVoicesReady(pLogic)) {
                            uint32_t state = SafeGetMusicLogicState(pLogic);
                            if (state != 4) { // Not playing -> start current in-game playlist track
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

                        std::string stName = (!g_stations.empty()) ? g_stations[g_currentStation].name : "RADIO";
                        TriggerPopup(STATE_PAUSED, stName, g_currentTrackTitle, g_currentArtist, "PAUSED");
                    } else {
                        LogMsg("User resumed radio playback.");
                        g_inGameTrackPlaying = false;
                        g_inGameTrackConfirmedPlaying = false;
                        StopGameMusic();
                        SilenceGameMusic();
                        if (g_currentChannel && pfn_FMOD_Channel_SetPaused) {
                            pfn_FMOD_Channel_SetPaused(g_currentChannel, 0);
                        } else {
                            PlayTrack(g_currentStation, g_currentTrack, STATE_PLAYING, true);
                        }
                        g_isStopped = false;
                        g_isPlaying = true;
                        std::string stName = (!g_stations.empty()) ? g_stations[g_currentStation].name : "RADIO";
                        TriggerPopup(STATE_PLAYING, stName, g_currentTrackTitle, g_currentArtist, "PLAYING");
                    }
                } else if (isBaseKeyDown && isNextStationKeyDown && (baseJustPressed || nextStationJustPressed)) {
                    // Next Station (Default: CapsLock + Tab)
                    if (!g_isStopped) {
                        if (!g_stations.empty()) {
                            int nextStation = (g_currentStation + 1) % (int)g_stations.size();
                            LogMsg("Switching to next station [%d -> %d]: %s",
                                   g_currentStation, nextStation, g_stations[nextStation].name.c_str());
                            PlayTrack(nextStation, 0, STATE_PLAYING, true);
                        }
                    } else {
                        // When radio is paused: switch in-game playlist
                        SwitchInGamePlaylist();
                    }
                } else if (baseJustPressed) {
                    // Just RadioKey pressed (Next Track)
                    if (g_isStopped || g_stations.empty()) {
                        LogMsg("Radio is paused: switching in-game music track.");
                        SwitchInGameTrack(true /* isManual = true */);
                    } else {
                        if (g_currentStation < 0 || g_currentStation >= (int)g_stations.size()) {
                            g_currentStation = 0;
                        }
                        Station& st = g_stations[g_currentStation];
                        if (st.tracks.empty()) {
                            // Station has no tracks
                        } else if (g_playOrder == ORDER_SHUFFLE) {
                            int nextTrack = 0;
                            if (st.tracks.size() == 2) {
                                nextTrack = 1 - g_currentTrack;
                            } else if (st.tracks.size() > 2) {
                                int offset = 1 + (rand() % ((int)st.tracks.size() - 1));
                                nextTrack = (g_currentTrack + offset) % (int)st.tracks.size();
                            }
                            LogMsg("Switching to next track in station '%s' [%d -> %d]",
                                   st.name.c_str(), g_currentTrack, nextTrack);
                            PlayTrack(g_currentStation, nextTrack, STATE_PLAYING, true);
                        } else {
                            int nextTrack = (g_currentTrack + 1) % (int)st.tracks.size();
                            LogMsg("Switching to next track in station '%s' [%d -> %d]",
                                   st.name.c_str(), g_currentTrack, nextTrack);
                            PlayTrack(g_currentStation, nextTrack, STATE_PLAYING, true);
                        }
                    }
                }
            }

            wasBaseKeyDown = isBaseKeyDown;
            wasOrderKeyDown = isOrderKeyDown;
            wasSkinKeyDown = isSkinKeyDown;
            wasNextStationKeyDown = isNextStationKeyDown;
            wasToggleKeyDown = isToggleKeyDown;
        } else {
            wasBaseKeyDown = false;
            wasOrderKeyDown = false;
            wasSkinKeyDown = false;
            wasNextStationKeyDown = false;
            wasToggleKeyDown = false;
        }
    }

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        SignalRadioLaunched();
        CreateThread(NULL, 0, RadioMainThread, NULL, 0, NULL);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        SetEnvironmentVariableA("NFSW_RADIO_LOADED", "0");
        SetEnvironmentVariableA("NFSW_RADIOMOD_LOADED", "0");
        if (g_hRadioActiveEvent) {
            CloseHandle(g_hRadioActiveEvent);
            g_hRadioActiveEvent = NULL;
        }
        if (g_hRadioModActiveEvent) {
            CloseHandle(g_hRadioModActiveEvent);
            g_hRadioModActiveEvent = NULL;
        }
        SaveConfigVolume();
        SaveConfigSkin(g_skin.name);
        if (g_popupX != -1 && g_popupY != -1) {
            SaveConfigPosition(g_popupX, g_popupY);
        }
        if (g_hMouseHook) {
            UnhookWindowsHookEx(g_hMouseHook);
            g_hMouseHook = NULL;
        }
        if (g_mouseHookThreadId) {
            PostThreadMessageA(g_mouseHookThreadId, WM_QUIT, 0, 0);
        }
        if (g_currentChannel && pfn_FMOD_Channel_Stop) {
            pfn_FMOD_Channel_Stop(g_currentChannel);
        }
        if (g_currentSound && pfn_FMOD_Sound_Release) {
            pfn_FMOD_Sound_Release(g_currentSound);
        }
        if (g_fmodSystem && pfn_FMOD_System_Release) {
            pfn_FMOD_System_Close(g_fmodSystem);
            pfn_FMOD_System_Release(g_fmodSystem);
        }
        if (g_hFmod) {
            FreeLibrary(g_hFmod);
        }
        if (g_pSkinBitmap) {
            delete g_pSkinBitmap;
            g_pSkinBitmap = NULL;
        }
        if (g_pCurrentCover) {
            delete g_pCurrentCover;
            g_pCurrentCover = NULL;
        }
        for (int i = 0; i < STATE_COUNT; ++i) {
            if (g_skin.states[i].pStateBitmap) {
                delete g_skin.states[i].pStateBitmap;
                g_skin.states[i].pStateBitmap = NULL;
            }
        }
        if (g_skin.fontLoaded && !g_skin.loadedFontPath.empty()) {
            RemoveFontResourceExA(g_skin.loadedFontPath.c_str(), FR_PRIVATE, 0);
        }
        if (g_gdiplusToken) {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
        g_popupDIB.Cleanup();
        g_inGamePopupDIB.Cleanup();
        UninstallMusicLogicHook();
        DeleteCriticalSection(&g_popupCs);
        DeleteCriticalSection(&g_inGamePopupCs);
    }
    return TRUE;
}