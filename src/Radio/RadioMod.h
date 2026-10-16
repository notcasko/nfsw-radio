#pragma once

#include "RadioCommon.h"

void PlayTrack(int stationIdx, int trackIdx, PlayerState triggerReason = STATE_PLAYING, bool showPopup = true);
void ScanRadioFolders();
bool InitFMOD();
void SignalRadioLaunched();
void SignalRadioModLaunched();
bool IsMusicPlayerLaunched();

extern "C" __declspec(dllexport) BOOL IsRadioLoaded(void);
extern "C" __declspec(dllexport) BOOL IsRadioModLoaded(void);
