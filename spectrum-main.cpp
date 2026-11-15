#include "spectrum.hpp"
#include <QtCore>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sys/prctl.h>
#include <unistd.h>

// The realtime callback only copies into a lock-free bounded ring. FFT and
// JSON serialization run on the Qt thread, never the audio processing thread.
class Capture {
    static constexpr size_t ringSize=4096;
    std::array<std::atomic<float>,ringSize> ring{};
    std::atomic<uint64_t> written{0};
    std::atomic<bool> failed{false};
    pw_thread_loop *loop=nullptr;
    pw_stream *stream=nullptr;
    static void process(void *user) {
        auto &self=*static_cast<Capture*>(user);
        auto *buffer=pw_stream_dequeue_buffer(self.stream);
        if(!buffer)return;
        auto *spa=buffer->buffer;
        if(spa->n_datas && spa->datas[0].data && spa->datas[0].chunk) {
            const auto &data=spa->datas[0];
            const auto offset=std::min(data.chunk->offset,data.maxsize);
            const auto bytes=std::min(data.chunk->size,data.maxsize-offset);
            auto index=self.written.load(std::memory_order_relaxed);
            const auto *raw=static_cast<const unsigned char*>(data.data)+offset;
            for(size_t pos=0;pos+sizeof(float)<=bytes;pos+=sizeof(float)) {
                float value;std::memcpy(&value,raw+pos,sizeof(value));
                self.ring[index++%ringSize].store(value,std::memory_order_relaxed);
            }
            self.written.store(index,std::memory_order_release);
        }
        pw_stream_queue_buffer(self.stream,buffer);
    }
    static void stateChanged(void *user,pw_stream_state,pw_stream_state state,const char*) {
        if(state==PW_STREAM_STATE_ERROR)static_cast<Capture*>(user)->failed.store(true);
    }
public:
    bool start() {
        static_assert(std::atomic<float>::is_always_lock_free);
        if(loop)return !failed.load();
        failed=false;written=0;
        loop=pw_thread_loop_new("ari-lyrics-spectrum",nullptr);
        if(!loop)return false;
        if(pw_thread_loop_start(loop)<0){pw_thread_loop_destroy(loop);loop=nullptr;return false;}
        pw_thread_loop_lock(loop);
        static const pw_stream_events events=[] {
            pw_stream_events result{};result.version=PW_VERSION_STREAM_EVENTS;
            result.state_changed=stateChanged;result.process=process;return result;
        }();
        stream=pw_stream_new_simple(pw_thread_loop_get_loop(loop),"Ari Lyrics spectrum",
            pw_properties_new(PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,"Capture",
                PW_KEY_MEDIA_ROLE,"Music",PW_KEY_STREAM_CAPTURE_SINK,"true",
                PW_KEY_STREAM_MONITOR,"true",PW_KEY_NODE_PASSIVE,"true",nullptr),&events,this);
        bool ok=false;
        if(stream) {
            uint8_t storage[1024];spa_pod_builder builder=SPA_POD_BUILDER_INIT(storage,sizeof(storage));
            spa_audio_info_raw format{};format.format=SPA_AUDIO_FORMAT_F32;
            format.rate=48000;format.channels=1;format.position[0]=SPA_AUDIO_CHANNEL_MONO;
            const spa_pod *params[]={spa_format_audio_raw_build(&builder,SPA_PARAM_EnumFormat,&format)};
            ok=pw_stream_connect(stream,PW_DIRECTION_INPUT,PW_ID_ANY,
                pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT|PW_STREAM_FLAG_MAP_BUFFERS|PW_STREAM_FLAG_RT_PROCESS),params,1)>=0;
        }
        pw_thread_loop_unlock(loop);
        if(!ok)stop();
        return ok;
    }
    void stop() {
        if(!loop)return;
        pw_thread_loop_lock(loop);
        if(stream){pw_stream_destroy(stream);stream=nullptr;}
        pw_thread_loop_unlock(loop);pw_thread_loop_stop(loop);pw_thread_loop_destroy(loop);loop=nullptr;
        written=0;
    }
    bool healthy() const {return loop && !failed.load();}
    bool snapshot(std::array<float,SpectrumAnalyzer::windowSize> &out) const {
        const auto end=written.load(std::memory_order_acquire);
        if(end<out.size()){out.fill(0);return false;}
        for(size_t i=0;i<out.size();++i)out[i]=ring[(end-out.size()+i)%ringSize].load(std::memory_order_relaxed);
        return true;
    }
    ~Capture(){stop();}
};

