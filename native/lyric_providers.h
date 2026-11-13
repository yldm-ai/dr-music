#pragma once
#include "http.h"
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace dr {
// Online lyric providers, ported from BetterLyrics (GPL-3.0, LyricsSearchService / MetadataComparer)
// and Lyricify Lyrics Helper (Apache-2.0, Providers/Web/*, Decrypter/*). Independent C++ code;
// request shapes, endpoints and decoders follow those projects. All traffic goes through an
// injected HttpFetch, so tests run every provider against fixtures.

/** What we know about a library track. Duration is in seconds (0 when unknown). */
struct TrackQuery { std::string title, artist, album; double duration = 0; };
/** Metadata of a remote match; any field may list several aliases. */
struct LyricCandidate { std::vector<std::string> titles, artists, albums; double duration = 0; };
struct LyricResult {
    std::string provider, title, artists, album, format, text, reference;
    double duration = 0;
    int score = 0;
    /** The provider's own romanization (NetEase romalrc "lrc", QQ contentroma "qrc"), when it has one. */
    std::string roman, roman_format;
};

/**
 * Scores remote metadata against a track, after BetterLyrics' MetadataComparer: Jaro-Winkler
 * similarity weighted title 30 %, artist 30 %, album 10 %, duration 30 %, renormalized over the
 * fields both sides have. Untagged songs ("未知艺术家") compare a sorted word fingerprint of the
 * file-name title with "title artist". Stricter gates than BetterLyrics: a wrong lyric is worse than none.
 */
class LyricMatcher {
public:
    explicit LyricMatcher(std::function<std::string(const std::string &)> fold = {});
    /** 0 when rejected, otherwise 1…100. */
    int score(const TrackQuery &track, const LyricCandidate &candidate) const;
    std::string key(const std::string &text) const;
    std::string fingerprint(const std::string &text) const;
    static double similarity(const std::string &a, const std::string &b);
private:
    std::function<std::string(const std::string &)> fold_;
};

/** Splits "A / B、C feat. D" into artist names. */
std::vector<std::string> split_artists(const std::string &text);
/** Search keywords to try in order: "title artist album", "title artist", "title". */
std::vector<std::string> search_terms(const TrackQuery &track);

std::optional<LyricResult> search_lrclib(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track);
std::optional<LyricResult> search_netease(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track);
std::optional<LyricResult> search_qq(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track);
std::optional<LyricResult> search_kugou(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track);
/** lrc.red (formerly BiniLyrics), the community TTML catalog Better Lyrics streams as "BiniLyrics". */
std::optional<LyricResult> search_lrcred(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track);

/** NetEase EAPI request body ("params=…") for a /api/… path, as Lyricify's EapiHelper builds it. */
std::string netease_eapi_body(const std::string &api_path, const Json &data);
/** Extracts the QRC text of one element (content = lyric, contentroma = romanization) from QQ Music's lyric_download.fcg XML response. */
std::string qq_lyric_from_response(const std::string &xml, const std::string &tag = "content");
/** Picks the most plausible Apple Music web-player token (JWT) from a script, or "". */
std::string apple_pick_token(const std::string &script, long long now_seconds);

/**
 * Apple Music catalog lyrics. Needs the user's own media-user-token (an Apple Music subscription
 * cookie); the public web-player bearer token is scraped from music.apple.com and cached.
 */
class AppleMusicLyrics {
public:
    std::optional<LyricResult> search(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track, const std::string &media_user_token);
private:
    void ensure_session(const HttpFetch &fetch, const std::string &media_user_token);
    std::mutex mutex_;
    std::string token_, storefront_, language_, session_user_;
    long long expires_ = 0;
};
}
