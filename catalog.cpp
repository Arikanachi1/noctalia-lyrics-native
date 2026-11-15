#include "catalog.hpp"
#include "lyrics.hpp"
#include <QtNetwork>
#include <algorithm>
#include <cmath>
#include <iostream>

QString normalizedArtist(QString artist) {
    artist = artist.normalized(QString::NormalizationForm_KC).simplified();
    static const QRegularExpression topic(R"(\s*(?:[-–—]\s*Topic|\((?:Official|公式)\)|VEVO)\s*$)", QRegularExpression::CaseInsensitiveOption);
    artist.remove(topic);
    return normalizedName(artist);
}

QJsonObject matchRecording(const QJsonArray &recordings, const QString &title, const QString &artist, double duration) {
    // Metadata enrichment is conservative: never identify a song by title alone.
    if (artist.isEmpty()) return {};
    QJsonObject selected;
    double best = 1e9;
    for (const auto &value : recordings) {
        auto record = value.toObject();
        if (normalizedName(record.value("title").toString()) != normalizedName(title)) continue;
        QStringList credits;
        bool sameArtist = false;
        for (const auto &credit : record.value("artist-credit").toArray()) {
            auto entry = credit.toObject();
            auto performer = entry.value("artist").toObject();
            QString credited = entry.value("name").toString(performer.value("name").toString());
            credits.push_back(credited + entry.value("joinphrase").toString());
            for (const auto &name : {credited, performer.value("name").toString(), performer.value("sort-name").toString()})
                if (!name.isEmpty() && normalizedArtist(name) == normalizedArtist(artist)) sameArtist = true;
        }
        QString fullArtist = credits.join("");
        sameArtist = sameArtist || normalizedArtist(fullArtist) == normalizedArtist(artist);
        if (!sameArtist) continue;
        const double length = record.value("length").toDouble()/1000.0;
        if (duration > 0 && (length <= 0 || std::abs(length-duration) > 3)) continue;
        double difference = duration > 0 ? std::abs(length-duration) : 0;
        if (difference < best) {
            best = difference;
            selected = {{"title", record.value("title")}, {"artist", fullArtist}, {"id", record.value("id")}};
        }
    }
    return selected;
}

QString overridePath(const QString &trackId) {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/overrides/"+trackId+".json";
}

QJsonArray rankedLyrics(const QJsonArray &records, const QString &title, const QString &artist, double duration, int limit) {
    struct Candidate { QJsonObject record; double rank; };
    QVector<Candidate> candidates;
    QSet<qint64> seen;
    for (const auto &value : records) {
        const auto record = value.toObject();
        if (record.value("trackName").toString().isEmpty() || record.value("artistName").toString().isEmpty()) continue;
        const auto id = static_cast<qint64>(record.value("id").toDouble());
        if (id <= 0 || seen.contains(id)) continue;
        const auto lines = parseLrc(record.value("syncedLyrics").toString());
        const auto plain = record.value("plainLyrics").toString();
        if (lines.isEmpty() && plain.isEmpty() && !record.value("instrumental").toBool()) continue;
        seen.insert(id);
        const auto length = record.value("duration").toDouble();
        const bool durationFits = duration > 0 && length > 0 && std::abs(length-duration) <= 3;
        const double exact = title.isEmpty() ? -1 : recordScore(record, title, artist, duration);
        double rank = exact >= 0 ? exact : 100;
        if (exact < 0) {
            if (!title.isEmpty() && normalizedName(record.value("trackName").toString()) != normalizedName(title)) rank += 200;
            if (!artist.isEmpty() && normalizedArtist(record.value("artistName").toString()) != normalizedArtist(artist)) rank += 100;
            if (duration > 0) rank += length > 0 ? std::min(80.0, std::abs(length-duration)) : 80;
            if (lines.isEmpty()) rank += 10;
        }
        candidates.push_back({QJsonObject{{"id", record.value("id")}, {"title", record.value("trackName")},
            {"artist", record.value("artistName")}, {"album", record.value("albumName")},
            {"duration", length}, {"synced", !lines.isEmpty()}, {"instrumental", record.value("instrumental").toBool()},
            {"plain", plain}, {"lines", jsonLines(lines)}, {"source", "LRCLIB"},
            {"matchesCurrent", exact >= 0}, {"durationFits", durationFits},
            {"durationDelta", duration > 0 && length > 0 ? length-duration : 0}}, rank});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return a.rank < b.rank; });
    QJsonArray results;
    for (int i=0; i<std::min(limit, static_cast<int>(candidates.size())); ++i) results.append(candidates[i].record);
    return results;
}

