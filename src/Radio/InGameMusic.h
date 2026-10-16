#pragma once

#include "RadioCommon.h"

#pragma pack(push, 1)
struct InGameTrackEntry {
    uint32_t trackId;
    uint32_t param1;
    char path[256];
};
#pragma pack(pop)

struct InGamePlaylist {
    std::string name;
    std::vector<int> trackIndices;
};

extern void* g_pMusicLogic;
typedef void (__thiscall *t_MusicLogic_Update)(void* thisPtr);
extern t_MusicLogic_Update g_origMusicLogic_Update;

extern std::vector<InGamePlaylist> g_inGamePlaylists;
extern int g_currentInGamePlaylist;
extern int g_currentInGameTrackInPlaylist;
extern int g_currentInGameTrackIndex;
extern PlayOrder g_inGamePlayOrder;
extern float g_inGameMusicVolume;

// Shuffle Deck State
extern std::vector<int> g_inGameShuffleDeck;
extern int g_shuffleDeckIndex;

// In-Game Playback Tracking
extern bool g_inGameTrackPlaying;
extern uint32_t g_currentInGameTrackId;
extern DWORD g_inGameTrackPlayStartTick;
extern bool g_inGameTrackConfirmedPlaying;

static const int INGAME_POPUP_W = 800;
static const int INGAME_POPUP_H = 44;

// Game memory and logic functions
void InstallMusicLogicHook();
void UninstallMusicLogicHook();
void* GetMusicLogicInstance();
uint32_t SafeGetMusicLogicState(void* pLogic);
uint32_t SafeGetMusicLogicTrackId(void* pLogic);
bool IsMusicLogicVoicesReady(void* pLogic);

// In-Game Music playback & volume controls
void StopGameMusic();
void SetInGameMusicBusVolume(float vol);
void SetInGameMusicVolume(float vol);
void SilenceGameMusic();
void RestoreGameMusic();
void ChangeInGameVolume(bool increase);

// Shuffle Deck Helpers
void ReshuffleDeck(int playlistIdx, int lastPlayedTrackInPl = -1);
void SyncShuffleDeckToCurrentTrack(int playlistIdx, int currentTrackInPl);

// In-Game Playlist management & track switching
std::string ExtractInGamePlaylistName(const char* rawPath);
bool BuildInGamePlaylists();
void EnsureInGamePlaylists();
std::string FormatInGameTrackName(const char* rawPath);
void PlayInGameTrackByIndex(int globalIdx, const std::string& customPrefix = "");
void SwitchInGameTrack(bool isManual = true);
void SwitchInGamePlaylist();