int main(int argc,char **argv) {
    const auto parent=getppid();prctl(PR_SET_PDEATHSIG,SIGTERM);
    if(getppid()!=parent)return 0;
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=3 || args[1]!="--lease" || !QFileInfo(args[2]).isAbsolute()) {
        std::cerr<<"Usage: ari-lyrics-spectrum --lease /absolute/lease-file\n";return 2;
    }
    pw_init(nullptr,nullptr);
    {
        Capture capture;SpectrumAnalyzer analyzer;
        QTimer leaseTimer,frameTimer;
        QFileSystemWatcher leaseWatch;
        QElapsedTimer frameClock;
        frameTimer.setTimerType(Qt::PreciseTimer);
        bool leased=false;
        qint64 leaseUntil=0;
        SpectrumCaptureRetry retry;
        auto emitFrame=[](const std::array<float,SpectrumAnalyzer::bandCount> &bands,bool available,bool active) {
            QJsonArray values;for(auto value:bands)values.append(qRound(value*1000)/1000.);
            const QJsonObject message{{"bands",values},{"available",available},{"active",active}};
            std::cout<<QJsonDocument(message).toJson(QJsonDocument::Compact).constData()<<'\n'<<std::flush;
        };
        QObject::connect(&frameTimer,&QTimer::timeout,&app,[&] {
            std::array<float,2048> samples{};
            const bool available=capture.healthy() && capture.snapshot(samples);
            const double dt=std::clamp(frameClock.restart()/1000.,.001,.2);
            emitFrame(analyzer.analyze(samples,48000,dt),available,true);
        });
        auto checkLease=[&] {
            // Atomic replacement drops the file watch, so watch the parent too
            // and re-arm the file when it is created or replaced.
            if(!leaseWatch.files().contains(args[2]) && QFileInfo::exists(args[2]))leaseWatch.addPath(args[2]);
            const auto now=QDateTime::currentMSecsSinceEpoch();
            QFile file(args[2]);bool valid=false;qint64 expires=0;
            if(file.open(QIODevice::ReadOnly) && file.size()<=32)expires=file.readAll().trimmed().toLongLong(&valid);
            // Reject long-lived/malformed leases. A stalled panel cannot leave
            // a hidden capture running indefinitely.
            const bool fresh=valid && expires>now && expires-now<=3000;
            if(valid)leaseUntil=fresh?expires:0;
            // writeFile can notify after truncation but before its new deadline
            // is written. Keep the previous deadline during that gap; it can
            // never extend capture, and an explicit zero still stops at once.
            const bool wanted=fresh || (!valid && leased && QFileInfo::exists(args[2]) && leaseUntil>now && leaseUntil-now<=3000);
            const bool connectNow=retry.attempt(wanted,capture.healthy(),now);
            if(!wanted) {
                if(leased){frameTimer.stop();capture.stop();analyzer.reset();emitFrame({},false,false);}
                leased=false;leaseUntil=0;return;
            }
            if(connectNow) {
                capture.stop();capture.start();
            }
            if(!leased){analyzer.reset();frameClock.start();frameTimer.start(20);}
            leased=true;
        };
        QObject::connect(&leaseTimer,&QTimer::timeout,&app,checkLease);
        QObject::connect(&leaseWatch,&QFileSystemWatcher::fileChanged,&app,checkLease);
        QObject::connect(&leaseWatch,&QFileSystemWatcher::directoryChanged,&app,checkLease);
        leaseWatch.addPath(QFileInfo(args[2]).absolutePath());
        leaseTimer.start(250);checkLease();
        app.exec();
    }
    pw_deinit();return 0;
}
