#pragma once

#include "RadioCommon.h"

// Radio Mod Popup Rendering & Lifecycle
void RenderPopup(int alpha);
void TriggerPopup(PlayerState state, const std::string& station, const std::string& track,
                  const std::string& artist = "", const std::string& status = "",
                  const std::string& order = "", const std::string& volume = "");
void ChangeVolume(bool increase);

// In-Game Music Popup Rendering & Lifecycle
void RenderInGamePopup(int alpha);
void TriggerInGameMusicPopup(const std::string& trackName, bool force = false);

// Window Procedure and Thread Procedures
LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
DWORD WINAPI PopupThreadProc(LPVOID lpParam);

// Low-Level Mouse Hook Procedures
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
DWORD WINAPI MouseHookThreadProc(LPVOID lpParam);
