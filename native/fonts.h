#pragma once
#include <string>
#include <vector>
namespace dr {
struct FontFamily {
    std::string family;  // Stable (en-US when available) name used for CSS matching and persistence.
    std::string display; // User-facing name, preferring the zh-CN localization.
};
// Installed system font families via DirectWrite, sorted by display name, without vertical (@) faces.
std::vector<FontFamily> system_font_families();
// A font family name accepted from the page: non-empty, bounded and free of CSS/markup delimiters.
bool valid_font_family(const std::string &name);
}
