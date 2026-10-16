#include "MetadataExtractor.h"

// Helper to decode ID3 Text Frames
static std::string ParseId3TextFrame(const unsigned char* data, size_t size)
{
    if (size <= 1) return "";
    unsigned char encoding = data[0];
    const unsigned char* strData = data + 1;
    size_t strLen = size - 1;

    if (encoding == 0 || encoding == 3) {
        while (strLen > 0 && strData[strLen - 1] == 0) strLen--;
        std::string raw((const char*)strData, strLen);
        if (encoding == 3) return raw; // Already UTF-8

        // Encoding 0: ISO-8859-1 or frequently Shift-JIS in Japanese tracks
        int uLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.c_str(), (int)raw.size(), NULL, 0);
        if (uLen > 0) return raw;

        int sjisLen = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, raw.c_str(), (int)raw.size(), NULL, 0);
        if (sjisLen > 0) {
            std::wstring w(sjisLen, 0);
            MultiByteToWideChar(932, 0, raw.c_str(), (int)raw.size(), &w[0], sjisLen);
            return WideToUtf8(w);
        }

        int acpLen = MultiByteToWideChar(CP_ACP, 0, raw.c_str(), (int)raw.size(), NULL, 0);
        if (acpLen > 0) {
            std::wstring w(acpLen, 0);
            MultiByteToWideChar(CP_ACP, 0, raw.c_str(), (int)raw.size(), &w[0], acpLen);
            return WideToUtf8(w);
        }
        return raw;
    } else if (encoding == 1 || encoding == 2) {
        if (strLen < 2) return "";
        bool bigEndian = (encoding == 2);
        size_t offset = 0;
        if (encoding == 1) {
            if (strData[0] == 0xFE && strData[1] == 0xFF) {
                bigEndian = true;
                offset = 2;
            } else if (strData[0] == 0xFF && strData[1] == 0xFE) {
                bigEndian = false;
                offset = 2;
            }
        }
        std::wstring wstr;
        for (size_t i = offset; i + 1 < strLen; i += 2) {
            wchar_t ch;
            if (bigEndian) ch = (wchar_t)((strData[i] << 8) | strData[i + 1]);
            else ch = (wchar_t)(strData[i] | (strData[i + 1] << 8));
            if (ch == 0) break;
            wstr.push_back(ch);
        }
        if (wstr.empty()) return "";
        return WideToUtf8(wstr);
    }
    return "";
}

