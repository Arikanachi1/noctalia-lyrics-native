#include "lyrics.hpp"
#include "catalog.hpp"
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
QVector<LyricLine> parseLrc(const QString &text) {
    QVector<LyricLine> out;
    static const QRegularExpression stamp(R"(\[(\d+):(\d{1,2}(?:\.\d+)?)\])");
    static const QRegularExpression offsetRx(R"(\[offset:([+-]?\d+)\])", QRegularExpression::CaseInsensitiveOption);
    auto offsetMatch = offsetRx.match(text);
    double offset = offsetMatch.hasMatch() ? offsetMatch.captured(1).toDouble()/1000.0 : 0;
    for (const auto &line : text.split('\n')) {
        auto it = stamp.globalMatch(line);
        QVector<double> times;
        qsizetype end = 0;
        while (it.hasNext()) {
            auto m = it.next(); end = m.capturedEnd();
            double seconds = m.captured(2).toDouble();
            if (seconds < 60) times.push_back(m.captured(1).toDouble()*60 + seconds + offset);
        }
        QString words = line.mid(end).trimmed();
        for (double time : times) out.push_back({std::max(0.0, time), words});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b){return a.seconds < b.seconds;});
    return out;
}
int currentLine(const QVector<LyricLine> &lines, double seconds) {
    if (!std::isfinite(seconds)) return -1;
    auto it = std::upper_bound(lines.begin(), lines.end(), seconds,
        [](double time, const auto &line){return time < line.seconds;});
    return it == lines.begin() ? -1 : static_cast<int>(it-lines.begin()-1);
}
QString cleanTitle(QString title) {
    static const QRegularExpression extra(R"(\s*[\[(](?:official\s+(?:music\s+)?(?:video|audio)|lyrics?|visuali[sz]er|audio|music video|4k|hd)[\])]\s*)", QRegularExpression::CaseInsensitiveOption);
    title.remove(extra);
    return title.simplified();
}
QJsonArray jsonLines(const QVector<LyricLine> &lines) {
    QJsonArray array;
    for (const auto &line : lines) array.append(QJsonObject{{"time",line.seconds},{"text",line.text}});
    return array;
}
QString normalizedName(QString name) {
    name=cleanTitle(name.normalized(QString::NormalizationForm_KC)).toCaseFolded();
    name.replace(QChar(0x2019), QChar('\''));
    name.remove(QRegularExpression("[\\x{200B}-\\x{200D}\\x{FEFF}]"));
    // Japanese titles often differ only in fullwidth forms or optional spaces.
    if(name.contains(QRegularExpression("[\\x{3040}-\\x{30FF}\\x{3400}-\\x{9FFF}]")))
        name.remove(QRegularExpression("\\s+"));
    return name;
}
double recordScore(const QJsonObject &record, const QString &title, const QString &artist, double duration) {
    if(normalizedName(record.value("trackName").toString())!=normalizedName(title)) return -1;
    if(!artist.isEmpty() && normalizedArtist(record.value("artistName").toString())!=normalizedArtist(artist)) return -1;
    double found=record.value("duration").toDouble();
    double difference=duration>0 && found>0 ? std::abs(found-duration) : 0;
    // Music videos, covers and alternate versions must not replace the recording.
    if(duration>0 && (found<=0 || difference>3)) return -1;
    return difference+(parseLrc(record.value("syncedLyrics").toString()).isEmpty()?10:0);
}
