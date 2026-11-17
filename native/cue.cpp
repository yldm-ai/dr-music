#include "cue.h"
#include <algorithm>
#include <cctype>
#include <sstream>
namespace dr {
namespace {
std::string from_code_page(const std::string &bytes, UINT code_page) {
    const auto size = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!size) return {};
    std::wstring text(size, L'\0');
    MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), size);
    return utf8(text);
}
std::string upper(std::string text) { std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); }); return text; }
/** The line's words; a "quoted string" is one word (CUE has no escapes). */
std::vector<std::string> words(const std::string &line) {
    std::vector<std::string> out;
    for (size_t at = 0; at < line.size();) {
        if (line[at] == ' ' || line[at] == '\t') { ++at; continue; }
        if (line[at] == '"') {
            const auto end = line.find('"', at + 1);
            out.push_back(line.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1));
            at = end == std::string::npos ? line.size() : end + 1;
        } else {
            const auto end = line.find_first_of(" \t", at);
            out.push_back(line.substr(at, end == std::string::npos ? std::string::npos : end - at));
            at = end == std::string::npos ? line.size() : end;
        }
    }
    return out;
}
/** mm:ss:ff (75 frames a second) → seconds; -1 when malformed. */
double cue_time(const std::string &text) {
    int minutes = 0, seconds = 0, frames = 0; char a = 0, b = 0;
    std::istringstream in(text);
    if (!(in >> minutes >> a >> seconds >> b >> frames) || a != ':' || b != ':' || minutes < 0 || seconds < 0 || seconds > 59 || frames < 0 || frames > 74) return -1;
    return minutes * 60.0 + seconds + frames / 75.0;
}
int number(const std::string &text) { try { return std::stoi(text); } catch (const std::exception &) { return 0; } }
}

std::string cue_text(std::string bytes) {
    if (bytes.starts_with("\xEF\xBB\xBF")) return bytes.substr(3);
    if (bytes.starts_with("\xFF\xFE") && bytes.size() % 2 == 0) {
        std::wstring text;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) text.push_back(static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) | (static_cast<unsigned char>(bytes[i + 1]) << 8)));
        return utf8(text);
    }
    if (bytes.empty()) return bytes;
    // Rippers write UTF-8 or the system's code page: Chinese sheets are usually GBK, Japanese ones Shift-JIS.
    // The ANSI code page goes first only when it is a CJK one; western ones decode almost any bytes.
    const UINT ansi = GetACP(); const bool cjk = ansi == 936 || ansi == 932 || ansi == 950 || ansi == 949;
    for (UINT code_page : {static_cast<UINT>(CP_UTF8), cjk ? ansi : 936u, 936u, 932u, 950u, ansi, 1252u})
        if (auto text = from_code_page(bytes, code_page); !text.empty()) return text;
    return {};
}

CueSheet parse_cue(const std::string &text, const fs::path &folder) {
    CueSheet sheet;
    CueTrack *track = nullptr;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto w = words(line);
        if (w.empty()) continue;
        const auto key = upper(w[0]);
        if (key == "FILE" && w.size() >= 2) {
            fs::path name;
            try { name = from_text(w[1]); } catch (const std::exception &) { continue; }
            sheet.files.push_back({name.is_absolute() ? name : folder / name, {}}); track = nullptr;
        } else if (key == "TRACK" && w.size() >= 2 && !sheet.files.empty()) {
            auto &tracks = sheet.files.back().tracks;
            tracks.push_back({number(w[1]), {}, {}, -1, 0}); track = &tracks.back();
        } else if ((key == "TITLE" || key == "PERFORMER") && w.size() >= 2) {
            auto &field = track ? (key == "TITLE" ? track->title : track->performer) : (key == "TITLE" ? sheet.title : sheet.performer);
            field = w[1];
        } else if (key == "INDEX" && w.size() >= 3 && track && number(w[1]) == 1) track->start = cue_time(w[2]);
        else if (key == "REM" && w.size() >= 3) {
            const auto field = upper(w[1]);
            if (field == "DATE") sheet.year = number(w[2].substr(0, 4));
            else if (field == "DISCNUMBER") sheet.disc = number(w[2]);
        }
    }
    for (auto &file : sheet.files) {
        std::erase_if(file.tracks, [](const CueTrack &t) { return t.start < 0; });
        // Each track runs until the next one starts; the last to the end of the file.
        for (size_t i = 0; i < file.tracks.size(); ++i) file.tracks[i].end = i + 1 < file.tracks.size() ? file.tracks[i + 1].start : 0;
        std::erase_if(file.tracks, [](const CueTrack &t) { return t.end > 0 && t.end <= t.start; });
    }
    std::erase_if(sheet.files, [](const CueFile &f) { return f.tracks.empty(); });
    return sheet;
}

CueSheet read_cue(const fs::path &file) {
    return parse_cue(cue_text(read_bytes(file, 1024 * 1024)), file.parent_path());
}
}