static Gdiplus::Bitmap* ExtractId3v2Cover(const std::string& filePath)
{
    FILE* f = _wfopen(StringToWide(filePath).c_str(), L"rb");
    if (!f) return NULL;

    unsigned char header[10];
    if (fread(header, 1, 10, f) != 10) {
        fclose(f);
        return NULL;
    }

    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        fclose(f);
        return NULL;
    }

    int version = header[3];
    if (version < 2 || version > 4) {
        fclose(f);
        return NULL;
    }

    unsigned int tagSize = ((header[6] & 0x7F) << 21) |
                           ((header[7] & 0x7F) << 14) |
                           ((header[8] & 0x7F) << 7)  |
                           (header[9] & 0x7F);

    if (tagSize == 0 || tagSize > 25 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    std::vector<unsigned char> tagData(tagSize);
    if (fread(tagData.data(), 1, tagSize, f) != tagSize) {
        fclose(f);
        return NULL;
    }
    fclose(f);

    size_t pos = 0;
    while (pos + 10 <= tagSize) {
        if (version == 3 || version == 4) {
            char frameId[5] = {0};
            memcpy(frameId, &tagData[pos], 4);
            if (frameId[0] == 0) break;

            unsigned int frameSize = 0;
            if (version == 4) {
                frameSize = ((tagData[pos + 4] & 0x7F) << 21) |
                            ((tagData[pos + 5] & 0x7F) << 14) |
                            ((tagData[pos + 6] & 0x7F) << 7)  |
                            (tagData[pos + 7] & 0x7F);
            } else {
                frameSize = ((unsigned int)tagData[pos + 4] << 24) |
                            ((unsigned int)tagData[pos + 5] << 16) |
                            ((unsigned int)tagData[pos + 6] << 8)  |
                            (unsigned int)tagData[pos + 7];
            }

            pos += 10;
            if (pos + frameSize > tagSize || frameSize < 4) break;

            if (strcmp(frameId, "APIC") == 0) {
                size_t frameEnd = pos + frameSize;
                for (size_t i = pos; i + 8 <= frameEnd; ++i) {
                    if (tagData[i] == 0xFF && tagData[i+1] == 0xD8 && tagData[i+2] == 0xFF) {
                        return LoadBitmapFromMemory(&tagData[i], frameEnd - i);
                    }
                    if (tagData[i] == 0x89 && tagData[i+1] == 0x50 && tagData[i+2] == 0x4E && tagData[i+3] == 0x47) {
                        return LoadBitmapFromMemory(&tagData[i], frameEnd - i);
                    }
                }
            }
            pos += frameSize;
        } else if (version == 2) {
            char frameId[4] = {0};
            memcpy(frameId, &tagData[pos], 3);
            if (frameId[0] == 0) break;

            unsigned int frameSize = ((unsigned int)tagData[pos + 3] << 16) |
                                     ((unsigned int)tagData[pos + 4] << 8)  |
                                     (unsigned int)tagData[pos + 5];
            pos += 6;
            if (pos + frameSize > tagSize) break;

            if (strcmp(frameId, "PIC") == 0) {
                size_t frameEnd = pos + frameSize;
                for (size_t i = pos; i + 4 <= frameEnd; ++i) {
                    if (tagData[i] == 0xFF && tagData[i+1] == 0xD8 && tagData[i+2] == 0xFF) {
                        return LoadBitmapFromMemory(&tagData[i], frameEnd - i);
                    }
                    if (tagData[i] == 0x89 && tagData[i+1] == 0x50 && tagData[i+2] == 0x4E && tagData[i+3] == 0x47) {
                        return LoadBitmapFromMemory(&tagData[i], frameEnd - i);
                    }
                }
            }
            pos += frameSize;
        }
    }
    return NULL;
}

static void ExtractId3v2Tags(const std::string& filePath, std::string& outArtist, std::string& outTitle)
{
    FILE* f = _wfopen(StringToWide(filePath).c_str(), L"rb");
    if (!f) return;

    unsigned char header[10];
    if (fread(header, 1, 10, f) != 10) { fclose(f); return; }
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') { fclose(f); return; }

    int version = header[3];
    if (version < 2 || version > 4) { fclose(f); return; }

    unsigned int tagSize = ((header[6] & 0x7F) << 21) |
                           ((header[7] & 0x7F) << 14) |
                           ((header[8] & 0x7F) << 7)  |
                           (header[9] & 0x7F);

    if (tagSize == 0 || tagSize > 25 * 1024 * 1024) { fclose(f); return; }

    std::vector<unsigned char> tagData(tagSize);
    if (fread(tagData.data(), 1, tagSize, f) != tagSize) { fclose(f); return; }
    fclose(f);

    size_t pos = 0;
    while (pos + 10 <= tagSize && (outArtist.empty() || outTitle.empty())) {
        if (version == 3 || version == 4) {
            char frameId[5] = {0};
            memcpy(frameId, &tagData[pos], 4);
            if (frameId[0] == 0) break;

            unsigned int frameSize = 0;
            if (version == 4) {
                frameSize = ((tagData[pos + 4] & 0x7F) << 21) |
                            ((tagData[pos + 5] & 0x7F) << 14) |
                            ((tagData[pos + 6] & 0x7F) << 7)  |
                            (tagData[pos + 7] & 0x7F);
            } else {
                frameSize = ((unsigned int)tagData[pos + 4] << 24) |
                            ((unsigned int)tagData[pos + 5] << 16) |
                            ((unsigned int)tagData[pos + 6] << 8)  |
                            (unsigned int)tagData[pos + 7];
            }

            pos += 10;
            if (pos + frameSize > tagSize || frameSize < 1) break;

            if (strcmp(frameId, "TPE1") == 0 && outArtist.empty()) {
                outArtist = TrimString(ParseId3TextFrame(&tagData[pos], frameSize));
            } else if (strcmp(frameId, "TIT2") == 0 && outTitle.empty()) {
                outTitle = TrimString(ParseId3TextFrame(&tagData[pos], frameSize));
            }
            pos += frameSize;
        } else if (version == 2) {
            char frameId[4] = {0};
            memcpy(frameId, &tagData[pos], 3);
            if (frameId[0] == 0) break;

            unsigned int frameSize = ((unsigned int)tagData[pos + 3] << 16) |
                                     ((unsigned int)tagData[pos + 4] << 8)  |
                                     (unsigned int)tagData[pos + 5];
            pos += 6;
            if (pos + frameSize > tagSize || frameSize < 1) break;

            if (strcmp(frameId, "TP1") == 0 && outArtist.empty()) {
                outArtist = TrimString(ParseId3TextFrame(&tagData[pos], frameSize));
            } else if (strcmp(frameId, "TT2") == 0 && outTitle.empty()) {
                outTitle = TrimString(ParseId3TextFrame(&tagData[pos], frameSize));
            }
            pos += frameSize;
        }
    }
}

