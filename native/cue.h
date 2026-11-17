#pragma once
#include "common.h"
#include <string>
#include <vector>
namespace dr {
/**
 * CUE sheets for whole-CD rips (one audio file + a .cue): each TRACK becomes a song that plays the
 * range from its INDEX 01 to the next track's INDEX 01 (the last one to the end of the file).
 */
struct CueTrack {
    int number = 0;
    std::string title, performer;
    double start = 0, end = 0; // seconds into the audio file; end 0 = to the end of the file
};
struct CueFile {
    fs::path audio; // as written in the sheet, resolved against its folder (not checked to exist)
    std::vector<CueTrack> tracks;
};
struct CueSheet {
    std::string title, performer;
    int year = 0, disc = 0;
    std::vector<CueFile> files;
};
/** Sheet text as UTF-8: UTF-8 (with or without BOM) / UTF-16 as is, otherwise GBK, Shift-JIS or the ANSI code page. */
std::string cue_text(std::string bytes);
/** Parses sheet text; `folder` resolves FILE names. Tracks without INDEX 01 are skipped. */
CueSheet parse_cue(const std::string &text, const fs::path &folder);
/** Reads and parses a .cue file (size-capped). */
CueSheet read_cue(const fs::path &file);
}
