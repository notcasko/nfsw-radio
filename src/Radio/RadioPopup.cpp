#include "RadioPopup.h"
#include "RadioConfig.h"
#include "RadioSkin.h"
#include "InGameMusic.h"

static bool s_isDragging = false;
static POINT s_dragMouseStart = {0, 0};
static POINT s_dragWndStart = {0, 0};

void RenderPopup(int alpha)
{
    if (!g_hPopupWnd) return;

    int w = g_skin.width;
    int h = g_skin.height;
    if (w <= 0) w = 400;
    if (h <= 0) h = 150;

    EnsurePopupParented(g_hPopupWnd);
    HWND hGame = FindGameWindow();

    RECT rcClient = {0};
    int clientW = 0, clientH = 0;
    if (hGame && GetClientRect(hGame, &rcClient)) {
        clientW = rcClient.right - rcClient.left;
        clientH = rcClient.bottom - rcClient.top;
    } else {
        clientW = GetSystemMetrics(SM_CXSCREEN);
        clientH = GetSystemMetrics(SM_CYSCREEN);
    }

    if (g_popupX < 0 || (clientW > 0 && g_popupX > clientW - 30) ||
        g_popupY < 0 || (clientH > 0 && g_popupY > clientH - 30)) {
        g_popupX = 35;
        g_popupY = (clientH > h) ? (clientH - h) / 2 : 35;
    }

    int x = g_popupX;
    int y = g_popupY;

    HDC hdcScreen = GetDC(NULL);
    g_popupDIB.Ensure(hdcScreen, w, h);
    if (!g_popupDIB.memDC || !g_popupDIB.pBits) {
        ReleaseDC(NULL, hdcScreen);
        return;
    }

    void* pBits = g_popupDIB.pBits;

    {
        Gdiplus::Bitmap dibBmp(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)pBits);
        Gdiplus::Graphics g(&dibBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        EnterCriticalSection(&g_popupCs);
        PlayerState activeState = g_popupPlayerState;
        PlayOrder activeOrder = g_popupPlayOrder;
        std::string stStation = g_popupStationName;
        std::string stTrack = g_popupTrackTitle;
        std::string stArtist = g_popupArtistName;
        std::string stStatus = g_popupStatus;
        std::string stOrder = g_popupOrder;
        std::string stVolume = g_popupVolume;

        const StateDisplayConfig& sc = g_skin.states[activeState];
        Gdiplus::Bitmap* pActiveSkin = sc.pStateBitmap ? sc.pStateBitmap : g_pSkinBitmap;

        const RectCoords& rStation = sc.hasStationRect ? sc.stationRect : g_skin.stationRect;
        const RectCoords& rTrack   = sc.hasTrackRect   ? sc.trackRect   : g_skin.trackRect;
        const RectCoords& rArtist  = sc.hasArtistRect  ? sc.artistRect  : g_skin.artistRect;
        const RectCoords& rStatus  = sc.hasStatusRect  ? sc.statusRect  : g_skin.statusRect;
        const RectCoords& rOrder   = sc.hasOrderRect   ? sc.orderRect   : g_skin.orderRect;
        const RectCoords& rVolume  = sc.hasVolumeRect  ? sc.volumeRect  : g_skin.volumeRect;
        const CoverCoords& rCover  = sc.hasCoverRect   ? sc.coverRect   : g_skin.coverRect;

        const bool useProceduralLayout = (g_pSkinBitmap == NULL) &&
                                 (_stricmp(g_skin.name.c_str(), "Default") == 0);

        bool showStation = useProceduralLayout ? true : sc.showStation;
        bool showTrack   = useProceduralLayout ? true : sc.showTrack;
        bool showArtist  = useProceduralLayout ? false : sc.showArtist;
        bool showStatus  = useProceduralLayout ? true : sc.showStatus;
        bool showOrder   = useProceduralLayout ? true : sc.showOrder;
        bool showVolume  = useProceduralLayout ? true : sc.showVolume;
        bool showCover   = (useProceduralLayout ? true : sc.showCover) && rCover.enabled;

        auto drawCover = [&]() {
            if (showCover && g_pCurrentCover) {
                if (rCover.drawBackground) {
                    Gdiplus::Color bgColor = rCover.hasBgColor ?
                        Gdiplus::Color(255, GetRValue(rCover.bgColor), GetGValue(rCover.bgColor), GetBValue(rCover.bgColor)) :
                        Gdiplus::Color(255, 35, 35, 40);
                    Gdiplus::SolidBrush bgBrush(bgColor);
                    g.FillRectangle(&bgBrush, (Gdiplus::REAL)rCover.x, (Gdiplus::REAL)rCover.y, (Gdiplus::REAL)rCover.width, (Gdiplus::REAL)rCover.height);
                }
                DrawCoverImage(g, g_pCurrentCover, rCover);
            }
        };

        auto drawSkin = [&]() {
            if (pActiveSkin) {
                g.DrawImage(pActiveSkin, 0, 0, w, h);
            } else if (useProceduralLayout) {
                Gdiplus::GraphicsPath path;
                int r = std::min(16, std::min(w, h) / 4);
                if (r < 2) r = 2;
                path.AddArc(0, 0, r, r, 180, 90);
                path.AddArc(w - r - 1, 0, r, r, 270, 90);
                path.AddArc(w - r - 1, h - r - 1, r, r, 0, 90);
                path.AddArc(0, h - r - 1, r, r, 90, 90);
                path.CloseFigure();

                Gdiplus::SolidBrush bgBrush(Gdiplus::Color(225, 18, 20, 26));
                g.FillPath(&bgBrush, &path);

                Gdiplus::Pen borderPen(Gdiplus::Color(255, 0, 210, 255), 2.0f);
                g.DrawPath(&borderPen, &path);
            }
        };

        if (!useProceduralLayout && rCover.zOrder == "below") {
            drawCover();
            drawSkin();
        } else {
            drawSkin();
            drawCover();
        }

        std::wstring wFontName = StringToWide(g_skin.fontName);
        std::wstring fontNameToUse = wFontName;
        {
            Gdiplus::FontFamily probe(wFontName.c_str());
            if (!probe.IsAvailable()) {
                Gdiplus::FontFamily fb(L"Segoe UI");
                fontNameToUse = fb.IsAvailable() ? L"Segoe UI" : L"Arial";
            }
        }
        Gdiplus::FontFamily fontFamily(fontNameToUse.c_str());

        auto renderField = [&](const RectCoords& rc, const std::string& text, Gdiplus::Color defaultColor, bool allowScroll = false) {
            if (!rc.enabled || text.empty()) return;
            int fs = (rc.fontSize > 0) ? rc.fontSize : g_skin.fontSize;
            if (fs <= 0) fs = 14;

            std::wstring wtext = StringToWide(text);
            std::wstring fieldFontName = GetBestFontForText(fontNameToUse, wtext);
            Gdiplus::FontFamily fieldFontFamily(fieldFontName.c_str());
            const Gdiplus::FontFamily* pFontFam = fieldFontFamily.IsAvailable() ? &fieldFontFamily : &fontFamily;

            Gdiplus::Font* pFont = CreateSafeGdiplusFont(pFontFam, (float)fs, true);
            Gdiplus::Color textColor = rc.hasColor ?
                Gdiplus::Color(255, GetRValue(rc.color), GetGValue(rc.color), GetBValue(rc.color)) : defaultColor;

            Gdiplus::SolidBrush textBrush(textColor);

            Gdiplus::StringFormat format;
            if (rc.align == "center") {
                format.SetAlignment(Gdiplus::StringAlignmentCenter);
            } else if (rc.align == "right" || rc.align == "far") {
                format.SetAlignment(Gdiplus::StringAlignmentFar);
            } else {
                format.SetAlignment(Gdiplus::StringAlignmentNear);
            }
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

            Gdiplus::RectF textBounds;
            Gdiplus::RectF measureRect(0, 0, 10000.0f, (Gdiplus::REAL)rc.height);
            g.MeasureString(wtext.c_str(), -1, pFont, measureRect, &format, &textBounds);

            if (rc.drawBackground) {
                Gdiplus::Color bgColor = rc.hasBgColor ?
                    Gdiplus::Color(255, GetRValue(rc.bgColor), GetGValue(rc.bgColor), GetBValue(rc.bgColor)) :
                    Gdiplus::Color(255, 35, 35, 40);
                Gdiplus::SolidBrush bgBrush(bgColor);

                Gdiplus::RectF bgRect;
                if (textBounds.Width < (Gdiplus::REAL)rc.width) {
                    float bgW = textBounds.Width + 6.0f;
                    if (bgW > (Gdiplus::REAL)rc.width) bgW = (Gdiplus::REAL)rc.width;

                    float bgX = (Gdiplus::REAL)rc.x;
                    if (rc.align == "center") {
                        bgX = (Gdiplus::REAL)rc.x + ((Gdiplus::REAL)rc.width - bgW) / 2.0f;
                    } else if (rc.align == "right" || rc.align == "far") {
                        bgX = (Gdiplus::REAL)rc.x + (Gdiplus::REAL)rc.width - bgW;
                    } else {
                        bgX = (Gdiplus::REAL)rc.x;
                    }
                    bgRect = Gdiplus::RectF(bgX, (Gdiplus::REAL)rc.y, bgW, (Gdiplus::REAL)rc.height);
                } else {
                    bgRect = Gdiplus::RectF((Gdiplus::REAL)rc.x, (Gdiplus::REAL)rc.y, (Gdiplus::REAL)rc.width, (Gdiplus::REAL)rc.height);
                }
                g.FillRectangle(&bgBrush, bgRect);
            }

            Gdiplus::RectF layoutRect = rc.drawBackground ?
                Gdiplus::RectF((Gdiplus::REAL)rc.x + 3.0f, (Gdiplus::REAL)rc.y, (Gdiplus::REAL)rc.width - 6.0f, (Gdiplus::REAL)rc.height) :
                Gdiplus::RectF((Gdiplus::REAL)rc.x, (Gdiplus::REAL)rc.y, (Gdiplus::REAL)rc.width, (Gdiplus::REAL)rc.height);

            if (!allowScroll || textBounds.Width <= layoutRect.Width) {
                format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                g.DrawString(wtext.c_str(), -1, pFont, layoutRect, &format, &textBrush);
            } else {
                format.SetTrimming(Gdiplus::StringTrimmingNone);
                g.SetClip(layoutRect);

                float textW = textBounds.Width;
                float gap = 50.0f;
                float cycleLen = textW + gap;

                DWORD now = GetTickCount();
                DWORD elapsed = (now >= g_popupHoldStart) ? (now - g_popupHoldStart) : 0;

                float offset = 0.0f;
                if (elapsed > 500) {
                    float scrollTime = (float)(elapsed - 500) / 1000.0f;
                    float speed = 40.0f;
                    offset = fmodf(scrollTime * speed, cycleLen);
                }

                Gdiplus::RectF drawRect1(layoutRect.X - offset, layoutRect.Y, textW + gap, layoutRect.Height);
                g.DrawString(wtext.c_str(), -1, pFont, drawRect1, &format, &textBrush);

                Gdiplus::RectF drawRect2(layoutRect.X - offset + cycleLen, layoutRect.Y, textW + gap, layoutRect.Height);
                g.DrawString(wtext.c_str(), -1, pFont, drawRect2, &format, &textBrush);

                g.ResetClip();
            }
            delete pFont;
        };

        if (showStation && !stStation.empty()) {
            renderField(rStation, FormatFieldText(rStation, "", stStation), Gdiplus::Color(255, 0, 210, 255), true);
        }

        std::string displayTrack = stTrack;
        if (showArtist && rArtist.enabled && !stArtist.empty()) {
            std::string prefixCheck = stArtist + " - ";
            if (displayTrack.size() > prefixCheck.size() && _strnicmp(displayTrack.c_str(), prefixCheck.c_str(), prefixCheck.size()) == 0) {
                displayTrack = displayTrack.substr(prefixCheck.size());
            }
            renderField(rArtist, FormatFieldText(rArtist, "ARTIST: ", stArtist), Gdiplus::Color(255, 0, 210, 255));
        } else if (!showArtist && !stArtist.empty()) {
            if (displayTrack.find(stArtist) == std::string::npos) {
                displayTrack = stArtist + " - " + displayTrack;
            }
        }

        if (showTrack && !displayTrack.empty()) {
            renderField(rTrack, FormatFieldText(rTrack, "", displayTrack), Gdiplus::Color(255, 255, 255, 255), true);
        }
        if (showStatus && !stStatus.empty()) {
            renderField(rStatus, FormatFieldText(rStatus, "STATUS: ", stStatus), Gdiplus::Color(255, 215, 0));
        }
        if (showOrder) {
            Gdiplus::Bitmap* pOrderIcon = (activeOrder >= 0 && activeOrder < ORDER_COUNT) ? g_skin.orderIcons[activeOrder] : NULL;
            if (pOrderIcon) {
                if (rOrder.drawBackground) {
                    Gdiplus::Color bgColor = rOrder.hasBgColor ?
                        Gdiplus::Color(255, GetRValue(rOrder.bgColor), GetGValue(rOrder.bgColor), GetBValue(rOrder.bgColor)) :
                        Gdiplus::Color(255, 35, 35, 40);
                    Gdiplus::SolidBrush bgBrush(bgColor);
                    g.FillRectangle(&bgBrush, (Gdiplus::REAL)rOrder.x, (Gdiplus::REAL)rOrder.y, (Gdiplus::REAL)rOrder.width, (Gdiplus::REAL)rOrder.height);
                }
                DrawIconImage(g, pOrderIcon, rOrder);
                if (rOrder.showText && !stOrder.empty()) {
                    renderField(rOrder, FormatFieldText(rOrder, "ORDER: ", stOrder), Gdiplus::Color(255, 0, 210, 255));
                }
            } else if (!stOrder.empty()) {
                renderField(rOrder, FormatFieldText(rOrder, "ORDER: ", stOrder), Gdiplus::Color(255, 0, 210, 255));
            }
        }
        if (showVolume) {
            if (rVolume.type == "bar" || rVolume.type == "progressbar" || rVolume.type == "progress") {
                float vol = g_volume;
                if (vol < 0.0f) vol = 0.0f;
                if (vol > 1.0f) vol = 1.0f;

                Gdiplus::RectF bgRect((Gdiplus::REAL)rVolume.x, (Gdiplus::REAL)rVolume.y,
                                      (Gdiplus::REAL)rVolume.width, (Gdiplus::REAL)rVolume.height);

                Gdiplus::Color bgColor = rVolume.hasBgColor ?
                    Gdiplus::Color(255, GetRValue(rVolume.bgColor), GetGValue(rVolume.bgColor), GetBValue(rVolume.bgColor)) :
                    Gdiplus::Color(140, 30, 30, 35);
                Gdiplus::SolidBrush bgBrush(bgColor);
                g.FillRectangle(&bgBrush, bgRect);

                float fillW = bgRect.Width * vol;
                if (fillW > 0.0f) {
                    Gdiplus::Color fillColor = rVolume.hasColor ?
                        Gdiplus::Color(255, GetRValue(rVolume.color), GetGValue(rVolume.color), GetBValue(rVolume.color)) :
                        Gdiplus::Color(255, 0, 210, 255);
                    Gdiplus::SolidBrush fillBrush(fillColor);
                    g.FillRectangle(&fillBrush, bgRect.X, bgRect.Y, fillW, bgRect.Height);
                }

                Gdiplus::Pen borderPen(Gdiplus::Color(120, 255, 255, 255), 1.0f);
                g.DrawRectangle(&borderPen, bgRect.X, bgRect.Y, bgRect.Width, bgRect.Height);

                if (rVolume.showText && !stVolume.empty()) {
                    renderField(rVolume, FormatFieldText(rVolume, "", stVolume), Gdiplus::Color(255, 255, 255, 255));
                }
            } else if (!stVolume.empty()) {
                renderField(rVolume, FormatFieldText(rVolume, "VOL: ", stVolume), Gdiplus::Color(255, 200, 200, 200));
            }
        }

        LeaveCriticalSection(&g_popupCs);
    }

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, AC_SRC_ALPHA };
    POINT ptSrc = { 0, 0 };
    SIZE sz = { w, h };
    POINT ptDst = { x, y };

    UpdateLayeredWindow(g_hPopupWnd, hdcScreen, &ptDst, &sz, g_popupDIB.memDC, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, hdcScreen);
}