static Gdiplus::Bitmap* ExtractFlacCover(const std::string& filePath)
{
    FILE* f = _wfopen(StringToWide(filePath).c_str(), L"rb");
    if (!f) return NULL;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "fLaC", 4) != 0) {
        fclose(f);
        return NULL;
    }

    bool isLast = false;
    while (!isLast) {
        unsigned char header[4];
        if (fread(header, 1, 4, f) != 4) break;

        isLast = (header[0] & 0x80) != 0;
        int blockType = header[0] & 0x7F;
        unsigned int blockSize = ((unsigned int)header[1] << 16) |
                                 ((unsigned int)header[2] << 8)  |
                                 (unsigned int)header[3];

        if (blockType == 6) {
            if (blockSize > 25 * 1024 * 1024 || blockSize < 32) {
                fclose(f);
                return NULL;
            }

            std::vector<unsigned char> data(blockSize);
            if (fread(data.data(), 1, blockSize, f) != blockSize) {
                fclose(f);
                return NULL;
            }
            fclose(f);

            size_t p = 4;
            if (p + 4 > blockSize) return NULL;
            unsigned int mimeLen = ((unsigned int)data[p] << 24) | ((unsigned int)data[p+1] << 16) | ((unsigned int)data[p+2] << 8) | data[p+3];
            p += 4 + mimeLen;
            if (p + 4 > blockSize) return NULL;
            unsigned int descLen = ((unsigned int)data[p] << 24) | ((unsigned int)data[p+1] << 16) | ((unsigned int)data[p+2] << 8) | data[p+3];
            p += 4 + descLen + 16;
            if (p + 4 > blockSize) return NULL;
            unsigned int picLen = ((unsigned int)data[p] << 24) | ((unsigned int)data[p+1] << 16) | ((unsigned int)data[p+2] << 8) | data[p+3];
            p += 4;
            if (p + picLen <= blockSize && picLen > 0) {
                return LoadBitmapFromMemory(&data[p], picLen);
            }
            for (size_t i = 8; i + 4 <= blockSize; ++i) {
                if (data[i] == 0xFF && data[i+1] == 0xD8 && data[i+2] == 0xFF) return LoadBitmapFromMemory(&data[i], blockSize - i);
                if (data[i] == 0x89 && data[i+1] == 0x50 && data[i+2] == 0x4E && data[i+3] == 0x47) return LoadBitmapFromMemory(&data[i], blockSize - i);
            }
            return NULL;
        } else {
            if (fseek(f, blockSize, SEEK_CUR) != 0) break;
        }
    }

    fclose(f);
    return NULL;
}

