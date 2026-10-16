#include "RadioSkin.h"
#include "RadioConfig.h"
#include "RadioPopup.h"

void ReadCoord(const std::map<std::string, std::string>& kv, const std::string& name, RectCoords& rc,
               int defaultX, int defaultY, int defaultW, int defaultH, int defaultFontSize, COLORREF defaultColor,
               bool defaultDrawBg, COLORREF defaultBgColor, bool defaultHasBgColor)
{
    rc.x = defaultX;
    rc.y = defaultY;
    rc.width = defaultW;
    rc.height = defaultH;
    rc.fontSize = defaultFontSize;
    rc.color = defaultColor;
    rc.hasColor = true;
    rc.enabled = true;
    rc.drawBackground = defaultDrawBg;
    rc.bgColor = defaultBgColor;
    rc.hasBgColor = defaultHasBgColor;

    auto getVal = [&](const std::string& sub1, const std::string& sub2) -> std::string {
        std::string k1 = name + "." + sub1;
        auto it = kv.find(k1);
        if (it != kv.end()) return it->second;
        std::string k2 = name + "_" + sub1;
        it = kv.find(k2);
        if (it != kv.end()) return it->second;
        if (!sub2.empty()) {
            std::string k3 = name + "." + sub2;
            it = kv.find(k3);
            if (it != kv.end()) return it->second;
            std::string k4 = name + "_" + sub2;
            it = kv.find(k4);
            if (it != kv.end()) return it->second;
        }
        return "";
    };

    auto hasVal = [&](const std::string& sub1, const std::string& sub2, std::string& outVal) -> bool {
        std::string k1 = name + "." + sub1;
        auto it = kv.find(k1);
        if (it != kv.end()) { outVal = it->second; return true; }
        std::string k2 = name + "_" + sub1;
        it = kv.find(k2);
        if (it != kv.end()) { outVal = it->second; return true; }
        if (!sub2.empty()) {
            std::string k3 = name + "." + sub2;
            it = kv.find(k3);
            if (it != kv.end()) { outVal = it->second; return true; }
            std::string k4 = name + "_" + sub2;
            it = kv.find(k4);
            if (it != kv.end()) { outVal = it->second; return true; }
        }
        return false;
    };

    std::string sx = getVal("x", "");
    if (!sx.empty()) rc.x = atoi(sx.c_str());

    std::string sy = getVal("y", "");
    if (!sy.empty()) rc.y = atoi(sy.c_str());

    std::string sw = getVal("width", "w");
    if (!sw.empty()) rc.width = atoi(sw.c_str());

    std::string sh = getVal("height", "h");
    if (!sh.empty()) rc.height = atoi(sh.c_str());

    std::string sfs = getVal("font_size", "fontsize");
    if (!sfs.empty()) rc.fontSize = atoi(sfs.c_str());

    std::string sen = getVal("enabled", "");
    if (!sen.empty()) {
        std::transform(sen.begin(), sen.end(), sen.begin(), ::tolower);
        if (sen == "false" || sen == "0") rc.enabled = false;
    }

    std::string scol = getVal("color", "");
    if (!scol.empty()) {
        if (scol[0] == '#') scol = scol.substr(1);
        else if (scol.size() > 2 && scol[0] == '0' && (scol[1] == 'x' || scol[1] == 'X')) scol = scol.substr(2);
        unsigned int c = 0;
        if (sscanf(scol.c_str(), "%x", &c) == 1) {
            rc.color = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            rc.hasColor = true;
        }
    }

    std::string sbg;
    if (hasVal("background", "bg", sbg)) {
        std::string sbgLower = sbg;
        std::transform(sbgLower.begin(), sbgLower.end(), sbgLower.begin(), ::tolower);
        if (sbgLower == "true" || sbgLower == "1" || sbgLower == "yes") {
            rc.drawBackground = true;
        } else if (sbgLower == "false" || sbgLower == "0" || sbgLower == "no") {
            rc.drawBackground = false;
        } else if (!sbg.empty()) {
            std::string scolor = sbg;
            if (scolor[0] == '#') scolor = scolor.substr(1);
            else if (scolor.size() > 2 && scolor[0] == '0' && (scolor[1] == 'x' || scolor[1] == 'X')) scolor = scolor.substr(2);
            unsigned int c = 0;
            if (sscanf(scolor.c_str(), "%x", &c) == 1) {
                rc.bgColor = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
                rc.hasBgColor = true;
                rc.drawBackground = true;
            }
        }
    }

    if (hasVal("bg_color", "bgcolor", sbg) || hasVal("background_color", "bg_colour", sbg)) {
        if (!sbg.empty()) {
            std::string sbgLower = sbg;
            std::transform(sbgLower.begin(), sbgLower.end(), sbgLower.begin(), ::tolower);
            if (sbgLower == "true" || sbgLower == "1" || sbgLower == "yes") {
                rc.drawBackground = true;
            } else if (sbgLower == "false" || sbgLower == "0" || sbgLower == "no") {
                rc.drawBackground = false;
            } else {
                std::string scolor = sbg;
                if (scolor[0] == '#') scolor = scolor.substr(1);
                else if (scolor.size() > 2 && scolor[0] == '0' && (scolor[1] == 'x' || scolor[1] == 'X')) scolor = scolor.substr(2);
                unsigned int c = 0;
                if (sscanf(scolor.c_str(), "%x", &c) == 1) {
                    rc.bgColor = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
                    rc.hasBgColor = true;
                    rc.drawBackground = true;
                }
            }
        }
    }

    std::string sfmt;
    if (hasVal("format", "fmt", sfmt)) {
        rc.format = sfmt;
        rc.hasFormat = true;
    }

    std::string spre;
    if (hasVal("prefix", "", spre)) {
        rc.prefix = spre;
        rc.hasPrefix = true;
    }

    std::string stype;
    if (hasVal("type", "mode", stype)) {
        std::transform(stype.begin(), stype.end(), stype.begin(), ::tolower);
        rc.type = stype;
    }

    std::string salign;
    if (hasVal("align", "alignment", salign)) {
        std::transform(salign.begin(), salign.end(), salign.begin(), ::tolower);
        rc.align = salign;
    }

    std::string stxt;
    if (hasVal("show_text", "showtext", stxt)) {
        std::transform(stxt.begin(), stxt.end(), stxt.begin(), ::tolower);
        if (stxt == "true" || stxt == "1" || stxt == "yes") rc.showText = true;
    }
}

