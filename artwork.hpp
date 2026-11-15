#pragma once
#include <QtCore>
#include <QtNetwork>
#include <functional>
class Artwork : public QObject {
    QNetworkAccessManager network;
    QPointer<QNetworkReply> pending;
    QString directory,source,current;
public:
    explicit Artwork(QString cacheDirectory):directory(std::move(cacheDirectory)){}
    ~Artwork() override {if(pending)pending->disconnect(this);}
    std::function<void()> changed;
    QString path() const {return current;}
    void setSource(const QString &url);
};
