#pragma once
#include <QtCore>
#include <QtNetwork>
#include <functional>

bool workspaceFullscreen(const QJsonObject &workspace, const QJsonArray &clients);

class FullscreenMonitor : public QObject {
    QString socketDir;
    QLocalSocket events;
    QTimer debounce, reconcile, reconnect, refresh;
    QByteArray eventBuffer;
    bool busy = false, dirty = false, known = false, fullscreen = false;
    quint64 eventRevision = 0;
    qint64 trustAfter = 0;
    std::function<void()> changed;
    void request(const QByteArray &command, std::function<void(QJsonDocument)> callback);
    void schedule();
    void readState();
    void setState(bool valid, bool blocked);
public:
    explicit FullscreenMonitor(QObject *parent = nullptr);
    ~FullscreenMonitor() override;
    void onChange(std::function<void()> callback) { changed = std::move(callback); }
    bool isKnown() const { return known; }
    bool isFullscreen() const { return fullscreen; }
};