void ReadCoverCoord(const std::map<std::string, std::string>& kv, const std::string& name, CoverCoords& rc,
                    int defaultX, int defaultY, int defaultW, int defaultH)
{
    ReadCoord(kv, name, rc, defaultX, defaultY, defaultW, defaultH, 0, RGB(255, 255, 255));
    rc.zOrder = "below";
    rc.keepAspect = true;

    auto itZ = kv.find(name + ".z_order");
    if (itZ == kv.end()) itZ = kv.find(name + "_z_order");
    if (itZ == kv.end()) itZ = kv.find(name + ".z");
    if (itZ == kv.end()) itZ = kv.find(name + ".layer");
    if (itZ != kv.end()) {
        std::string z = itZ->second;
        std::transform(z.begin(), z.end(), z.begin(), ::tolower);
        if (z == "above" || z == "top" || z == "over" || z == "1") {
            rc.zOrder = "above";
        } else {
            rc.zOrder = "below";
        }
    }

    auto itKa = kv.find(name + ".keep_aspect");
    if (itKa == kv.end()) itKa = kv.find(name + "_keep_aspect");
    if (itKa != kv.end()) {
        std::string ka = itKa->second;
        std::transform(ka.begin(), ka.end(), ka.begin(), ::tolower);
        if (ka == "false" || ka == "0" || ka == "no") rc.keepAspect = false;
        else rc.keepAspect = true;
    }
}

std::string FormatFieldText(const RectCoords& rc, const std::string& defaultPrefix, const std::string& val)
{
    if (rc.hasFormat) {
        std::string fmt = rc.format;
        size_t p = fmt.find("%s");
        if (p != std::string::npos) {
            return fmt.substr(0, p) + val + fmt.substr(p + 2);
        }
        p = fmt.find("{}");
        if (p != std::string::npos) {
            return fmt.substr(0, p) + val + fmt.substr(p + 2);
        }
        return fmt + val;
    }
    if (rc.hasPrefix) {
        return rc.prefix + val;
    }
    return defaultPrefix + val;
}