static void ExtractFlacTags(const std::string& filePath, std::string& outArtist, std::string& outTitle)
{
    FILE* f = _wfopen(StringToWide(filePath).c_str(), L"rb");
    if (!f) return;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "fLaC", 4) != 0) {
        fclose(f);
        return;
    }

    bool isLast = false;
    while (!isLast) {
        unsigned char header[4];
        if (fread(header, 1, 4, f) != 4) break;

        isLast = (header[0] & 0x80) != 0;
        int blockType = header[0] & 0x7F;
        unsigned int blockSize = ((unsigned int)header[1] << 16) |
                                 ((unsigned int)header[2] << 8)  |
                                 (unsigned int)header[3];

        if (blockType == 4) { // VORBIS_COMMENT
            if (blockSize > 10 * 1024 * 1024 || blockSize < 8) {
                fclose(f);
                return;
            }
            std::vector<unsigned char> data(blockSize);
            if (fread(data.data(), 1, blockSize, f) != blockSize) {
                fclose(f);
                return;
            }
            fclose(f);

            size_t p = 0;
            if (p + 4 > blockSize) return;
            unsigned int vendorLen = data[p] | (data[p+1] << 8) | (data[p+2] << 16) | (data[p+3] << 24);
            p += 4 + vendorLen;
            if (p + 4 > blockSize) return;
            unsigned int commentCount = data[p] | (data[p+1] << 8) | (data[p+2] << 16) | (data[p+3] << 24);
            p += 4;

            for (unsigned int i = 0; i < commentCount && p + 4 <= blockSize; ++i) {
                unsigned int cLen = data[p] | (data[p+1] << 8) | (data[p+2] << 16) | (data[p+3] << 24);
                p += 4;
                if (p + cLen > blockSize) break;

                std::string comment((const char*)&data[p], cLen);
                p += cLen;

                size_t eq = comment.find('=');
                if (eq != std::string::npos) {
                    std::string key = comment.substr(0, eq);
                    std::transform(key.begin(), key.end(), key.begin(), ::toupper);
                    std::string val = TrimString(comment.substr(eq + 1));
                    if (key == "ARTIST" && outArtist.empty()) outArtist = val;
                    else if (key == "TITLE" && outTitle.empty()) outTitle = val;
                }
            }
            return;
        } else {
            if (fseek(f, blockSize, SEEK_CUR) != 0) break;
        }
    }
    fclose(f);
}

static Gdiplus::Bitmap* ExtractMp4Cover(const std::string& filePath)
{
    FILE* f = _wfopen(StringToWide(filePath).c_str(), L"rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fileLen = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileLen <= 32) {
        fclose(f);
        return NULL;
    }

    size_t scanSize = (fileLen > 8 * 1024 * 1024) ? (8 * 1024 * 1024) : fileLen;
    std::vector<unsigned char> buf(scanSize);
    size_t readBytes = fread(buf.data(), 1, scanSize, f);
    fclose(f);

    if (readBytes < 32) return NULL;

    for (size_t i = 0; i + 16 < readBytes; ++i) {
        if (buf[i] == 'c' && buf[i+1] == 'o' && buf[i+2] == 'v' && buf[i+3] == 'r') {
            size_t maxSearch = (i + 2048 < readBytes) ? i + 2048 : readBytes;
            for (size_t j = i + 4; j + 16 < maxSearch; ++j) {
                if (buf[j] == 'd' && buf[j+1] == 'a' && buf[j+2] == 't' && buf[j+3] == 'a') {
                    size_t dataStart = j - 4;
                    unsigned int dataSize = ((unsigned int)buf[dataStart] << 24) |
                                            ((unsigned int)buf[dataStart+1] << 16) |
                                            ((unsigned int)buf[dataStart+2] << 8) |
                                            buf[dataStart+3];
                    size_t imgOffset = j + 12;
                    if (dataSize > 16 && imgOffset + (dataSize - 16) <= readBytes) {
                        return LoadBitmapFromMemory(&buf[imgOffset], dataSize - 16);
                    }
                    for (size_t k = j + 4; k + 4 < j + 32 && k + 4 < readBytes; ++k) {
                        if (buf[k] == 0xFF && buf[k+1] == 0xD8 && buf[k+2] == 0xFF) return LoadBitmapFromMemory(&buf[k], readBytes - k);
                        if (buf[k] == 0x89 && buf[k+1] == 0x50 && buf[k+2] == 0x4E && buf[k+3] == 0x47) return LoadBitmapFromMemory(&buf[k], readBytes - k);
                    }
                }
            }
        }
    }
    return NULL;
}

