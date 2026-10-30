// SPDX-License-Identifier: GPL-3.0-only
// Which files Convertii can take. Shared by the application (folder drops)
// and by the shell extension (menu visibility), so the two never disagree.
#pragma once
#include <string>
#include <string_view>

namespace cvt {
// The extension is expected lower case and with the leading dot.
inline bool is_media_extension(std::string_view extension) {
    static constexpr std::string_view list[] = {
        ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".m4v", ".mpg", ".mpeg",
        ".ts", ".mts", ".m2ts", ".3gp", ".ogv", ".vob", ".rmvb",
        ".mp3", ".wav", ".flac", ".aac", ".ogg", ".m4a", ".wma", ".opus", ".aiff", ".ape",
        ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tif", ".tiff", ".heic", ".avif"};
    for (auto candidate : list) {
        if (candidate == extension) return true;
    }
    return false;
}
// Lower cases an ASCII extension so callers can pass whatever the file system
// handed them.
inline std::string lower_extension(std::string_view extension) {
    std::string value(extension);
    for (auto& character : value) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
    return value;
}
}