Gdiplus::Bitmap* LoadOrderIcon(const std::string& iconsDir, PlayOrder order)
{
    std::vector<std::string> candidates;
    switch (order) {
    case ORDER_DEFAULT:
        candidates = { "default", "default_icon", "order_default", "order", "normal" };
        break;
    case ORDER_REPEAT_PLAYLIST:
        candidates = { "repeat_playlist", "repeat_playlist_icon", "repeatplaylist", "repeat_all", "repeat" };
        break;
    case ORDER_REPEAT_TRACK:
        candidates = { "repeat_track", "repeat_track_icon", "repeattrack", "repeat_one", "repeat_1", "repeat1" };
        break;
    case ORDER_SHUFFLE:
        candidates = { "shuffle", "shuffle_icon", "random" };
        break;
    default:
        break;
    }

    static const std::vector<std::string> exts = { ".png", ".jpg", ".jpeg", ".bmp", ".ico" };

    for (const auto& name : candidates) {
        for (const auto& ext : exts) {
            std::string path = iconsDir + "\\" + name + ext;
            Gdiplus::Bitmap* bmp = LoadBitmapFromFile(path);
            if (bmp) return bmp;
        }
    }

    auto normalize = [](const std::string& s) -> std::string {
        std::string res;
        for (char c : s) {
            if (isalnum((unsigned char)c)) res += (char)tolower((unsigned char)c);
        }
        return res;
    };

    std::vector<std::string> normCandidates;
    for (const auto& c : candidates) normCandidates.push_back(normalize(c));

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((iconsDir + "\\*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::string fname = fd.cFileName;
                size_t dot = fname.find_last_of('.');
                if (dot != std::string::npos) {
                    std::string normBase = normalize(fname.substr(0, dot));
                    for (const auto& nc : normCandidates) {
                        if (normBase == nc) {
                            std::string fullPath = iconsDir + "\\" + fname;
                            Gdiplus::Bitmap* bmp = LoadBitmapFromFile(fullPath);
                            if (bmp) {
                                FindClose(hFind);
                                return bmp;
                            }
                        }
                    }
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    return NULL;
}

void DrawIconImage(Gdiplus::Graphics& g, Gdiplus::Bitmap* pBmp, const RectCoords& cfg)
{
    if (!pBmp || !cfg.enabled || cfg.width <= 0 || cfg.height <= 0) return;
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    float bmpW = (float)pBmp->GetWidth();
    float bmpH = (float)pBmp->GetHeight();
    if (bmpW <= 0 || bmpH <= 0) return;

    float scale = std::min((float)cfg.width / bmpW, (float)cfg.height / bmpH);
    float destW = bmpW * scale;
    float destH = bmpH * scale;

    float destX = (float)cfg.x;
    if (cfg.align == "center") {
        destX = cfg.x + (cfg.width - destW) / 2.0f;
    } else if (cfg.align == "right" || cfg.align == "far") {
        destX = cfg.x + (cfg.width - destW);
    }
    float destY = cfg.y + (cfg.height - destH) / 2.0f;

    g.DrawImage(pBmp, destX, destY, destW, destH);
}

void DrawCoverImage(Gdiplus::Graphics& g, Gdiplus::Bitmap* pBmp, const CoverCoords& cfg)
{
    if (!pBmp || !cfg.enabled || cfg.width <= 0 || cfg.height <= 0) return;
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    if (cfg.keepAspect) {
        float bmpW = (float)pBmp->GetWidth();
        float bmpH = (float)pBmp->GetHeight();
        if (bmpW <= 0 || bmpH <= 0) return;

        float scale = std::min((float)cfg.width / bmpW, (float)cfg.height / bmpH);
        float destW = bmpW * scale;
        float destH = bmpH * scale;
        float destX = cfg.x + (cfg.width - destW) / 2.0f;
        float destY = cfg.y + (cfg.height - destH) / 2.0f;

        g.DrawImage(pBmp, destX, destY, destW, destH);
    } else {
        g.DrawImage(pBmp, (Gdiplus::REAL)cfg.x, (Gdiplus::REAL)cfg.y, (Gdiplus::REAL)cfg.width, (Gdiplus::REAL)cfg.height);
    }
}

void LoadSkin(const std::string& requestedSkinName)
{
    std::string skinsDir = GetSkinsDir();
    std::string skinName = requestedSkinName.empty() ? "Default" : requestedSkinName;
    std::string skinFolder = skinsDir + "\\" + skinName;

    if (_stricmp(skinName.c_str(), "Transparent") == 0) {
        skinName = "Transparent";
        skinFolder = skinsDir + "\\Transparent";
        CreateDirectoryA(skinFolder.c_str(), NULL);
    } else if (_stricmp(skinName.c_str(), "Default") == 0) {
        skinName = "Default";
        skinFolder = skinsDir + "\\Default";
        CreateDirectoryA(skinFolder.c_str(), NULL);
    } else {
        DWORD attr = GetFileAttributesA(skinFolder.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            WIN32_FIND_DATAA fd;
            HANDLE hFind = FindFirstFileA((skinsDir + "\\*").c_str(), &fd);
            bool found = false;
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        std::string dirName = fd.cFileName;
                        if (dirName != "." && dirName != "..") {
                            std::string candJson = skinsDir + "\\" + dirName + "\\" + dirName + ".json";
                            if (GetFileAttributesA(candJson.c_str()) != INVALID_FILE_ATTRIBUTES) {
                                skinName = dirName;
                                skinFolder = skinsDir + "\\" + skinName;
                                found = true;
                                break;
                            }
                        }
                    }
                } while (FindNextFileA(hFind, &fd));
                FindClose(hFind);
            }
            if (!found) {
                skinFolder = skinsDir + "\\Default";
                CreateDirectoryA(skinFolder.c_str(), NULL);
                skinName = "Default";
            }
        }
    }

    if (g_pSkinBitmap) {
        delete g_pSkinBitmap;
        g_pSkinBitmap = NULL;
    }
    for (int i = 0; i < STATE_COUNT; ++i) {
        if (g_skin.states[i].pStateBitmap) {
            delete g_skin.states[i].pStateBitmap;
            g_skin.states[i].pStateBitmap = NULL;
        }
    }
    if (g_skin.fontLoaded && !g_skin.loadedFontPath.empty()) {
        RemoveFontResourceExA(g_skin.loadedFontPath.c_str(), FR_PRIVATE, 0);
        g_skin.fontLoaded = false;
        g_skin.loadedFontPath = "";
    }

    g_skin = SkinConfig();
    g_skin.name = skinName;
    g_skin.folderPath = skinFolder;

    std::string jsonPath = skinFolder + "\\" + skinName + ".json";
    FILE* f = fopen(jsonPath.c_str(), "rb");
    std::string jsonContent = "";
    if (f) {
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len > 0) {
            jsonContent.resize(len);
            fread(&jsonContent[0], 1, len, f);
        }
        fclose(f);
    } else {
        jsonContent =
            "{\n"
            "  \"image\": \"" + skinName + ".png\",\n"
            "  \"font_name\": \"Segoe UI\",\n"
            "  \"font_size\": 15,\n"
            "  \"volume_step\": \"5%\",\n"
            "  \"width\": 400,\n"
            "  \"height\": 150,\n"
            "  \"cover\": { \"x\": 16, \"y\": 16, \"width\": 118, \"height\": 118, \"z_order\": \"below\", \"keep_aspect\": true },\n"
            "  \"station\": { \"x\": 146, \"y\": 14, \"width\": 240, \"height\": 24, \"font_size\": 15, \"color\": \"#00D2FF\" },\n"
            "  \"artist\": { \"x\": 146, \"y\": 38, \"width\": 240, \"height\": 24, \"font_size\": 15, \"color\": \"#00D2FF\" },\n"
            "  \"track\": { \"x\": 146, \"y\": 62, \"width\": 240, \"height\": 28, \"font_size\": 17, \"color\": \"#FFFFFF\" },\n"
            "  \"status\": { \"x\": 146, \"y\": 90, \"width\": 160, \"height\": 22, \"font_size\": 14, \"color\": \"#FFD700\" },\n"
            "  \"order\": { \"x\": 146, \"y\": 116, \"width\": 160, \"height\": 22, \"font_size\": 14, \"color\": \"#00D2FF\" },\n"
            "  \"volume\": { \"x\": 295, \"y\": 116, \"width\": 90, \"height\": 22, \"font_size\": 16, \"color\": \"#FFFFFF\" },\n"
            "  \"states\": {\n"
            "    \"playing\": { \"show\": [\"station\", \"track\", \"status\", \"order\", \"cover\"] },\n"
            "    \"paused\":  { \"show\": [\"station\", \"track\", \"status\", \"cover\"] },\n"
            "    \"station\": { \"show\": [\"station\", \"track\", \"order\", \"cover\"] },\n"
            "    \"order\":   { \"show\": [\"station\", \"track\", \"order\", \"cover\"] },\n"
            "    \"volume\":  { \"show\": [\"station\", \"track\", \"status\", \"order\", \"cover\", \"volume\"] }\n"
            "  }\n"
            "}\n";
        FILE* outF = fopen(jsonPath.c_str(), "wb");
        if (outF) {
            fwrite(jsonContent.c_str(), 1, jsonContent.size(), outF);
            fclose(outF);
            LogMsg("Created skin JSON at: %s", jsonPath.c_str());
        }
    }

    std::map<std::string, std::string> kv;
    size_t parsePos = 0;
    ParseJsonObject(jsonContent, parsePos, "", kv);

    if (kv.find("image") != kv.end()) g_skin.imageName = kv["image"];
    else g_skin.imageName = skinName + ".png";

    if (kv.find("font_name") != kv.end()) g_skin.fontName = kv["font_name"];
    if (kv.find("font_size") != kv.end()) g_skin.fontSize = atoi(kv["font_size"].c_str());
    if (g_skin.fontSize <= 0) g_skin.fontSize = 14;

    if (kv.find("volume_step") != kv.end()) {
        std::string vstep = kv["volume_step"];
        float val = (float)atof(vstep.c_str());
        if (vstep.find('%') != std::string::npos || val >= 1.0f) {
            g_skin.volumeStep = val / 100.0f;
        } else if (val > 0.0f) {
            g_skin.volumeStep = val;
        }
    }
    if (g_skin.volumeStep <= 0.001f) g_skin.volumeStep = 0.05f;

    if (kv.find("width") != kv.end()) g_skin.width = atoi(kv["width"].c_str());
    if (kv.find("height") != kv.end()) g_skin.height = atoi(kv["height"].c_str());

    // Base coordinates
    ReadCoverCoord(kv, "cover", g_skin.coverRect, 16, 16, 118, 118);
    ReadCoord(kv, "station", g_skin.stationRect, 146, 14, 240, 24, g_skin.fontSize, RGB(0, 210, 255));
    ReadCoord(kv, "track",   g_skin.trackRect,   146, 42, 240, 28, g_skin.fontSize + 2, RGB(255, 255, 255));

    bool hasArtistDef = (kv.find("artist.x") != kv.end() || kv.find("artist_x") != kv.end() ||
                         kv.find("artist.width") != kv.end() || kv.find("artist.y") != kv.end());
    ReadCoord(kv, "artist",  g_skin.artistRect,  146, 38, 240, 24, g_skin.fontSize, RGB(0, 210, 255));
    if (!hasArtistDef) {
        g_skin.artistRect.enabled = false;
    }

    ReadCoord(kv, "status",  g_skin.statusRect,  146, 74, 120, 22, g_skin.fontSize, RGB(255, 215, 0));
    ReadCoord(kv, "order",   g_skin.orderRect,   146, 102, 140, 22, g_skin.fontSize, RGB(0, 210, 255));
    ReadCoord(kv, "volume",  g_skin.volumeRect,  295, 102, 90, 22, g_skin.fontSize, RGB(255, 255, 255));

    auto parseStateConfig = [&](PlayerState stEnum, const std::string& stKey) {
        StateDisplayConfig& sc = g_skin.states[stEnum];
        sc = StateDisplayConfig();

        std::string prefix = "states." + stKey + ".";
        bool hasStateConfig = false;
        for (const auto& pair : kv) {
            if (pair.first.compare(0, prefix.size(), prefix) == 0) {
                hasStateConfig = true;
                break;
            }
        }

        if (!hasStateConfig) {
            sc.configured = false;
            if (stEnum == STATE_PLAYING) {
                sc.showStation = true; sc.showTrack = true; sc.showStatus = true; sc.showOrder = true; sc.showCover = true; sc.showVolume = false;
                sc.showArtist = hasArtistDef;
            } else if (stEnum == STATE_PAUSED) {
                sc.showStation = true; sc.showTrack = true; sc.showStatus = true; sc.showOrder = false; sc.showCover = true; sc.showVolume = false;
                sc.showArtist = hasArtistDef;
            } else if (stEnum == STATE_STATION) {
                sc.showStation = true; sc.showTrack = true; sc.showStatus = false; sc.showOrder = true; sc.showCover = true; sc.showVolume = false;
                sc.showArtist = hasArtistDef;
            } else if (stEnum == STATE_ORDER) {
                sc.showStation = true; sc.showTrack = true; sc.showStatus = false; sc.showOrder = true; sc.showCover = true; sc.showVolume = false;
                sc.showArtist = hasArtistDef;
            } else if (stEnum == STATE_VOLUME) {
                sc.showStation = true; sc.showTrack = true; sc.showStatus = true; sc.showOrder = true; sc.showCover = true; sc.showVolume = true; sc.showArtist = hasArtistDef;
            }
        } else {
            sc.configured = true;
            auto itShow = kv.find(prefix + "show");
            if (itShow != kv.end()) {
                std::string showStr = itShow->second;
                std::transform(showStr.begin(), showStr.end(), showStr.begin(), ::tolower);
                sc.showStation = (showStr.find("station") != std::string::npos);
                sc.showTrack   = (showStr.find("track")   != std::string::npos);
                sc.showArtist  = (showStr.find("artist")  != std::string::npos);
                sc.showStatus  = (showStr.find("status")  != std::string::npos);
                sc.showOrder   = (showStr.find("order")   != std::string::npos);
                sc.showVolume  = (showStr.find("volume")  != std::string::npos);
                sc.showCover   = (showStr.find("cover")   != std::string::npos);
            } else {
                if (stEnum == STATE_PLAYING) {
                    sc.showStation = true; sc.showTrack = true; sc.showStatus = true; sc.showOrder = true; sc.showCover = true;
                    sc.showArtist = hasArtistDef;
                } else if (stEnum == STATE_PAUSED) {
                    sc.showStation = true; sc.showTrack = true; sc.showStatus = true; sc.showCover = true;
                    sc.showArtist = hasArtistDef;
                } else if (stEnum == STATE_STATION) {
                    sc.showStation = true; sc.showTrack = true; sc.showOrder = true; sc.showCover = true;
                    sc.showArtist = hasArtistDef;
                } else if (stEnum == STATE_ORDER) {
                    sc.showStation = true; sc.showTrack = true; sc.showOrder = true; sc.showCover = true;
                    sc.showArtist = hasArtistDef;
                } else if (stEnum == STATE_VOLUME) {
                    sc.showStation = true; sc.showTrack = true; sc.showStatus = true;
                    sc.showOrder = true; sc.showCover = true; sc.showVolume = true;
                    sc.showArtist = hasArtistDef;
                }
            }

            auto itImg = kv.find(prefix + "image");
            if (itImg != kv.end()) sc.imageName = itImg->second;

            if (kv.find(prefix + "station.x") != kv.end() || kv.find(prefix + "station.y") != kv.end()) {
                sc.hasStationRect = true;
                ReadCoord(kv, prefix + "station", sc.stationRect, g_skin.stationRect.x, g_skin.stationRect.y, g_skin.stationRect.width, g_skin.stationRect.height, g_skin.stationRect.fontSize, g_skin.stationRect.color);
            }
            if (kv.find(prefix + "track.x") != kv.end() || kv.find(prefix + "track.y") != kv.end()) {
                sc.hasTrackRect = true;
                ReadCoord(kv, prefix + "track", sc.trackRect, g_skin.trackRect.x, g_skin.trackRect.y, g_skin.trackRect.width, g_skin.trackRect.height, g_skin.trackRect.fontSize, g_skin.trackRect.color);
            }
            if (kv.find(prefix + "artist.x") != kv.end() || kv.find(prefix + "artist.y") != kv.end()) {
                sc.hasArtistRect = true;
                ReadCoord(kv, prefix + "artist", sc.artistRect, g_skin.artistRect.x, g_skin.artistRect.y, g_skin.artistRect.width, g_skin.artistRect.height, g_skin.artistRect.fontSize, g_skin.artistRect.color);
            }
            if (kv.find(prefix + "status.x") != kv.end() || kv.find(prefix + "status.y") != kv.end()) {
                sc.hasStatusRect = true;
                ReadCoord(kv, prefix + "status", sc.statusRect, g_skin.statusRect.x, g_skin.statusRect.y, g_skin.statusRect.width, g_skin.statusRect.height, g_skin.statusRect.fontSize, g_skin.statusRect.color);
            }
            if (kv.find(prefix + "order.x") != kv.end() || kv.find(prefix + "order.y") != kv.end()) {
                sc.hasOrderRect = true;
                ReadCoord(kv, prefix + "order", sc.orderRect, g_skin.orderRect.x, g_skin.orderRect.y, g_skin.orderRect.width, g_skin.orderRect.height, g_skin.orderRect.fontSize, g_skin.orderRect.color);
            }
            if (kv.find(prefix + "volume.x") != kv.end() || kv.find(prefix + "volume.y") != kv.end()) {
                sc.hasVolumeRect = true;
                ReadCoord(kv, prefix + "volume", sc.volumeRect, g_skin.volumeRect.x, g_skin.volumeRect.y, g_skin.volumeRect.width, g_skin.volumeRect.height, g_skin.volumeRect.fontSize, g_skin.volumeRect.color);
            }
            if (kv.find(prefix + "cover.x") != kv.end() || kv.find(prefix + "cover.y") != kv.end()) {
                sc.hasCoverRect = true;
                ReadCoverCoord(kv, prefix + "cover", sc.coverRect, g_skin.coverRect.x, g_skin.coverRect.y, g_skin.coverRect.width, g_skin.coverRect.height);
            }
        }

        std::string candStateImg = "";
        if (!sc.imageName.empty()) {
            candStateImg = skinFolder + "\\" + sc.imageName;
        } else {
            std::string cand1 = skinFolder + "\\" + skinName + "_" + stKey + ".png";
            std::string cand2 = skinFolder + "\\" + stKey + ".png";
            if (GetFileAttributesA(cand1.c_str()) != INVALID_FILE_ATTRIBUTES) candStateImg = cand1;
            else if (GetFileAttributesA(cand2.c_str()) != INVALID_FILE_ATTRIBUTES) candStateImg = cand2;
        }
        if (!candStateImg.empty() && GetFileAttributesA(candStateImg.c_str()) != INVALID_FILE_ATTRIBUTES) {
            sc.pStateBitmap = LoadBitmapFromFile(candStateImg);
            if (sc.pStateBitmap) {
                LogMsg("Loaded state '%s' graphic: %s", stKey.c_str(), candStateImg.c_str());
            }
        }
    };

    parseStateConfig(STATE_PLAYING, "playing");
    parseStateConfig(STATE_PAUSED, "paused");
    parseStateConfig(STATE_STATION, "station");
    parseStateConfig(STATE_ORDER, "order");
    parseStateConfig(STATE_VOLUME, "volume");

    // Custom Font loading
    if (!g_skin.fontName.empty()) {
        std::string baseFontName = g_skin.fontName;
        std::vector<std::string> fontCandidates = {
            skinFolder + "\\" + baseFontName,
            skinFolder + "\\" + baseFontName + ".ttf",
            skinFolder + "\\" + baseFontName + ".otf"
        };
        for (const auto& fc : fontCandidates) {
            if (GetFileAttributesA(fc.c_str()) != INVALID_FILE_ATTRIBUTES) {
                if (AddFontResourceExA(fc.c_str(), FR_PRIVATE, 0) > 0) {
                    g_skin.loadedFontPath = fc;
                    g_skin.fontLoaded = true;
                    LogMsg("Loaded skin private font: %s", fc.c_str());
                    break;
                }
            }
        }
        size_t dot = baseFontName.find_last_of('.');
        if (dot != std::string::npos) {
            std::string ext = baseFontName.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ttf" || ext == ".otf") {
                g_skin.fontName = baseFontName.substr(0, dot);
            }
        }
    }

    std::string iconsDir = skinFolder + "\\icons";
    DWORD attrIcons = GetFileAttributesA(iconsDir.c_str());
    if (attrIcons != INVALID_FILE_ATTRIBUTES && (attrIcons & FILE_ATTRIBUTE_DIRECTORY)) {
        for (int i = 0; i < ORDER_COUNT; ++i) {
            g_skin.orderIcons[i] = LoadOrderIcon(iconsDir, (PlayOrder)i);
            if (g_skin.orderIcons[i]) {
                LogMsg("Loaded order icon for '%s'", GetPlayOrderName((PlayOrder)i));
            }
        }
    }

    std::string pngPath = skinFolder + "\\" + skinName + ".png";
    if (GetFileAttributesA(pngPath.c_str()) == INVALID_FILE_ATTRIBUTES && !g_skin.imageName.empty()) {
        pngPath = skinFolder + "\\" + g_skin.imageName;
    }

    if (GetFileAttributesA(pngPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        g_pSkinBitmap = LoadBitmapFromFile(pngPath);
        if (g_pSkinBitmap) {
            LogMsg("Loaded primary skin graphic: %s (%ux%u)", pngPath.c_str(), g_pSkinBitmap->GetWidth(), g_pSkinBitmap->GetHeight());
            if (kv.find("width") == kv.end()) g_skin.width = (int)g_pSkinBitmap->GetWidth();
            if (kv.find("height") == kv.end()) g_skin.height = (int)g_pSkinBitmap->GetHeight();
        }
    }

    if (g_skin.width <= 0) g_skin.width = 400;
    if (g_skin.height <= 0) g_skin.height = 150;

    LogMsg("Skin '%s' initialized (Width=%d, Height=%d, VolumeStep=%.2f, Font='%s')",
           skinName.c_str(), g_skin.width, g_skin.height, g_skin.volumeStep, g_skin.fontName.c_str());
}