struct Response { QJsonDocument document; QString error; };
static Response requestJson(const QUrl &url) {
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "AriLyrics/1.5.0 (personal Noctalia desktop; MusicBrainz metadata lookup)");
    request.setTransferTimeout(12000);
    auto reply = network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [reply] { if (reply->bytesAvailable() > 2*1024*1024) reply->abort(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) return {{}, "Источник временно недоступен"};
    auto raw = reply->readAll();
    if (raw.size() > 2*1024*1024) return {{}, "Ответ источника слишком большой"};
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError) return {{}, "Некорректный ответ источника"};
    return {document, {}};
}

static int output(const QJsonObject &object, int code=0) {
    std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).constData() << '\n';
    return code;
}

int catalogCommand(const QStringList &args) {
    const auto mode = args.value(1);
    if (mode == "--search") {
        if (args.size() < 3 || args[2].trimmed().isEmpty() || args[2].size() > 250 || (args.size()-3)%2 != 0)
            return output({{"error", "Укажи название песни или исполнителя"}, {"results", QJsonArray()}}, 2);
        QString title, artist; double duration=0;
        for (int i=3; i<args.size(); i+=2) {
            if (args[i]=="--title") title=args[i+1];
            else if (args[i]=="--artist") artist=args[i+1];
            else if (args[i]=="--duration") {
                bool ok=false; duration=args[i+1].toDouble(&ok);
                if (!ok || !std::isfinite(duration) || duration<0) return 2;
            } else return 2;
        }
        QUrl url("https://lrclib.net/api/search");
        QUrlQuery query; query.addQueryItem("q", args[2].trimmed()); url.setQuery(query);
        auto response = requestJson(url);
        auto records=response.document.array();
        // Full-text search can miss "title + artist" combinations. For that
        // default query also ask for explicit metadata, then merge by record ID.
        const auto defaultQuery=cleanTitle(title)+(artist.isEmpty()?QString():" "+artist);
        if (!title.isEmpty() && (args[2].simplified().compare(defaultQuery.simplified(),Qt::CaseInsensitive)==0 || records.isEmpty())) {
            QUrl structured("https://lrclib.net/api/search"); QUrlQuery filters;
            filters.addQueryItem("track_name",cleanTitle(title));
            if (!artist.isEmpty()) filters.addQueryItem("artist_name",normalizedArtist(artist));
            structured.setQuery(filters);
            const auto more=requestJson(structured);
            for(const auto &entry:more.document.array()) records.append(entry);
            if (!more.document.isNull()) response.error.clear();
        }
        auto results=rankedLyrics(records,title,artist,duration);
        if (!results.isEmpty()) response.error.clear();
        return output({{"results", results}, {"error", response.error}}, response.error.isEmpty()?0:1);
    }
    if (mode == "--choose-lyrics") {
        if (args.size() != 4 || !QRegularExpression("^[0-9a-f]{64}$").match(args[2]).hasMatch()
            || !QRegularExpression("^[0-9]{1,12}$").match(args[3]).hasMatch()) return 2;
        const auto response = requestJson(QUrl("https://lrclib.net/api/get/"+args[3]));
        if (!response.error.isEmpty()) return output({{"error", response.error}}, 1);
        auto record = response.document.object();
        if (record.value("trackName").toString().isEmpty() || record.value("artistName").toString().isEmpty())
            return output({{"error", "Запись не содержит названия и исполнителя"}}, 1);
        if (parseLrc(record.value("syncedLyrics").toString()).isEmpty() && record.value("plainLyrics").toString().isEmpty()
            && !record.value("instrumental").toBool()) return output({{"error", "В записи нет текста"}},1);
        record["_ariManual"] = true; record["_ariFor"] = args[2]; record["_ariSource"] = "LRCLIB";
        const auto path = overridePath(args[2]); QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return output({{"error", "Не удалось сохранить выбор"}}, 1);
        file.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
        const bool saved=file.commit();return output({{"ok", saved},{"id",record.value("id")},
            {"synced",!parseLrc(record.value("syncedLyrics").toString()).isEmpty()}},saved?0:1);
    }
    if (mode == "--clear-lyrics") {
        if (args.size() != 3 || !QRegularExpression("^[0-9a-f]{64}$").match(args[2]).hasMatch()) return 2;
        const auto path = overridePath(args[2]);
        return output({{"ok", !QFile::exists(path) || QFile::remove(path)}});
    }
    return -1;
}
