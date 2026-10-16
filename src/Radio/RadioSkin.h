#pragma once

#include "RadioCommon.h"

// Skin Coordinates and Formatting Readers
void ReadCoord(const std::map<std::string, std::string>& kv, const std::string& name, RectCoords& rc,
               int defaultX, int defaultY, int defaultW, int defaultH, int defaultFontSize, COLORREF defaultColor,
               bool defaultDrawBg = false, COLORREF defaultBgColor = RGB(35, 35, 40), bool defaultHasBgColor = false);

void ReadCoverCoord(const std::map<std::string, std::string>& kv, const std::string& name, CoverCoords& rc,
                    int defaultX, int defaultY, int defaultW, int defaultH);

std::string FormatFieldText(const RectCoords& rc, const std::string& defaultPrefix, const std::string& val);

// Skin Graphic and Icon Rendering Helpers
Gdiplus::Bitmap* LoadOrderIcon(const std::string& iconsDir, PlayOrder order);
void DrawIconImage(Gdiplus::Graphics& g, Gdiplus::Bitmap* pBmp, const RectCoords& cfg);
void DrawCoverImage(Gdiplus::Graphics& g, Gdiplus::Bitmap* pBmp, const CoverCoords& cfg);

// Skin Management and Switching
void LoadSkin(const std::string& requestedSkinName);
std::vector<std::string> GetSkinList();
void SwitchToNextSkin();