std::vector<std::string> GetSkinList()
{
    std::vector<std::string> list;
    list.push_back("Default");
    list.push_back("Transparent");

    std::vector<std::string> customSkins;
    std::string skinsDir = GetSkinsDir();
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((skinsDir + "\\*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::string dirName = fd.cFileName;
                if (dirName != "." && dirName != "..") {
                    if (_stricmp(dirName.c_str(), "Default") != 0 &&
                        _stricmp(dirName.c_str(), "Transparent") != 0) {
                        customSkins.push_back(dirName);
                    }
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    std::sort(customSkins.begin(), customSkins.end());
    list.insert(list.end(), customSkins.begin(), customSkins.end());
    return list;
}

void SwitchToNextSkin()
{
    std::vector<std::string> skins = GetSkinList();
    if (skins.empty()) skins.push_back("Default");

    int currentIndex = -1;
    for (size_t i = 0; i < skins.size(); ++i) {
        if (_stricmp(skins[i].c_str(), g_skin.name.c_str()) == 0) {
            currentIndex = (int)i;
            break;
        }
    }

    int nextIndex = (currentIndex + 1) % (int)skins.size();
    std::string nextSkin = skins[nextIndex];

    LogMsg("Skin changed from '%s' to '%s'", g_skin.name.c_str(), nextSkin.c_str());
    LoadSkin(nextSkin);
    SaveConfigSkin(nextSkin);

    std::string stName = (!g_stations.empty() && g_currentStation >= 0 && g_currentStation < (int)g_stations.size())
        ? g_stations[g_currentStation].name : "RADIO";

    TriggerPopup(g_isStopped ? STATE_PAUSED : STATE_PLAYING, stName, g_currentTrackTitle, g_currentArtist, g_skin.name);
}
