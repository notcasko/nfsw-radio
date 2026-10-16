#include "InGameMusic.h"
#include "RadioPopup.h"

void* g_pMusicLogic = NULL;
t_MusicLogic_Update g_origMusicLogic_Update = NULL;

std::vector<InGamePlaylist> g_inGamePlaylists;
int g_currentInGamePlaylist = 0;
int g_currentInGameTrackInPlaylist = 0;
int g_currentInGameTrackIndex = -1;
PlayOrder g_inGamePlayOrder = ORDER_DEFAULT;
float g_inGameMusicVolume = 1.0f;

std::vector<int> g_inGameShuffleDeck;
int g_shuffleDeckIndex = 0;

bool g_inGameTrackPlaying = false;
uint32_t g_currentInGameTrackId = 0;
DWORD g_inGameTrackPlayStartTick = 0;
bool g_inGameTrackConfirmedPlaying = false;

static void __fastcall Hooked_MusicLogic_Update(void* thisPtr, void* edx)
{
    g_pMusicLogic = thisPtr;
    if (g_origMusicLogic_Update && g_origMusicLogic_Update != (t_MusicLogic_Update)&Hooked_MusicLogic_Update) {
        g_origMusicLogic_Update(thisPtr);
    }
}

void InstallMusicLogicHook()
{
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t delta = base - 0x00400000;
    void** pVtableEntry = (void**)(0x00bd8a30 + delta);

    DWORD oldProt = 0;
    if (VirtualProtect(pVtableEntry, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
        if (*pVtableEntry != (void*)&Hooked_MusicLogic_Update) {
            g_origMusicLogic_Update = (t_MusicLogic_Update)*pVtableEntry;
            *pVtableEntry = (void*)&Hooked_MusicLogic_Update;
            LogMsg("Hooked MusicLogic::Update vtable at 0x%p (orig: 0x%p)", pVtableEntry, g_origMusicLogic_Update);
        }
        VirtualProtect(pVtableEntry, sizeof(void*), oldProt, &oldProt);
    }
}

void UninstallMusicLogicHook()
{
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
        }
        g_origMusicLogic_Update = NULL;
    }
}

