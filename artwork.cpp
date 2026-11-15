#include "artwork.hpp"
void Artwork::setSource(const QString &value) {
    if(source==value)return;
    source=value;
    if(pending){pending->abort();pending=nullptr;}
    current.clear();
    const QUrl url(value);
    if(url.isLocalFile()) {
        const QFileInfo file(url.toLocalFile());
        if(file.isFile() && file.isReadable() && file.size()<=2*1024*1024)current=file.absoluteFilePath();
        return;
    }
    if(url.scheme()!="https" && url.scheme()!="http")return;
    const auto base=directory+"/"+QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(),QCryptographicHash::Sha256).toHex());
    for(const auto &suffix:{".png",".jpg",".webp"}) {
        if(QFileInfo::exists(base+suffix)){current=base+suffix;return;}
    }
    QNetworkRequest request(url);request.setTransferTimeout(12000);
    request.setRawHeader("User-Agent","AriLyrics/1.7.0");
    auto reply=network.get(request);pending=reply;
    connect(reply,&QNetworkReply::readyRead,this,[reply]{if(reply->bytesAvailable()>2*1024*1024)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,value,base] {
        reply->deleteLater();if(source!=value)return;
        pending=nullptr;
        const auto bytes=reply->readAll();
        if(reply->error()!=QNetworkReply::NoError || bytes.size()>2*1024*1024)return;
        QString suffix;
        if(bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))suffix=".png";
        else if(bytes.startsWith(QByteArray::fromHex("ffd8ff")))suffix=".jpg";
        else if(bytes.startsWith("RIFF") && bytes.mid(8,4)=="WEBP")suffix=".webp";
        if(suffix.isEmpty())return;
        QDir().mkpath(directory);QSaveFile file(base+suffix);
        if(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit()) {
            current=base+suffix;if(changed)changed();
        }
    });
}
