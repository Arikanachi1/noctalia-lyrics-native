#include "playback.hpp"
#include <QUrl>
#include <QUrlQuery>
#include <QStringList>
#include <algorithm>
#include <cmath>

bool isYouTubeMusic(const QString &value, const QString &desktopEntry, const QString &identity) {
    const QUrl url(value);
    const auto host = url.host().toLower();
    if (host == "music.youtube.com") return url.scheme() == "https" || url.scheme() == "http";
    // A dedicated app's identity must not make an ordinary YouTube video eligible.
    if (host == "youtube.com" || host.endsWith(".youtube.com") || host == "youtu.be") return false;
    auto desktop = desktopEntry.toLower();
    if (desktop.endsWith(".desktop")) desktop.chop(8);
    const QStringList desktops{"ytmdesktop", "youtube-music", "youtube-music-for-desktop", "pear-desktop"};
    const QStringList identities{"youtube music", "youtube music desktop app", "ytmdesktop", "pear desktop"};
    return desktops.contains(desktop) || identities.contains(identity.simplified().toLower());
}

QString mediaIdentity(const QString &value) {
    const QUrl url(value);
    if (url.host().compare("music.youtube.com", Qt::CaseInsensitive) == 0) {
        const auto video = QUrlQuery(url).queryItemValue("v");
        if (!video.isEmpty()) return "ytmusic:" + video;
    }
    return value;
}

bool isSupportedMusicSource(const QString &url, const QString &desktopEntry, const QString &identity) {
    auto desktop = desktopEntry.toLower();
    if (desktop.endsWith(".desktop")) desktop.chop(8);
    if (desktop == "wave" && identity.simplified().compare("Wave", Qt::CaseInsensitive) == 0)
        return true; // Wave exports only its own local/streaming music, never a browser video.
    return isYouTubeMusic(url, desktopEntry, identity);
}

double PlaybackClock::position(qint64 now) const {
    if (!known) return 0;
    return std::max(0.0, anchor + (playing ? std::max<qint64>(0, now - anchoredAt) / 1000.0 * rate : 0));
}

void PlaybackClock::observe(const QString &identity, double raw, bool valid, bool running, double speed, qint64 now) {
    if (identity != key) {
        key = identity; known = false; haveRaw = false; anchor = 0; anchoredAt = now;
    }
    double current = position(now);
    valid = valid && std::isfinite(raw) && raw >= 0;
    if (valid && (!known || !haveRaw || std::abs(raw - lastRaw) > .01)) {
        // Coarse clocks can report a new second slightly behind our estimate.
        // Avoid moving text backwards for that quantization, but honour seeks.
        if (!known || !haveRaw || raw < lastRaw - .01 || std::abs(raw - current) > 1.25 || raw > current)
            current = raw;
        known = true; haveRaw = true; lastRaw = raw;
    }
    anchor = current; anchoredAt = now; playing = running;
    rate = std::isfinite(speed) && speed > 0 ? speed : 1;
}

void PlaybackClock::seek(double seconds, qint64 now) {
    if (!std::isfinite(seconds) || seconds < 0) return;
    anchor = seconds; anchoredAt = now; known = true;
    // Keep lastRaw: a stale Position read immediately after Seeked must not
    // undo the signal. A subsequent changed Position is authoritative again.
}
