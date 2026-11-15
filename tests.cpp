#include "lyrics.hpp"
#include "catalog.hpp"
#include "playback.hpp"
#include "fullscreen.hpp"
#include <QCoreApplication>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocalServer>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <cmath>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int errors = 0;
    auto check = [&](bool ok, const char *name){if(!ok){std::cerr<<name<<" FAILED\n"; ++errors;}};
    auto lines=parseLrc("[ar:Ari]\n[offset:-250]\n[00:05.5][00:10.50]Вторая строка\n[00:01.00]Первая строка\n[00:12.00]\n[99:99]bad");
    check(lines.size()==4,"multiple stamps and invalid timestamp");
    check(lines[0].text=="Первая строка","UTF-8 and sort");
    check(std::abs(lines[0].seconds-.75)<.001,"offset");
    check(currentLine(lines,0)==-1,"intro");
    check(currentLine(lines,5.25)==1,"exact timestamp");
    check(currentLine(lines,10.3)==2,"repeat timestamp");
    check(currentLine(lines,1)==0,"seek backwards");
    check(currentLine(lines,15)==3 && lines[3].text.isEmpty(),"instrumental gap");
    check(currentLine({},100)==-1,"empty lyric");
    check(cleanTitle("Song (Official Music Video) [Lyrics]")=="Song","metadata cleanup");
    check(cleanTitle("Song (Live)")=="Song (Live)","keep version identity");
    check(jsonLines(lines)[0].toObject().value("text").toString()=="Первая строка","JSON Unicode");
    check(normalizedName("ＤＩＶＩＮＥ")==normalizedName("DIVINE"),"fullwidth title");
    check(normalizedName("しかばね の 踊り")==normalizedName("しかばねの踊り"),"Japanese spacing");
    QJsonObject record{{"trackName","DIVINE"},{"artistName","Dasu"},{"duration",184},{"syncedLyrics","[00:01.00]test"}};
    check(recordScore(record,"divine","dasu",185)>=0,"matching recording");
    check(recordScore(record,"DIVINE","different artist",185)<0,"wrong performer");
    check(recordScore(record,"DIVINE","Dasu",210)<0,"wrong video version");
    check(recordScore(record,"DIVINE (Live)","Dasu",185)<0,"keep live identity");
    check(recordScore(record,"DIVINE","Dasu - Topic",185)>=0,"YouTube Topic channel suffix");
    QJsonObject recording{{"id","fixture-recording"},{"title","DIVINE"},{"length",184000},
        {"artist-credit",QJsonArray{QJsonObject{{"name","Dasu"},{"artist",QJsonObject{{"name","Dasu"},{"sort-name","Dasu"}}}}}}};
    check(!matchRecording({recording},"DIVINE","Dasu - Topic",185).isEmpty(),"conservative MusicBrainz match");
    check(matchRecording({recording},"DIVINE","unrelated channel",185).isEmpty(),"do not identify by title only");
    check(matchRecording({recording},"DIVINE","Dasu",210).isEmpty(),"different MusicBrainz recording length");
    check(matchRecording({recording},"DIVINE","",185).isEmpty(),"missing artist remains ambiguous");
    check(isYouTubeMusic("https://music.youtube.com/watch?v=music"),"browser YouTube Music");
    check(!isYouTubeMusic("https://www.youtube.com/watch?v=video"),"ordinary YouTube excluded");
    check(!isYouTubeMusic("https://youtu.be/video","youtube-music","YouTube Music"),"ordinary video overrides app identity");
    check(!isYouTubeMusic("https://music.youtube.com.evil.example/watch?v=x"),"exact music origin");
    check(!isYouTubeMusic("","firefox","Firefox"),"unknown browser source excluded");
    check(isYouTubeMusic("","youtube-music-for-desktop.desktop",""),"dedicated desktop music app");
    check(isSupportedMusicSource("file:///music/song.mp3","wave","Wave"),"Wave local music");
    check(isSupportedMusicSource("https://www.youtube.com/watch?v=video","wave.desktop","Wave"),"Wave audio stream");
    check(!isSupportedMusicSource("https://www.youtube.com/watch?v=video","firefox","Wave"),"Wave name does not whitelist browser");
    check(!isSupportedMusicSource("file:///music/song.mp3","wave-other","Wave"),"exact Wave desktop identity");
    check(mediaIdentity("https://music.youtube.com/watch?v=one&list=first&t=3")==mediaIdentity("https://music.youtube.com/watch?v=one&list=second"),"playlist changes preserve song clock");
    PlaybackClock clock;
    clock.observe("song",10,true,true,1,0);
    clock.observe("song",10,true,true,1,4000);
    check(std::abs(clock.position(4500)-14.5)<.001 && clock.available(),"static MPRIS Position advances while playing");
    clock.seek(5,4500);clock.observe("song",10,true,true,1,4600);
    check(std::abs(clock.position(4700)-5.2)<.001,"Seeked survives a stale property read");
    clock.observe("song",5,true,true,1,4700);clock.observe("song",5,true,false,1,4800);
    check(std::abs(clock.position(10000)-5.1)<.001,"pause freezes the interpolated position");
    clock.observe("song",5,true,true,1,10000);
    check(std::abs(clock.position(11000)-6.1)<.001,"resume preserves position");
    clock.observe("song",5,false,true,2,11000);
    check(std::abs(clock.position(12000)-8.1)<.001,"rate changes preserve position and speed");
    clock.observe("next",0,false,true,1,13000);
    check(!clock.available() && clock.position(15000)==0,"new track requires an authoritative position");
    clock.observe("next",3,true,true,1,15000);clock.observe("next",0,false,true,1,16000);
    check(clock.available() && std::abs(clock.position(17000)-5)<.001,"temporary metadata loss keeps confirmed clock");
    QJsonObject ws{{"id",1}};
    QJsonObject window{{"workspace",QJsonObject{{"id",1}}},{"mapped",true},{"hidden",false},{"fullscreen",1}};
    check(!workspaceFullscreen(ws,{window}),"maximized remains visible");
    window["fullscreen"]=2;check(workspaceFullscreen(ws,{window}),"true fullscreen hides lyrics");
    window["hidden"]=true;check(!workspaceFullscreen(ws,{window}),"hidden client does not block lyrics");
    window["hidden"]=false;window["workspace"]=QJsonObject{{"id",2}};
    check(!workspaceFullscreen(ws,{window}),"fullscreen on another workspace does not block lyrics");
    QJsonArray candidates;
    for(int i=1;i<=50;++i) {
        auto candidate=record;candidate["id"]=i;candidate["duration"]=210+i;candidate["syncedLyrics"]="";candidate["plainLyrics"]="Fixture";
        candidates.append(candidate);
    }
    auto timed=record;timed["id"]=100;timed["duration"]=187; candidates.append(timed);
    auto exactPlain=record;exactPlain["id"]=101;exactPlain["duration"]=185;exactPlain["syncedLyrics"]="";exactPlain["plainLyrics"]="Fixture";candidates.append(exactPlain);
    candidates.append(timed);
    const auto ranked=rankedLyrics(candidates,"DIVINE","Dasu",185);
    check(ranked.size()==30 && ranked[0].toObject().value("id").toInt()==100,"timed matching recording ranks before truncation and plaintext");
    check(ranked[0].toObject().value("durationFits").toBool() && ranked[0].toObject().value("matchesCurrent").toBool(),"search reports actual recording match");
    check(recordScore(exactPlain,"DIVINE","Dasu",185)>recordScore(timed,"DIVINE","Dasu",185),"auto search favours timestamps within allowed duration");
    // Destroy a connected compositor monitor while its change callback and
    // sockets still exist: socket teardown must not invoke destroyed state.
    {
        QTemporaryDir runtime;
        const auto previousRuntime=qgetenv("XDG_RUNTIME_DIR");
        const auto previousSignature=qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8());
        qputenv("HYPRLAND_INSTANCE_SIGNATURE","teardown-fixture");
        const auto sockets=runtime.path()+"/hypr/teardown-fixture";
        QDir().mkpath(sockets);
        QLocalServer eventServer;
        check(eventServer.listen(sockets+"/.socket2.sock"),"monitor fixture socket");
        {
            FullscreenMonitor monitor;
            monitor.onChange([retained=QString(4096,QLatin1Char('x'))] { Q_UNUSED(retained); });
            QElapsedTimer wait;wait.start();
            while(!eventServer.hasPendingConnections() && wait.elapsed()<1000) {
                app.processEvents();QThread::msleep(1);
            }
            check(eventServer.hasPendingConnections(),"monitor fixture connected before teardown");
        }
        qputenv("XDG_RUNTIME_DIR",previousRuntime);
        qputenv("HYPRLAND_INSTANCE_SIGNATURE",previousSignature);
    }
    std::cout << (errors ? "FAILED" : "All lyrics tests passed") << '\n';
    return errors ? 1 : 0;
}