static void ExtractMp4Tags(const std::string& filePath, std::string& outArtist, std::string& outTitle)
{
    FILE* f = _wfopen(StringToWide(filePath).c_str(), L"rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long fileLen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileLen <= 32) { fclose(f); return; }

    size_t scanSize = (fileLen > 8 * 1024 * 1024) ? (8 * 1024 * 1024) : fileLen;
    std::vector<unsigned char> buf(scanSize);
    size_t readBytes = fread(buf.data(), 1, scanSize, f);
    fclose(f);
    if (readBytes < 32) return;

    auto findTag = [&](const char* tagId) -> std::string {
        for (size_t i = 0; i + 24 < readBytes; ++i) {
            if (memcmp(&buf[i], tagId, 4) == 0) {
                size_t maxSearch = (i + 512 < readBytes) ? i + 512 : readBytes;
                for (size_t j = i + 4; j + 16 < maxSearch; ++j) {
                    if (memcmp(&buf[j], "data", 4) == 0) {
                        size_t dataSizeOffset = j - 4;
                        unsigned int dataSize = ((unsigned int)buf[dataSizeOffset] << 24) |
                                                ((unsigned int)buf[dataSizeOffset + 1] << 16) |
                                                ((unsigned int)buf[dataSizeOffset + 2] << 8) |
                                                buf[dataSizeOffset + 3];
                        size_t strOffset = j + 12;
                        if (dataSize > 16 && strOffset + (dataSize - 16) <= readBytes) {
                            return TrimString(std::string((const char*)&buf[strOffset], dataSize - 16));
                        }
                    }
                }
            }
        }
        return "";
    };

    if (outArtist.empty()) outArtist = findTag("\xa9" "ART");
    if (outTitle.empty()) outTitle = findTag("\xa9" "nam");
}

static Gdiplus::Bitmap* ExtractWavCover(const std::string& filePath)
{
    FILE* f = _wfopen(StringToWide(filePath).c_str(), L"rb");
    if (!f) return NULL;

    char riff[4];
    if (fread(riff, 1, 4, f) != 4 || memcmp(riff, "RIFF", 4) != 0) {
        fclose(f);
        return ExtractId3v2Cover(filePath);
    }

    fseek(f, 8, SEEK_SET);
    char wave[4];
    if (fread(wave, 1, 4, f) != 4 || memcmp(wave, "WAVE", 4) != 0) {
        fclose(f);
        return ExtractId3v2Cover(filePath);
    }

    while (true) {
        char chunkId[4];
        unsigned int chunkSize = 0;
        if (fread(chunkId, 1, 4, f) != 4) break;
        if (fread(&chunkSize, 4, 1, f) != 1) break;

        if (memcmp(chunkId, "id3 ", 4) == 0 || memcmp(chunkId, "ID3 ", 4) == 0) {
            std::vector<unsigned char> data(chunkSize);
            if (fread(data.data(), 1, chunkSize, f) == chunkSize) {
                fclose(f);
                for (size_t i = 0; i + 4 <= chunkSize; ++i) {
                    if (data[i] == 0xFF && data[i+1] == 0xD8 && data[i+2] == 0xFF) return LoadBitmapFromMemory(&data[i], chunkSize - i);
                    if (data[i] == 0x89 && data[i+1] == 0x50 && data[i+2] == 0x4E && data[i+3] == 0x47) return LoadBitmapFromMemory(&data[i], chunkSize - i);
                }
            }
            return NULL;
        }

        unsigned int advance = chunkSize + (chunkSize & 1);
        if (fseek(f, advance, SEEK_CUR) != 0) break;
    }

    fclose(f);
    return ExtractId3v2Cover(filePath);
}

Gdiplus::Bitmap* ExtractTrackCover(const std::string& filePath)
{
    std::string lower = filePath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    Gdiplus::Bitmap* bmp = NULL;
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".mp3") {
        bmp = ExtractId3v2Cover(filePath);
    } else if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".flac") {
        bmp = ExtractFlacCover(filePath);
        if (!bmp) bmp = ExtractId3v2Cover(filePath);
    } else if (lower.size() >= 4 && (lower.substr(lower.size() - 4) == ".m4a" || lower.substr(lower.size() - 4) == ".aac")) {
        bmp = ExtractMp4Cover(filePath);
        if (!bmp) bmp = ExtractId3v2Cover(filePath);
    } else if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".wav") {
        bmp = ExtractWavCover(filePath);
    } else {
        bmp = ExtractId3v2Cover(filePath);
        if (!bmp) bmp = ExtractFlacCover(filePath);
        if (!bmp) bmp = ExtractMp4Cover(filePath);
    }
    return bmp;
}

