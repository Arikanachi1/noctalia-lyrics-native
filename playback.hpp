#pragma once
#include <QString>

bool isYouTubeMusic(const QString &url, const QString &desktopEntry = {}, const QString &identity = {});
bool isSupportedMusicSource(const QString &url, const QString &desktopEntry = {}, const QString &identity = {});
QString mediaIdentity(const QString &url);

// MPRIS Position need not change between reads. While Playing, advance from
// the last authoritative position using Rate; Seeked always reanchors it.
class PlaybackClock {
    QString key;
    bool known = false, playing = false, haveRaw = false;
    double anchor = 0, lastRaw = 0, rate = 1;
    qint64 anchoredAt = 0;
public:
    void observe(const QString &identity, double raw, bool available, bool running, double speed, qint64 now);
    void seek(double seconds, qint64 now);
    double position(qint64 now) const;
    bool available() const { return known; }
};