void TriggerPopup(PlayerState state, const std::string& station, const std::string& track,
                  const std::string& artist, const std::string& status,
                  const std::string& order, const std::string& volume)
{
    EnterCriticalSection(&g_popupCs);
    g_popupPlayerState = state;
    g_popupPlayOrder = g_playOrder;
    g_popupStationName = station;
    g_popupTrackTitle = track;
    g_popupArtistName = artist;
    g_popupStatus = status;
    g_popupOrder = order.empty() ? GetPlayOrderName(g_playOrder) : order;
    if (!volume.empty()) {
        g_popupVolume = volume;
    } else {
        char vBuf[32];
        snprintf(vBuf, sizeof(vBuf), "%d%%", (int)(g_volume * 100.0f + 0.5f));
        g_popupVolume = vBuf;
    }
    g_popupState = POPUP_HOLD;
    g_popupHoldStart = GetTickCount();
    LeaveCriticalSection(&g_popupCs);

    g_receivedUserInput = true;
    EnsurePopupParented(g_hPopupWnd);
    if (g_hPopupWnd && !IsWindowVisible(g_hPopupWnd)) {
        ShowWindow(g_hPopupWnd, SW_SHOWNOACTIVATE);
    }
}

void ChangeVolume(bool increase)
{
    g_receivedUserInput = true;
    float step = (g_skin.volumeStep > 0.0f) ? g_skin.volumeStep : 0.05f;
    if (increase) {
        g_volume += step;
        if (g_volume > 1.0f) g_volume = 1.0f;
    } else {
        g_volume -= step;
        if (g_volume < 0.0f) g_volume = 0.0f;
    }

    if (g_currentChannel && pfn_FMOD_Channel_SetVolume) {
        pfn_FMOD_Channel_SetVolume(g_currentChannel, g_volume);
    }

    SaveConfigVolume();

    int volPercent = (int)(g_volume * 100.0f + 0.5f);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d%%", volPercent);

    std::string stName = "RADIO";
    if (!g_stations.empty() && g_currentStation >= 0 && g_currentStation < (int)g_stations.size()) {
        stName = g_stations[g_currentStation].name;
    }

    TriggerPopup(STATE_VOLUME, stName, g_currentTrackTitle, g_currentArtist, "VOLUME", GetPlayOrderName(g_playOrder), buf);
    LogMsg("Volume adjusted to %d%% (step=%.2f, val=%.2f)", volPercent, step, g_volume);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
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
                    if (!g_isStopped) {
                        ChangeVolume(delta > 0);
                    } else {
                        ChangeInGameVolume(delta > 0);
                    }
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

DWORD WINAPI MouseHookThreadProc(LPVOID lpParam)
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

void RenderInGamePopup(int alpha)
{
    if (!g_hInGamePopupWnd) return;

    const int w = INGAME_POPUP_W;
    const int h = INGAME_POPUP_H;

    HDC hdcScreen = GetDC(NULL);
    g_inGamePopupDIB.Ensure(hdcScreen, w, h);
    if (!g_inGamePopupDIB.memDC || !g_inGamePopupDIB.pBits) {
        ReleaseDC(NULL, hdcScreen);
        return;
    }

    void* pBits = g_inGamePopupDIB.pBits;

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

    UpdateLayeredWindow(g_hInGamePopupWnd, NULL, NULL, &sizeWnd, g_inGamePopupDIB.memDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, hdcScreen);
}

void TriggerInGameMusicPopup(const std::string& trackName, bool force)
{
    if (!force && !g_cfgShowInGameMusicPopUp) return;

    if (g_hPopupWnd && IsWindowVisible(g_hPopupWnd)) {
        ShowWindow(g_hPopupWnd, SW_HIDE);
        EnterCriticalSection(&g_popupCs);
        g_popupState = POPUP_IDLE;
        LeaveCriticalSection(&g_popupCs);
    }

    EnterCriticalSection(&g_inGamePopupCs);
    g_inGamePopupTrackName = trackName;
    g_inGamePopupState = POPUP_HOLD;
    g_inGamePopupHoldStart = GetTickCount();
    LeaveCriticalSection(&g_inGamePopupCs);
    
    if (g_hInGamePopupWnd) {
        EnsurePopupParented(g_hInGamePopupWnd);
        HWND hGame = FindGameWindow();

        RECT rcClient = {0};
        int clientW = 0, clientH = 0;
        if (hGame && GetClientRect(hGame, &rcClient)) {
            clientW = rcClient.right - rcClient.left;
            clientH = rcClient.bottom - rcClient.top;
        } else {
            clientW = GetSystemMetrics(SM_CXSCREEN);
            clientH = GetSystemMetrics(SM_CYSCREEN);
        }

        const int popW = INGAME_POPUP_W;
        const int popH = INGAME_POPUP_H;
        int posX = (clientW > popW) ? (clientW - popW) / 2 : 0;
        int posY = (int)(clientH * 0.18f);

        SetWindowPos(g_hInGamePopupWnd, HWND_TOP, posX, posY, popW, popH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RenderInGamePopup(245);
    }
}

LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_LBUTTONDOWN:
    {
        s_isDragging = true;
        SetCapture(hwnd);
        GetCursorPos(&s_dragMouseStart);

        RECT rw;
        GetWindowRect(hwnd, &rw);
        if (g_popupX == -1) g_popupX = rw.left;
        if (g_popupY == -1) g_popupY = rw.top;

        s_dragWndStart.x = g_popupX;
        s_dragWndStart.y = g_popupY;

        EnterCriticalSection(&g_popupCs);
        g_popupState = POPUP_HOLD;
        g_popupHoldStart = GetTickCount();
        LeaveCriticalSection(&g_popupCs);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (s_isDragging) {
            POINT cur;
            GetCursorPos(&cur);
            g_popupX = s_dragWndStart.x + (cur.x - s_dragMouseStart.x);
            g_popupY = s_dragWndStart.y + (cur.y - s_dragMouseStart.y);

            HWND hParent = GetParent(hwnd);
            if (hParent) {
                RECT rc;
                GetClientRect(hParent, &rc);
                int maxW = rc.right - g_skin.width;
                int maxH = rc.bottom - g_skin.height;
                if (maxW > 0) {
                    if (g_popupX < 0) g_popupX = 0;
                    if (g_popupX > maxW) g_popupX = maxW;
                }
                if (maxH > 0) {
                    if (g_popupY < 0) g_popupY = 0;
                    if (g_popupY > maxH) g_popupY = maxH;
                }
            }

            SetWindowPos(hwnd, NULL, g_popupX, g_popupY, 0, 0,
                        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);

            EnterCriticalSection(&g_popupCs);
            g_popupHoldStart = GetTickCount();
            LeaveCriticalSection(&g_popupCs);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (s_isDragging) {
            s_isDragging = false;
            ReleaseCapture();
            SaveConfigPosition(g_popupX, g_popupY);

            EnterCriticalSection(&g_popupCs);
            g_popupHoldStart = GetTickCount();
            LeaveCriticalSection(&g_popupCs);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
    {
        if (s_isDragging) {
            s_isDragging = false;
            SaveConfigPosition(g_popupX, g_popupY);
        }
        return 0;
    }
    case WM_SETCURSOR:
    {
        SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        return TRUE;
    }
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

DWORD WINAPI PopupThreadProc(LPVOID lpParam)
{
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PopupWndProc;
    wc.hInstance = g_hInstance;
    wc.lpszClassName = "NFSW_Radio_Popup";
    RegisterClassExA(&wc);

    g_hPopupWnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        "NFSW Radio Popup",
        WS_POPUP,
        35, 35, 400, 150,
        NULL, NULL, g_hInstance, NULL
    );

    if (g_hPopupWnd) {
        ShowWindow(g_hPopupWnd, SW_HIDE);
    }

    WNDCLASSEXA wcInGame = {0};
    wcInGame.cbSize = sizeof(WNDCLASSEXA);
    wcInGame.style = CS_HREDRAW | CS_VREDRAW;
    wcInGame.lpfnWndProc = DefWindowProcA;
    wcInGame.hInstance = g_hInstance;
    wcInGame.lpszClassName = "NFSW_InGame_Music_Popup";
    RegisterClassExA(&wcInGame);

    g_hInGamePopupWnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        wcInGame.lpszClassName,
        "NFSW InGame Music Popup",
        WS_POPUP,
        0, 0, INGAME_POPUP_W, INGAME_POPUP_H,
        NULL, NULL, g_hInstance, NULL
    );

    if (g_hInGamePopupWnd) {
        ShowWindow(g_hInGamePopupWnd, SW_HIDE);
    }

    HWND hEarlyGame = FindGameWindow();
    if (hEarlyGame) {
        EnsurePopupParented(g_hPopupWnd);
        EnsurePopupParented(g_hInGamePopupWnd);
    }

    MSG msg;
    while (true) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        EnterCriticalSection(&g_popupCs);
        PopupState st = g_popupState;
        DWORD holdStart = g_popupHoldStart;
        DWORD fadeStart = g_popupFadeStart;
        LeaveCriticalSection(&g_popupCs);

        DWORD now = GetTickCount();

        auto updatePopupLifecycle = [](HWND hWnd, CRITICAL_SECTION& cs, PopupState& state,
                                       DWORD hold, DWORD& fade, DWORD currentTick,
                                       bool canFade, void (*renderFn)(int)) {
            if (state == POPUP_HOLD) {
                EnsurePopupParented(hWnd);
                renderFn(245);
                if (canFade && (currentTick - hold >= 3000)) {
                    EnterCriticalSection(&cs);
                    state = POPUP_FADE;
                    fade = currentTick;
                    LeaveCriticalSection(&cs);
                }
            } else if (state == POPUP_FADE) {
                DWORD fadeElapsed = currentTick - fade;
                if (fadeElapsed < 250) {
                    float t = (float)fadeElapsed / 250.0f;
                    int alpha = (int)(245.0f * (1.0f - t));
                    if (alpha < 0) alpha = 0;
                    renderFn(alpha);
                } else {
                    EnterCriticalSection(&cs);
                    if (state == POPUP_FADE) {
                        state = POPUP_IDLE;
                        ShowWindow(hWnd, SW_HIDE);
                    }
                    LeaveCriticalSection(&cs);
                }
            }
        };

        updatePopupLifecycle(g_hPopupWnd, g_popupCs, g_popupState, holdStart, g_popupFadeStart, now, !s_isDragging, RenderPopup);

        EnterCriticalSection(&g_inGamePopupCs);
        PopupState igSt = g_inGamePopupState;
        DWORD igHoldStart = g_inGamePopupHoldStart;
        DWORD igFadeStart = g_inGamePopupFadeStart;
        LeaveCriticalSection(&g_inGamePopupCs);

        updatePopupLifecycle(g_hInGamePopupWnd, g_inGamePopupCs, g_inGamePopupState, igHoldStart, g_inGamePopupFadeStart, now, true, RenderInGamePopup);

        Sleep(15);
    }

    return 0;
}