static void ExtractId3v1Tags(const std::string& filePath, std::string& outArtist, std::string& outTitle)
{
    FILE* f = _wfopen(StringToWide(filePath).c_str(), L"rb");
    if (!f) return;

    if (fseek(f, -128, SEEK_END) != 0) {
        fclose(f);
        return;
    }

    unsigned char buf[128];
    if (fread(buf, 1, 128, f) != 128) {
        fclose(f);
        return;
    }
    fclose(f);

    if (buf[0] != 'T' || buf[1] != 'A' || buf[2] != 'G') {
        return;
    }

    auto decodeTagString = [](const unsigned char* rawBytes, size_t maxLen) -> std::string {
        size_t len = maxLen;
        while (len > 0 && (rawBytes[len - 1] == 0 || rawBytes[len - 1] == ' ')) len--;
        if (len == 0) return "";
        std::string raw((const char*)rawBytes, len);

        int uLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.c_str(), (int)raw.size(), NULL, 0);
        if (uLen > 0) {
            bool hasHigh = false;
            for (unsigned char c : raw) { if (c >= 0x80) { hasHigh = true; break; } }
            if (hasHigh) return raw;
        }

        int sjisLen = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, raw.c_str(), (int)raw.size(), NULL, 0);
        if (sjisLen > 0) {
            std::wstring w(sjisLen, 0);
            MultiByteToWideChar(932, 0, raw.c_str(), (int)raw.size(), &w[0], sjisLen);
            return WideToUtf8(w);
        }

        int acpLen = MultiByteToWideChar(CP_ACP, 0, raw.c_str(), (int)raw.size(), NULL, 0);
        if (acpLen > 0) {
            std::wstring w(acpLen, 0);
            MultiByteToWideChar(CP_ACP, 0, raw.c_str(), (int)raw.size(), &w[0], acpLen);
            return WideToUtf8(w);
        }
        return raw;
    };

    if (outTitle.empty()) {
        outTitle = TrimString(decodeTagString(&buf[3], 30));
    }
    if (outArtist.empty()) {
        outArtist = TrimString(decodeTagString(&buf[33], 30));
    }
}

