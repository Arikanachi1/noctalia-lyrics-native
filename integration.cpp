#include <QtCore>
#include <QtDBus>
#include <QtNetwork>
#include <iostream>

// Entirely private D-Bus, compositor sockets and original fixture lyrics.
// No real media playback, desktop windows or internet access.
class Fixture : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ status)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double Rate READ rate)
public:
    QString playback="Playing", title="Test track", url="https://music.youtube.com/watch?v=test";
    qlonglong pos=1000000;
    bool provideTiming=true;
    QString status() const {return playback;}
    qlonglong position() const {return pos;}
    double rate() const {return 1;}
    QVariantMap metadata() const {
        QVariantMap map{{"xesam:title",title},{"xesam:artist",QStringList{"Ari"}},
            {"xesam:album","Test album"},{"mpris:length",qlonglong(20000000)},{"xesam:url",url}};
        if(!provideTiming) map.remove("mpris:length");
        return map;
    }
signals:
    void Seeked(qlonglong position);
};

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2) return 2;
    QTemporaryDir temp;
    const auto cache=temp.path()+"/ari-lyrics/tracks"; QDir().mkpath(cache);
    const auto lrc="[00:00.00]A quiet test begins\n[00:03.00]The next test line\n[00:10.00]A later test line";
    auto record=[&](QString title,QJsonObject body) {
        body["trackName"]=title;body["artistName"]="Ari";body["duration"]=20;return body;
    };
    auto save=[&](QString title,int duration,QJsonObject body) {
        const auto key=(title+"\nAri\nTest album\n"+QString::number(duration)).toUtf8();
        const auto name=QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
        QFile file(cache+"/"+name+".json");
        if(!file.open(QIODevice::WriteOnly)) qFatal("Cannot write fixture cache");
        file.write(QJsonDocument(record(title,body)).toJson());
    };
    save("Test track",20,{{"syncedLyrics",lrc}});
    save("Clockless",0,{{"syncedLyrics",lrc}});
    save("Clockless",20,{{"syncedLyrics",lrc}});
    save("Signal start",0,{{"syncedLyrics",lrc}});
    save("Silence",20,{{"instrumental",true}});

    const auto runtime=temp.path()+"/runtime";
    const auto socketDir=runtime+"/hypr/lyrics-fixture";
    QDir().mkpath(socketDir);
    QLocalServer events, commands;
    if(!events.listen(socketDir+"/.socket2.sock") || !commands.listen(socketDir+"/.socket.sock")) return 2;
    QPointer<QLocalSocket> eventClient;
    int fullscreenMode=0;
    bool specialWorkspace=false;
    QObject::connect(&events,&QLocalServer::newConnection,&app,[&] {
        eventClient=events.nextPendingConnection();
        QObject::connect(eventClient,&QLocalSocket::disconnected,eventClient,&QObject::deleteLater);
    });
    QObject::connect(&commands,&QLocalServer::newConnection,&app,[&] {
        auto socket=commands.nextPendingConnection();
        QObject::connect(socket,&QLocalSocket::readyRead,socket,[&,socket] {
            const auto command=socket->readAll();
            QJsonDocument result;
            if(command=="j/activeworkspace") result=QJsonDocument(QJsonObject{{"id",1},{"hasfullscreen",fullscreenMode!=0}});
            else if(command=="j/clients") result=QJsonDocument(QJsonArray{QJsonObject{
                {"workspace",QJsonObject{{"id",specialWorkspace?-99:1}}},{"mapped",true},{"hidden",false},{"fullscreen",fullscreenMode}}});
            else if(command=="j/activewindow") result=QJsonDocument(QJsonObject{
                {"workspace",QJsonObject{{"id",specialWorkspace?-99:1}}},{"mapped",true},{"hidden",false},{"fullscreen",fullscreenMode}});
            else qFatal("Unexpected compositor request");
            socket->write(result.toJson(QJsonDocument::Compact));socket->flush();
        });
        QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
    });
    auto fullscreenEvent=[&](int mode) {
        fullscreenMode=mode;
        if(!eventClient) qFatal("No compositor event subscriber");
        eventClient->write(mode==0 ? "fullscreen>>0\n" : "fullscreen>>1\n");eventClient->flush();
    };

    Fixture player;
    auto bus=QDBusConnection::sessionBus();
    const auto service="org.mpris.MediaPlayer2.youtube_test";
    if(!bus.registerService(service) || !bus.registerObject("/org/mpris/MediaPlayer2",&player,
        QDBusConnection::ExportAllProperties|QDBusConnection::ExportAllSignals)) return 2;
    QProcess process;
    auto env=QProcessEnvironment::systemEnvironment();
    env.insert("XDG_CACHE_HOME",temp.path());env.insert("XDG_CONFIG_HOME",temp.path());
    env.insert("XDG_RUNTIME_DIR",runtime);env.insert("HYPRLAND_INSTANCE_SIGNATURE","lyrics-fixture");
    process.setProcessEnvironment(env);
    QByteArray buffer;
    QMap<QString,bool> checks;
    for(const auto &name:{"initial","seek","paused","manual","offset","static-position","fullscreen",
        "fullscreen-paused","fullscreen-seek","maximized","ordinary-video","unavailable-clock","clock-recovered","instrumental","player-exit"})
        checks[name]=false;
    enum Phase {Initial,Seeking,Offset,Paused,Resume,Static,Full,FullPaused,FullSeek,Maximized,Video,Unknown,Recovered,Instrumental,Exit};
    Phase phase=Initial;
    QStringList observed;
    QObject::connect(&process,&QProcess::readyReadStandardOutput,&app,[&] {
        buffer+=process.readAllStandardOutput();
        while(buffer.contains('\n')) {
            const auto end=buffer.indexOf('\n');
            const auto obj=QJsonDocument::fromJson(buffer.left(end)).object();buffer.remove(0,end+1);
            const auto state=obj.value("state").toString();
            const auto position=obj.value("position").toDouble();
            const bool fs=obj.value("fullscreen").toBool(),fsKnown=obj.value("fullscreenKnown").toBool();
            observed.push_back(QString("phase=%1 state=%2 position=%3 timing=%4 fullscreen=%5/%6")
                .arg(phase).arg(state).arg(position).arg(obj.value("timingAvailable").toBool()).arg(fsKnown).arg(fs));
            if(phase==Initial && state=="synced" && obj.value("index").toInt()==0)checks["initial"]=true;
            if(phase==Seeking && state=="synced" && obj.value("index").toInt()==1)checks["seek"]=true;
            if(phase==Paused && !obj.value("playing").toBool() && state=="synced")checks["paused"]=true;
            if(obj.value("manualMatch").toBool())checks["manual"]=true;
            if(phase==Offset && obj.value("offset").toDouble()==2 && obj.value("index").toInt()==0)checks["offset"]=true;
            if(phase==Static && state=="synced" && position>7.8 && obj.value("timingAvailable").toBool())checks["static-position"]=true;
            if(phase==Full && fsKnown && fs)checks["fullscreen"]=true;
            if(phase==FullPaused && fsKnown && fs && !obj.value("playing").toBool())checks["fullscreen-paused"]=true;
            if(phase==FullSeek && fsKnown && fs && obj.value("index").toInt()==2)checks["fullscreen-seek"]=true;
            if(phase==Maximized && fsKnown && !fs)checks["maximized"]=true;
            if(phase==Video && state=="idle" && !obj.value("musicEligible").toBool() && obj.value("lines").toArray().isEmpty())checks["ordinary-video"]=true;
            if(phase==Unknown && state=="untimed" && !obj.value("timingAvailable").toBool()
                && obj.value("lines").toArray().isEmpty() && !obj.value("plain").toString().isEmpty())checks["unavailable-clock"]=true;
            if(phase==Recovered && state=="synced" && obj.value("title")=="Signal start"
                && position>=2 && obj.value("timingAvailable").toBool())checks["clock-recovered"]=true;
            if(phase==Instrumental && state=="instrumental")checks["instrumental"]=true;
            if(phase==Exit && state=="idle")checks["player-exit"]=true;
        }
    });
    process.start(argv[1]);
    const auto id=QString::fromLatin1(QCryptographicHash::hash("Test track\nAri\nTest album",QCryptographicHash::Sha256).toHex());
    const auto choiceDir=temp.path()+"/ari-lyrics/overrides";QDir().mkpath(choiceDir);
    QTimer::singleShot(350,Qt::PreciseTimer,&app,[&] {
        QFile file(choiceDir+"/"+id+".json");if(!file.open(QIODevice::WriteOnly))qFatal("Cannot write chosen lyrics");
        file.write(QJsonDocument(record("Test track",{{"id",123},{"syncedLyrics",lrc},{"_ariManual",true},{"_ariFor",id}})).toJson());
    });
    auto setOffset=[&](QString delta) {
        QProcess adjustment;adjustment.setProcessEnvironment(env);adjustment.start(argv[1],{"--adjust-offset",id,delta});
        if(!adjustment.waitForFinished(1000) || adjustment.exitCode()!=0)qFatal("Offset fixture failed");
    };
    QTimer::singleShot(650,Qt::PreciseTimer,&app,[&] {phase=Seeking;player.pos=4000000;emit player.Seeked(player.pos);});
    QTimer::singleShot(1050,Qt::PreciseTimer,&app,[&] {phase=Offset;setOffset("2");});
    QTimer::singleShot(1550,Qt::PreciseTimer,&app,[&] {phase=Paused;player.playback="Paused";});
    QTimer::singleShot(1900,Qt::PreciseTimer,&app,[&] {setOffset("-2");});
    QTimer::singleShot(2200,Qt::PreciseTimer,&app,[&] {phase=Resume;player.pos=5000000;player.playback="Playing";QFile::remove(choiceDir+"/"+id+".json");});
    QTimer::singleShot(3700,Qt::PreciseTimer,&app,[&] {phase=Static;});
    QTimer::singleShot(5300,Qt::PreciseTimer,&app,[&] {phase=Full;specialWorkspace=true;fullscreenEvent(2);});
    QTimer::singleShot(5700,Qt::PreciseTimer,&app,[&] {phase=FullPaused;player.playback="Paused";});
    QTimer::singleShot(6000,Qt::PreciseTimer,&app,[&] {phase=FullSeek;player.playback="Playing";player.pos=12000000;emit player.Seeked(player.pos);});
    QTimer::singleShot(6400,Qt::PreciseTimer,&app,[&] {phase=Maximized;specialWorkspace=false;fullscreenEvent(1);});
    QTimer::singleShot(6800,Qt::PreciseTimer,&app,[&] {phase=Video;player.url="https://www.youtube.com/watch?v=ordinary";});
    QTimer::singleShot(7200,Qt::PreciseTimer,&app,[&] {phase=Unknown;player.url="https://music.youtube.com/watch?v=clockless";player.title="Clockless";player.provideTiming=false;player.pos=8;});
    QTimer::singleShot(7800,Qt::PreciseTimer,&app,[&] {phase=Recovered;player.title="Signal start";emit player.Seeked(2000000);});
    QTimer::singleShot(8200,Qt::PreciseTimer,&app,[&] {phase=Instrumental;player.provideTiming=true;player.title="Silence";});
    QTimer::singleShot(8600,Qt::PreciseTimer,&app,[&] {phase=Exit;bus.unregisterService(service);});
    QTimer::singleShot(9300,Qt::PreciseTimer,&app,[&] {process.terminate();process.waitForFinished(1000);app.quit();});
    app.exec();
    bool pass=true;
    for(auto it=checks.cbegin();it!=checks.cend();++it) {
        std::cout<<it.key().toStdString()<<'='<<it.value()<<' ';pass=pass&&it.value();
    }
    std::cout<<'\n';
    if(!pass)std::cerr<<observed.join('\n').toStdString()<<'\n'<<process.readAllStandardError().constData();
    return pass?0:1;
}
#include "integration.moc"
