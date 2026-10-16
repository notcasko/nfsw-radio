#pragma once

#include "RadioCommon.h"

// Embedded Audio Cover and Metadata Extraction
Gdiplus::Bitmap* ExtractTrackCover(const std::string& filePath);
void ExtractTrackMetadata(const std::string& filePath, std::string& outArtist, std::string& outTitle);
Gdiplus::Bitmap* LoadStationCover(const std::string& folderPath);
void UpdateCurrentCover(const std::string& trackPath, const std::string& stationFolder);