void ExtractTrackMetadata(const std::string& filePath, std::string& outArtist, std::string& outTitle)
{
    outArtist = "";
    outTitle = "";

    std::string lower = filePath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".mp3") {
        ExtractId3v2Tags(filePath, outArtist, outTitle);
        if (outArtist.empty() || outTitle.empty()) {
            ExtractId3v1Tags(filePath, outArtist, outTitle);
        }
    } else if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".flac") {
        ExtractFlacTags(filePath, outArtist, outTitle);
        if (outArtist.empty() && outTitle.empty()) ExtractId3v2Tags(filePath, outArtist, outTitle);
    } else if (lower.size() >= 4 && (lower.substr(lower.size() - 4) == ".m4a" || lower.substr(lower.size() - 4) == ".aac" || lower.substr(lower.size() - 4) == ".mp4")) {
        ExtractMp4Tags(filePath, outArtist, outTitle);
        if (outArtist.empty() && outTitle.empty()) ExtractId3v2Tags(filePath, outArtist, outTitle);
    } else {
        ExtractId3v2Tags(filePath, outArtist, outTitle);
    }

    auto isCorruptedOrQuestionMarks = [](const std::string& s) -> bool {
        if (s.empty()) return true;
        for (unsigned char c : s) {
            if (c != '?' && c != ' ' && c != '\t' && c != '-' && c != '_') return false;
        }
        return true;
    };

    if (isCorruptedOrQuestionMarks(outTitle)) outTitle = "";
    if (isCorruptedOrQuestionMarks(outArtist)) outArtist = "";

    std::string baseFilename = GetFilenameWithoutExtension(filePath);
    if (outArtist.empty() && outTitle.empty()) {
        size_t dash = baseFilename.find(" - ");
        if (dash != std::string::npos) {
            outArtist = TrimString(baseFilename.substr(0, dash));
            outTitle = TrimString(baseFilename.substr(dash + 3));
        } else {
            outTitle = baseFilename;
        }
    } else if (outArtist.empty()) {
        size_t dash = baseFilename.find(" - ");
        if (dash != std::string::npos) {
            outArtist = TrimString(baseFilename.substr(0, dash));
            if (outTitle.empty()) {
                outTitle = TrimString(baseFilename.substr(dash + 3));
            }
        }
    } else if (outTitle.empty()) {
        outTitle = baseFilename;
    }
}

Gdiplus::Bitmap* LoadStationCover(const std::string& folderPath)
{
    static const std::vector<std::string> exts = {
        ".png", ".jpg", ".jpeg", ".bmp", ".webp", ".gif"
    };

    for (const auto& ext : exts) {
        std::string cand = folderPath + "\\cover" + ext;
        Gdiplus::Bitmap* bmp = LoadBitmapFromFile(cand);
        if (bmp) return bmp;
    }

    std::wstring searchPattern = StringToWide(folderPath) + L"\\cover.*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::string fullPath = folderPath + "\\" + WideToUtf8(fd.cFileName);
                Gdiplus::Bitmap* bmp = LoadBitmapFromFile(fullPath);
                if (bmp) {
                    FindClose(hFind);
                    return bmp;
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    return NULL;
}

void UpdateCurrentCover(const std::string& trackPath, const std::string& stationFolder)
{
    EnterCriticalSection(&g_popupCs);
    if (g_pCurrentCover) {
        delete g_pCurrentCover;
        g_pCurrentCover = NULL;
    }

    g_pCurrentCover = ExtractTrackCover(trackPath);
    if (g_pCurrentCover) {
        LogMsg("Active Cover: Extracted from track metadata.");
    } else {
        g_pCurrentCover = LoadStationCover(stationFolder);
        if (g_pCurrentCover) {
            LogMsg("Active Cover: Loaded station cover image.");
        } else {
            LogMsg("Active Cover: None available.");
        }
    }
    LeaveCriticalSection(&g_popupCs);
}
