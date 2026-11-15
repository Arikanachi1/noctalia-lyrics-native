#include "lyrics.hpp"
#include "catalog.hpp"
#include "playback.hpp"
#include "fullscreen.hpp"
#include "artwork.hpp"
#include <QtCore>
#include <QtDBus>
#include <QtNetwork>
#include <csignal>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <functional>
#include <sys/prctl.h>
#include <unistd.h>

struct Track {
    QString service,title,artist,album,status,url,artUrl;
    double duration=0, position=0, rate=1;
    bool timingAvailable=false;
    QString key() const {return title+"\n"+artist+"\n"+album+"\n"+QString::number(qRound(duration));}
    QString id() const {return QString::fromLatin1(QCryptographicHash::hash((title+"\n"+artist+"\n"+album).toUtf8(),QCryptographicHash::Sha256).toHex());}
};
static QString offsetPath() {return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/offsets.json";}
static QJsonObject readOffsets() {
    QFile file(offsetPath());if(!file.open(QIODevice::ReadOnly) || file.size()>1024*1024)return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
static int adjustOffset(const QString &id, const QString &change) {
    if(!QRegularExpression("^[0-9a-f]{64}$").match(id).hasMatch())return 2;
    bool ok=false;double delta=change.toDouble(&ok);if(!ok || !std::isfinite(delta))return 2;
    QDir().mkpath(QFileInfo(offsetPath()).absolutePath());
    QLockFile lock(offsetPath()+".lock");if(!lock.tryLock(1000))return 1;
    auto values=readOffsets();double next=values.value(id).toDouble()+delta;
    values[id]=std::clamp(next,-10.0,10.0);
    QSaveFile file(offsetPath());if(!file.open(QIODevice::WriteOnly))return 1;
    file.write(QJsonDocument(values).toJson(QJsonDocument::Compact));return file.commit()?0:1;
}
static QVariant unwrap(QVariant v) {
    if(v.metaType()==QMetaType::fromType<QDBusVariant>()) return v.value<QDBusVariant>().variant();
    return v;
}
static QVariantMap asMap(QVariant v) {
    v=unwrap(v);
    if(v.metaType()==QMetaType::fromType<QDBusArgument>()) return qdbus_cast<QVariantMap>(v.value<QDBusArgument>());
    return v.toMap();
}
class Engine : public QObject {
    Q_OBJECT
    QNetworkAccessManager network;
    FullscreenMonitor fullscreen;
    Artwork artwork{QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/art"};
    QTimer timer;
    Track track;
    QVector<LyricLine> lyrics;
    QString plain, message="Включи музыку в Wave или YouTube Music", key, cacheDir;
    QString state="idle";
    QByteArray previous;
    QStringList services;
    QElapsedTimer scanClock;
    QElapsedTimer monotonic;
    PlaybackClock clock;
    QString positionTrack;
    QString seekService;
    QHash<QString,QVariantMap> playerInfo;
    double offsetSeconds=0;
    qint64 lastOffsetRead=0;
    quint64 generation=0;
    qint64 retryAfter=0;
    QPointer<QNetworkReply> pending;
    bool snapshotMode=false;
    bool polling=false;
    QString lookupTitle, lookupArtist, source="LRCLIB", matchedTitle, matchedArtist;
    qint64 matchedLyricsId=0;
    QJsonObject plainCandidate;
    bool metadataAttempted=false, plainAttempted=false, manualMatch=false, versionMismatch=false;
    qint64 overrideStamp=0, lastOverrideRead=0, lastMetadataRequest=0;
public:
    explicit Engine(bool snapshot=false): snapshotMode(snapshot) {
        monotonic.start();
        fullscreen.onChange([this]{emitState();});
        artwork.changed=[this]{emitState();};
        cacheDir=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/tracks";
        QDir().mkpath(cacheDir);
        timer.setInterval(160);
        timer.setTimerType(Qt::PreciseTimer);
        connect(&timer,&QTimer::timeout,this,[this]{poll();});
        timer.start(); poll();
    }
    QString cachePath() const {
        return cacheDir+"/"+QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),QCryptographicHash::Sha256).toHex())+".json";
    }
    Track activeTrack() {
        auto bus=QDBusConnection::sessionBus();
        if(!scanClock.isValid() || scanClock.elapsed()>5000) {
            services.clear();
            playerInfo.clear();
            if(bus.interface()) {
                auto reply=bus.interface()->registeredServiceNames();
                if(reply.isValid()) for(const auto &name:reply.value())
                    if(name.startsWith("org.mpris.MediaPlayer2.")) services.push_back(name);
            }
            scanClock.start();
        }
        Track best; int bestScore=-1;
        for(const auto &service:services) {
            auto call=QDBusMessage::createMethodCall(service,"/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","GetAll");
            call << "org.mpris.MediaPlayer2.Player";
            auto reply=bus.call(call,QDBus::Block,150);
            if(reply.type()==QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) continue;
            auto props=asMap(reply.arguments()[0]);
            auto meta=asMap(props.value("Metadata"));
            Track candidate;
            candidate.service=service;
            candidate.status=unwrap(props.value("PlaybackStatus")).toString();
            candidate.title=cleanTitle(unwrap(meta.value("xesam:title")).toString());
            auto artists=unwrap(meta.value("xesam:artist"));
            if(artists.metaType()==QMetaType::fromType<QDBusArgument>())
                candidate.artist=qdbus_cast<QStringList>(artists.value<QDBusArgument>()).join(", ");
            else candidate.artist=artists.toStringList().join(", ");
            candidate.album=unwrap(meta.value("xesam:album")).toString();
            candidate.url=unwrap(meta.value("xesam:url")).toString();
            candidate.artUrl=unwrap(meta.value("mpris:artUrl")).toString();
            if (!isSupportedMusicSource(candidate.url)) {
                if (!playerInfo.contains(service)) {
                    auto root=QDBusMessage::createMethodCall(service,"/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","GetAll");
                    root << "org.mpris.MediaPlayer2";
                    auto info=bus.call(root,QDBus::Block,150);
                    playerInfo[service]=info.arguments().isEmpty()?QVariantMap():asMap(info.arguments()[0]);
                }
                const auto info=playerInfo.value(service);
                if (!isSupportedMusicSource(candidate.url,unwrap(info.value("DesktopEntry")).toString(),unwrap(info.value("Identity")).toString())) continue;
            }
            candidate.rate=unwrap(props.value("Rate")).toDouble();
            candidate.duration=unwrap(meta.value("mpris:length")).toLongLong()/1000000.0;
            candidate.position=unwrap(props.value("Position")).toLongLong()/1000000.0;
            bool hasPosition=props.contains("Position");
            if(!hasPosition) {
                auto get=QDBusMessage::createMethodCall(service,"/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","Get");
                get << "org.mpris.MediaPlayer2.Player" << "Position";
                auto posReply=bus.call(get,QDBus::Block,150);
                if(posReply.type()!=QDBusMessage::ErrorMessage && !posReply.arguments().isEmpty()) {
                    candidate.position=unwrap(posReply.arguments()[0]).toLongLong()/1000000.0;
                    hasPosition=true;
                }
            }
            // Some browser players advertise Position but emit a tiny sentinel and
            // no duration. That is not an authoritative playback clock.
            candidate.timingAvailable=hasPosition && (candidate.duration>0 || candidate.position>=1);
            if(candidate.title.isEmpty() || candidate.status=="Stopped") continue;
            int score=candidate.status=="Playing" ? 100 : 0;
            if(service==track.service) score+=1;
            if(score>bestScore){best=candidate;bestScore=score;}
        }
        return best;
    }
    void poll() {
        if(polling)return;
        QScopedValueRollback<bool> guard(polling,true);
        Track next=activeTrack();
        const QString clockKey=next.service+"\n"+next.title+"\n"+next.artist+"\n"+next.album+"\n"+mediaIdentity(next.url);
        // Firefox can temporarily drop length after a seek. Keep the last
        // confirmed length for this exact media identity, avoiding cache churn.
        if(clockKey==positionTrack && next.duration<=0 && track.duration>0)next.duration=track.duration;
        if(clockKey==positionTrack && next.artUrl.isEmpty())next.artUrl=track.artUrl;
        artwork.setSource(next.artUrl);
        clock.observe(clockKey,next.position,next.timingAvailable,next.status=="Playing",next.rate,monotonic.elapsed());
        positionTrack=clockKey;
        next.timingAvailable=clock.available();
        if (next.service != seekService) {
            auto bus=QDBusConnection::sessionBus();
            if (!seekService.isEmpty()) bus.disconnect(seekService,"/org/mpris/MediaPlayer2","org.mpris.MediaPlayer2.Player","Seeked",this,SLOT(seeked(qlonglong)));
            seekService=next.service;
            if (!seekService.isEmpty()) bus.connect(seekService,"/org/mpris/MediaPlayer2","org.mpris.MediaPlayer2.Player","Seeked",this,SLOT(seeked(qlonglong)));
        }
        const QString nextKey=next.title.isEmpty()?QString():next.key();
        track=next;
        const auto now=QDateTime::currentMSecsSinceEpoch();
        if(nextKey!=key || now-lastOffsetRead>=300) {
            offsetSeconds=readOffsets().value(track.id()).toDouble();lastOffsetRead=now;
        }
        bool overrideChanged=false;
        if(nextKey!=key || now-lastOverrideRead>=500) {
            auto stamp=QFileInfo(overridePath(track.id())).lastModified().toMSecsSinceEpoch();
            overrideChanged=stamp!=overrideStamp;
            overrideStamp=stamp;lastOverrideRead=now;
        }
        if(nextKey!=key || overrideChanged) {
            ++generation;
            if(pending){pending->abort();pending=nullptr;}
            key=nextKey; lyrics.clear();plain.clear();retryAfter=0;
            lookupTitle=track.title;lookupArtist=normalizedArtist(track.artist);
            metadataAttempted=false;plainAttempted=false;manualMatch=false;versionMismatch=false;
            source="LRCLIB";matchedTitle.clear();matchedArtist.clear();matchedLyricsId=0;plainCandidate={};
            if(key.isEmpty()){state="idle";message="Включи музыку в Wave или YouTube Music";}
            else {
                state="loading";message="Ищу слова…";
                QFile chosen(overridePath(track.id()));
                QJsonObject chosenRecord;
                if(chosen.open(QIODevice::ReadOnly) && chosen.size()<2*1024*1024)
                    chosenRecord=QJsonDocument::fromJson(chosen.readAll()).object();
                if(chosenRecord.value("_ariFor").toString()==track.id() && chosenRecord.value("_ariManual").toBool()) {
                    load(chosenRecord);emitState();return;
                }
                QFile file(cachePath());
                bool cached=false;
                if(file.open(QIODevice::ReadOnly) && file.size()<2*1024*1024) {
                    auto doc=QJsonDocument::fromJson(file.readAll());
                    if(doc.isObject() && (recordScore(doc.object(),track.title,track.artist,track.duration)>=0
                        || (doc.object().value("_ariFor").toString()==track.id()
                            && std::abs(doc.object().value("_ariDuration").toDouble()-track.duration)<=3))
                        && (doc.object().contains("syncedLyrics") || doc.object().contains("plainLyrics") || doc.object().value("instrumental").toBool())) {load(doc.object());cached=true;}
                }
                if(!cached) fetch(false,generation);
                else if(state=="plain") {plainCandidate=docRecordFromCache();fetch(true,generation);}
            }
        } else if(state=="error" && !pending && QDateTime::currentMSecsSinceEpoch()>retryAfter) {
            state="loading";message="Повторный поиск…";fetch(false,generation);
        }
        emitState();
    }
    void load(const QJsonObject &obj) {
        matchedLyricsId=static_cast<qint64>(obj.value("id").toDouble());
        source=obj.value("_ariSource").toString("LRCLIB");
        matchedTitle=obj.value("trackName").toString();matchedArtist=obj.value("artistName").toString();
        manualMatch=obj.value("_ariManual").toBool();
        versionMismatch=manualMatch && track.duration>0 && obj.value("duration").toDouble()>0
            && std::abs(track.duration-obj.value("duration").toDouble())>3;
        lyrics=parseLrc(obj.value("syncedLyrics").toString());
        plain=obj.value("plainLyrics").toString();
        if(obj.value("instrumental").toBool()) {state="instrumental";message="Инструментальная композиция";}
        else if(!lyrics.isEmpty()){state="synced";message="Синхронизированный текст";}
        else if(!plain.isEmpty()){state="plain";message="Текст без временных меток";}
        else {state="missing";message="Для этого трека слова не найдены";}
    }
    QJsonObject docRecordFromCache() const {
        QFile file(cachePath());
        if(!file.open(QIODevice::ReadOnly) || file.size()>2*1024*1024)return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
    void cacheRecord(QJsonObject obj) {
        obj["_ariFor"]=track.id();obj["_ariDuration"]=track.duration;
        QSaveFile file(cachePath());
        if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));file.commit();}
    }
    void getJson(const QUrl &url, quint64 token, std::function<void(QJsonDocument)> finished) {
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent","AriLyrics/1.5.0 (personal Noctalia desktop client)");
        request.setTransferTimeout(12000);
        auto reply=network.get(request);pending=reply;
        connect(reply,&QNetworkReply::readyRead,this,[reply]{if(reply->bytesAvailable()>2*1024*1024)reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[this,reply,token,finished]{
            reply->deleteLater();if(token!=generation)return;pending=nullptr;
            const auto raw=reply->readAll();
            if(reply->error()!=QNetworkReply::NoError || raw.size()>2*1024*1024){finished({});return;}
            finished(QJsonDocument::fromJson(raw));
        });
    }
    void fallbackPlain(quint64 token) {
        if (!plainCandidate.isEmpty()) {load(plainCandidate);cacheRecord(plainCandidate);emitState();return;}
        if(plainAttempted || lookupArtist.isEmpty()) {state="missing";message="Слова не найдены · попробуй поиск и ручной выбор";emitState();return;}
        plainAttempted=true;
        QUrl url("https://api.lyrics.ovh/v1/"+QString::fromLatin1(QUrl::toPercentEncoding(lookupArtist))+"/"+QString::fromLatin1(QUrl::toPercentEncoding(lookupTitle)));
        getJson(url,token,[this](QJsonDocument doc){
            const auto text=doc.object().value("lyrics").toString();
            if(text.isEmpty()){state="missing";message="Слова не найдены · попробуй поиск и ручной выбор";}
            else {
                QJsonObject record{{"trackName",lookupTitle},{"artistName",lookupArtist},{"duration",track.duration},
                    {"plainLyrics",text},{"_ariSource","Lyrics.ovh"}};
                load(record);cacheRecord(record);
            }
            emitState();
        });
    }
    void fallbackMetadata(quint64 token) {
        if(metadataAttempted){fallbackPlain(token);return;}
        metadataAttempted=true;
        // MusicBrainz permits at most one request per second per application.
        const auto delay=std::max<qint64>(0,1100-(QDateTime::currentMSecsSinceEpoch()-lastMetadataRequest));
        QTimer::singleShot(delay,this,[this,token]{
            if(token!=generation)return;
            lastMetadataRequest=QDateTime::currentMSecsSinceEpoch();
            QString title=track.title;title.replace("\\","\\\\");title.replace("\"","\\\"");
            QUrl url("https://musicbrainz.org/ws/2/recording/");QUrlQuery query;
            query.addQueryItem("query","recording:\""+title+"\"");query.addQueryItem("fmt","json");query.addQueryItem("limit","25");url.setQuery(query);
            getJson(url,token,[this,token](QJsonDocument doc){
                auto match=matchRecording(doc.object().value("recordings").toArray(),track.title,track.artist,track.duration);
                if(match.isEmpty()){fallbackPlain(token);return;}
                lookupTitle=match.value("title").toString();lookupArtist=match.value("artist").toString();
                fetch(true,token);
            });
        });
    }
    void fetch(bool search, quint64 token) {
        QUrl url(search ? "https://lrclib.net/api/search" : "https://lrclib.net/api/get");
        QUrlQuery query;
        query.addQueryItem("track_name",cleanTitle(lookupTitle.normalized(QString::NormalizationForm_KC)));
        if(!lookupArtist.isEmpty()) query.addQueryItem("artist_name",lookupArtist.normalized(QString::NormalizationForm_KC));
        if(!search) {
            if(!track.album.isEmpty()) query.addQueryItem("album_name",track.album);
            if(track.duration>0) query.addQueryItem("duration",QString::number(qRound(track.duration)));
        }
        url.setQuery(query);
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent","AriLyrics/1.5.0 (Noctalia native desktop lyrics)");
        request.setTransferTimeout(12000);
        auto reply=network.get(request);pending=reply;
        // Bound untrusted response size. TLS verification stays enabled.
        connect(reply,&QNetworkReply::readyRead,this,[reply]{if(reply->bytesAvailable()>2*1024*1024)reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[this,reply,search,token]{
            reply->deleteLater();
            if(token!=generation) return;
            pending=nullptr;
            int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if((status==404 || status==400) && !search){fetch(true,token);return;}
            if((status==404 || status==400) && search){fallbackMetadata(token);return;}
            if(reply->error()!=QNetworkReply::NoError){
                if(!plainCandidate.isEmpty()){load(plainCandidate);emitState();return;}
                state="error";message="Не удалось загрузить слова — проверю снова через 30 с";
                retryAfter=QDateTime::currentMSecsSinceEpoch()+30000;emitState();return;
            }
            QByteArray raw=reply->readAll();
            if(raw.size()>2*1024*1024){state="error";message="Слишком большой ответ сервиса";retryAfter=QDateTime::currentMSecsSinceEpoch()+60000;emitState();return;}
            QJsonParseError error;auto doc=QJsonDocument::fromJson(raw,&error);
            if(error.error!=QJsonParseError::NoError || (!doc.isObject() && !doc.isArray())){
                state="error";message="Сервис прислал некорректный ответ";retryAfter=QDateTime::currentMSecsSinceEpoch()+60000;emitState();return;
            }
            QJsonObject selected;
            if(search) {
                double best=1e9;
                for(const auto &value:doc.array()) {
                    auto obj=value.toObject();
                    double rank=recordScore(obj,lookupTitle,lookupArtist,track.duration);
                    if(rank<0) continue;
                    if(rank<best){best=rank;selected=obj;}
                }
            } else {
                selected=doc.object();
                if(recordScore(selected,lookupTitle,lookupArtist,track.duration)<0) {fetch(true,token);return;}
            }
            if(selected.isEmpty()){fallbackMetadata(token);return;}
            else {
                if(selected.value("syncedLyrics").toString().isEmpty() && !selected.value("instrumental").toBool()
                    && !selected.value("plainLyrics").toString().isEmpty() && !search) {
                    plainCandidate=selected;load(selected);emitState();fetch(true,token);return;
                }
                load(selected);
                cacheRecord(selected);
            }
            emitState();
        });
    }
    void emitState() {
        bool canSync=track.timingAvailable && !versionMismatch;
        double position=clock.available()?clock.position(monotonic.elapsed()):track.position;
        if(track.duration>0)position=std::min(position,track.duration);
        int index=canSync ? currentLine(lyrics,position-offsetSeconds) : -1;
        QString line=index>=0?lyrics[index].text:QString();
        QString outputState=state, outputMessage=message, outputPlain=plain;
        QJsonArray outputLines=jsonLines(lyrics);
        if(state=="synced" && !canSync) {
            outputState=versionMismatch?"version_mismatch":"untimed";
            outputMessage=versionMismatch?"Другая длина записи · полный текст без синхронизации":"Плеер не сообщает время — показываю полный текст";
            if(outputPlain.isEmpty()) {
                QStringList words;for(const auto &l:lyrics)words.push_back(l.text);
                outputPlain=words.join('\n');
            }
            outputLines=QJsonArray();
        } else if(state=="synced" && line.isEmpty()) line="♪";
        QJsonObject obj{{"state",outputState},{"message",outputMessage},{"title",track.title},{"artist",track.artist},
            {"player",track.service},{"playing",track.status=="Playing"},{"position",position},
            {"duration",track.duration},{"index",index},{"line",line},{"plain",outputPlain},{"lines",outputLines},
            {"timingAvailable",track.timingAvailable},{"syncedAvailable",!lyrics.isEmpty()},{"trackId",track.id()},{"offset",offsetSeconds},
            {"source",source},{"manualMatch",manualMatch},{"matchedTitle",matchedTitle},{"matchedArtist",matchedArtist}};
        obj["musicEligible"]=!track.service.isEmpty();obj["url"]=track.url;obj["rate"]=track.rate;
        obj["fullscreenKnown"]=fullscreen.isKnown();obj["fullscreen"]=fullscreen.isFullscreen();
        obj["lyricsId"]=matchedLyricsId;
        obj["artPath"]=artwork.path();
        QByteArray json=QJsonDocument(obj).toJson(QJsonDocument::Compact);
        if(json!=previous){std::cout<<json.constData()<<'\n'<<std::flush;previous=json;}
        if(snapshotMode) QTimer::singleShot(0,qApp,&QCoreApplication::quit);
    }
public slots:
    void seeked(qlonglong position) {
        const auto sender=seekService;
        // A song-change Seeked can arrive before our next periodic read.
        // Bind the signal to the new metadata, not the previous song's clock.
        poll();
        if(track.service.isEmpty() || track.service!=sender)return;
        clock.seek(position/1000000.0,monotonic.elapsed());track.timingAvailable=clock.available();emitState();
    }
};
int main(int argc,char **argv) {
    const auto parent=getppid();prctl(PR_SET_PDEATHSIG,SIGTERM);
    if(getppid()!=parent)return 0;
    std::signal(SIGPIPE,SIG_IGN);
    QCoreApplication app(argc,argv);
    QCoreApplication::setApplicationName("ari-lyrics");
    if(app.arguments().contains("--help")) {
        std::cout<<"ari-lyrics [--snapshot] | --search QUERY [--title TITLE --artist ARTIST --duration SECONDS] | --choose-lyrics TRACK_ID LRCLIB_ID | --clear-lyrics TRACK_ID | --adjust-offset TRACK_ID SECONDS\nJSON-lines YouTube Music / MPRIS / LRCLIB native backend. Positive offset delays lyrics.\n";return 0;
    }
    const auto args=app.arguments();int adjust=args.indexOf("--adjust-offset");
    if(adjust>=0)return args.size()==adjust+3 ? adjustOffset(args[adjust+1],args[adjust+2]) : 2;
    if(args.value(1).startsWith("--") && args.value(1)!="--snapshot") {
        const int code=catalogCommand(args);return code>=0?code:2;
    }
    Engine engine(app.arguments().contains("--snapshot"));
    return app.exec();
}
#include "main.moc"