void* GetMusicLogicInstance()
{
    if (g_pMusicLogic) return g_pMusicLogic;

    // Rate-limit memory scanning to at most once per 2 seconds to avoid startup stalls/crashes
    static DWORD s_lastScanTick = 0;
    DWORD now = GetTickCount();
    if (s_lastScanTick != 0 && (now - s_lastScanTick < 2000)) {
        return NULL;
    }
    s_lastScanTick = now;

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
            __try {
                for (size_t i = 0; i < count; ++i) {
                    if (pWords[i] == targetVtable) {
                        uintptr_t candidate = (uintptr_t)&pWords[i];
                        if (candidate != targetVtable && (i + (0x68 / sizeof(uintptr_t)) < count)) {
                            uintptr_t tStart = *(uintptr_t*)(candidate + 0x64);
                            uintptr_t tEnd   = *(uintptr_t*)(candidate + 0x68);
                            if (tStart != 0 && tEnd > tStart && ((tEnd - tStart) % 0x108 == 0)) {
                                g_pMusicLogic = (void*)candidate;
                                LogMsg("Discovered Sound::MusicLogic instance via memory scan: 0x%p", g_pMusicLogic);
                                return g_pMusicLogic;
                            }
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return g_pMusicLogic;
}

uint32_t SafeGetMusicLogicState(void* pLogic)
{
    if (!pLogic) return 0;
    __try {
        return *(uint32_t*)((uintptr_t)pLogic + 0x50);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uint32_t SafeGetMusicLogicTrackId(void* pLogic)
{
    if (!pLogic) return 0;
    __try {
        return *(uint32_t*)((uintptr_t)pLogic + 0x54);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool IsMusicLogicVoicesReady(void* pLogic)
{
    if (!pLogic) return false;
    __try {
        uintptr_t pVoice38 = *(uintptr_t*)((uintptr_t)pLogic + 0x38);
        uintptr_t pVoice40 = *(uintptr_t*)((uintptr_t)pLogic + 0x40);
        return (pVoice38 != 0 && pVoice40 != 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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

void StopGameMusic()
{
    void* pLogic = g_pMusicLogic ? g_pMusicLogic : GetMusicLogicInstance();
    if (!pLogic) return;

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t delta = base - 0x00400000;
    SafeInvokeStopTrack(pLogic, 0x00816b90 + delta);
}

void SetInGameMusicBusVolume(float vol)
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

void SetInGameMusicVolume(float vol)
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

void SilenceGameMusic()
{
    SetInGameMusicBusVolume(0.0f);

    void* pLogic = g_pMusicLogic ? g_pMusicLogic : GetMusicLogicInstance();
    if (pLogic) {
        uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
        uintptr_t delta = base - 0x00400000;
        SafeInvokeCrossfade(pLogic, 0x00816390 + delta, 0.0f);
        uint32_t state = SafeGetMusicLogicState(pLogic);
        if (state == 4) {
            StopGameMusic();
        }
    }
}

void RestoreGameMusic()
{
    SetInGameMusicVolume(g_inGameMusicVolume);
}

std::string ExtractInGamePlaylistName(const char* rawPath)
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

bool BuildInGamePlaylists()
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

void EnsureInGamePlaylists()
{
    if (g_inGamePlaylists.empty()) {
        BuildInGamePlaylists();
    }
}

std::string FormatInGameTrackName(const char* rawPath)
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

void ReshuffleDeck(int playlistIdx, int lastPlayedTrackInPl)
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

void SyncShuffleDeckToCurrentTrack(int playlistIdx, int currentTrackInPl)
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

void ChangeInGameVolume(bool increase)
{
    if (increase) {
        g_inGameMusicVolume += 0.05f;
        if (g_inGameMusicVolume > 1.0f) g_inGameMusicVolume = 1.0f;
    } else {
        g_inGameMusicVolume -= 0.05f;
        if (g_inGameMusicVolume < 0.0f) g_inGameMusicVolume = 0.0f;
    }

    SetInGameMusicVolume(g_inGameMusicVolume);
    int volPercent = (int)std::round(g_inGameMusicVolume * 100.0f);
    LogMsg("In-game music volume changed to: %d%%", volPercent);
    TriggerInGameMusicPopup("VOLUME: " + std::to_string(volPercent) + "%", true);
}

void PlayInGameTrackByIndex(int globalIdx, const std::string& customPrefix)
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

    RestoreGameMusic();

    g_currentInGameTrackId = nextTrack.trackId;
    g_inGameTrackPlaying = true;
    g_inGameTrackConfirmedPlaying = false;
    g_inGameTrackPlayStartTick = GetTickCount();

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

void SwitchInGameTrack(bool isManual)
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
    } else if (g_inGamePlayOrder == ORDER_REPEAT_TRACK && !isManual) {
        // Auto-advancing during ORDER_REPEAT_TRACK repeats the current track
        nextIdxInPl = g_currentInGameTrackInPlaylist;
    } else if (g_inGamePlayOrder == ORDER_DEFAULT) {
        int candIdx = g_currentInGameTrackInPlaylist + 1;
        if (candIdx >= (int)pl.trackIndices.size() && !isManual) {
            SwitchInGamePlaylist();
            return;
        } else {
            nextIdxInPl = candIdx % (int)pl.trackIndices.size();
        }
    } else {
        // ORDER_REPEAT_PLAYLIST and manual advance during ORDER_REPEAT_TRACK:
        // All loop back silently to track 0 when reaching the end so music never stops.
        nextIdxInPl = (g_currentInGameTrackInPlaylist + 1) % (int)pl.trackIndices.size();
    }

    int prevIdxInPl = g_currentInGameTrackInPlaylist;
    g_currentInGameTrackInPlaylist = nextIdxInPl;
    int globalIdx = pl.trackIndices[nextIdxInPl];

    LogMsg("Switching to next in-game track in playlist '%s' [%d -> %d] (Order: %s, %s)",
           pl.name.c_str(), prevIdxInPl, nextIdxInPl, GetPlayOrderName(g_inGamePlayOrder),
           isManual ? "Manual" : "Auto");
    PlayInGameTrackByIndex(globalIdx);
}

void SwitchInGamePlaylist()
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
