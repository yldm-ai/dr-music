#include "fonts.h"
#include <windows.h>
#include <dwrite.h>
#include <wrl.h>
#include <algorithm>
#include <set>
#include <string_view>

namespace dr {
namespace {
std::string narrow(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring localized(IDWriteLocalizedStrings *names, const wchar_t *locale) {
    UINT32 index = 0; BOOL exists = FALSE;
    if (FAILED(names->FindLocaleName(locale, &index, &exists)) || !exists) return {};
    UINT32 length = 0;
    if (FAILED(names->GetStringLength(index, &length))) return {};
    std::wstring text(length + 1, L'\0');
    if (FAILED(names->GetString(index, text.data(), length + 1))) return {};
    text.resize(length);
    return text;
}
std::wstring first_name(IDWriteLocalizedStrings *names) {
    UINT32 length = 0;
    if (names->GetCount() == 0 || FAILED(names->GetStringLength(0, &length))) return {};
    std::wstring text(length + 1, L'\0');
    if (FAILED(names->GetString(0, text.data(), length + 1))) return {};
    text.resize(length);
    return text;
}
}

bool valid_font_family(const std::string &name) {
    if (name.empty() || name.size() > 200) return false;
    return std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f || std::string_view("\"'\\;{}<>()`").find(static_cast<char>(c)) != std::string_view::npos; });
}

std::vector<FontFamily> system_font_families() {
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(factory.GetAddressOf())))) return {};
    Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(&collection, FALSE)) || !collection) return {};
    std::vector<FontFamily> families;
    std::set<std::string> seen;
    for (UINT32 i = 0, count = collection->GetFontFamilyCount(); i < count; ++i) {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(collection->GetFontFamily(i, &family)) || FAILED(family->GetFamilyNames(&names))) continue;
        auto english = localized(names.Get(), L"en-us");
        if (english.empty()) english = first_name(names.Get());
        auto chinese = localized(names.Get(), L"zh-cn");
        FontFamily entry{narrow(english), narrow(chinese.empty() ? english : chinese)};
        if (entry.family.empty() || entry.family.front() == '@' || !valid_font_family(entry.family) || !seen.insert(entry.family).second) continue;
        families.push_back(std::move(entry));
    }
    std::sort(families.begin(), families.end(), [](const FontFamily &a, const FontFamily &b) { return a.display < b.display; });
    return families;
}
}
