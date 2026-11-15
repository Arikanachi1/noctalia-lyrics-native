#include "fullscreen.hpp"
#include <memory>

bool workspaceFullscreen(const QJsonObject &workspace, const QJsonArray &clients) {
    const auto id = workspace.value("id").toInt();
    for (const auto &value : clients) {
        const auto client = value.toObject();
        if (client.value("workspace").toObject().value("id").toInt() != id) continue;
        if (!client.value("mapped").toBool(true) || client.value("hidden").toBool()) continue;
        // Hyprland mode 1 is maximized; mode 2 (or 3) is true fullscreen.
        if ((client.value("fullscreen").toInt() & 2) != 0) return true;
    }
    return false;
}

FullscreenMonitor::FullscreenMonitor(QObject *parent) : QObject(parent) {
    const auto signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (signature.isEmpty()) return; // Unknown compositor state: fail closed.
    socketDir = qEnvironmentVariable("XDG_RUNTIME_DIR") + "/hypr/" + signature;
    if (!QFileInfo::exists(socketDir)) socketDir = "/tmp/hypr/" + signature;
    debounce.setSingleShot(true); debounce.setInterval(8);
    reconcile.setSingleShot(true); reconcile.setInterval(90);
    connect(&debounce, &QTimer::timeout, this, [this] { readState(); });
    connect(&reconcile, &QTimer::timeout, this, [this] { schedule(); });
    connect(&events, &QLocalSocket::connected, this, [this] { eventBuffer.clear(); ++eventRevision; schedule(); });
    connect(&events, &QLocalSocket::disconnected, this, [this] { ++eventRevision; setState(false, true); });
    connect(&events, &QLocalSocket::readyRead, this, [this] {
        eventBuffer += events.readAll();
        if (eventBuffer.size() > 1024 * 1024) { eventBuffer.clear(); ++eventRevision; setState(false, true); schedule(); return; }
        bool update = false;
        while (eventBuffer.contains('\n')) {
            const auto end = eventBuffer.indexOf('\n');
            const auto line = eventBuffer.left(end); eventBuffer.remove(0, end + 1);
            const auto name = line.left(line.indexOf(">>"));
            if (name == "fullscreen" || name.startsWith("workspace") || name.startsWith("activespecial")
                || name.startsWith("movewindow") || name.startsWith("activewindow")
                || name.startsWith("openwindow") || name.startsWith("closewindow") || name == "focusedmon") {
                ++eventRevision; update = true;
                // Block immediately while a fullscreen-enter snapshot commits.
                if (name == "fullscreen" && line.mid(line.indexOf(">>") + 2).trimmed() == "1") {
                    trustAfter = QDateTime::currentMSecsSinceEpoch() + 80;
                    setState(false, true);
                }
            }
        }
        if (update) { schedule(); reconcile.start(); }
    });
    reconnect.setInterval(2000);
    connect(&reconnect, &QTimer::timeout, this, [this] {
        if (events.state() == QLocalSocket::UnconnectedState) events.connectToServer(socketDir + "/.socket2.sock");
    });
    refresh.setInterval(15000);
    connect(&refresh, &QTimer::timeout, this, [this] { schedule(); });
    events.connectToServer(socketDir + "/.socket2.sock"); reconnect.start(); refresh.start(); schedule();
}

FullscreenMonitor::~FullscreenMonitor() {
    // Disconnect while all captured state is alive. QLocalSocket's destructor
    // can emit disconnected after later-declared members have been destroyed.
    changed = {};
    debounce.stop(); reconcile.stop(); reconnect.stop(); refresh.stop();
    events.disconnect(this);
    events.abort();
    for (auto *socket : findChildren<QLocalSocket *>()) {
        socket->disconnect();
        for (auto *timer : socket->findChildren<QTimer *>()) timer->stop();
        socket->abort();
    }
}

void FullscreenMonitor::setState(bool valid, bool blocked) {
    if (valid == known && blocked == fullscreen) return;
    known = valid; fullscreen = blocked;
    if (changed) changed();
}

void FullscreenMonitor::schedule() {
    if (busy) { dirty = true; return; }
    if (!debounce.isActive()) debounce.start();
}

void FullscreenMonitor::request(const QByteArray &command, std::function<void(QJsonDocument)> callback) {
    auto socket = new QLocalSocket(this);
    auto timeout = new QTimer(socket); timeout->setSingleShot(true); timeout->setInterval(1200);
    auto buffer = std::make_shared<QByteArray>();
    auto complete = std::make_shared<bool>(false);
    auto finish = [socket, timeout, complete, callback](QJsonDocument result) {
        if (*complete) return;
        *complete = true; timeout->stop(); socket->abort(); socket->deleteLater(); callback(result);
    };
    connect(timeout, &QTimer::timeout, socket, [finish] { finish({}); });
    connect(socket, &QLocalSocket::errorOccurred, socket, [finish](QLocalSocket::LocalSocketError) { finish({}); });
    connect(socket, &QLocalSocket::connected, socket, [socket, command] { socket->write(command); socket->flush(); });
    connect(socket, &QLocalSocket::readyRead, socket, [socket, buffer, finish] {
        *buffer += socket->readAll();
        if (buffer->size() > 4 * 1024 * 1024) { finish({}); return; }
        QJsonParseError error; auto result = QJsonDocument::fromJson(*buffer, &error);
        if (error.error == QJsonParseError::NoError) finish(result);
    });
    connect(socket, &QLocalSocket::disconnected, socket, [buffer, finish] { finish(QJsonDocument::fromJson(*buffer)); });
    timeout->start(); socket->connectToServer(socketDir + "/.socket.sock");
}

void FullscreenMonitor::readState() {
    if (busy) { dirty = true; return; }
    busy = true; dirty = false;
    const auto revision = eventRevision;
    auto done = [this, revision](bool valid, bool blocked) {
        // Never publish a stale "not fullscreen" response after a newer event.
        if (revision != eventRevision) dirty = true;
        else if (events.state() != QLocalSocket::ConnectedState) setState(false, true);
        else if (!blocked && QDateTime::currentMSecsSinceEpoch() < trustAfter) setState(false, true);
        else setState(valid, blocked);
        busy = false; if (dirty) schedule();
    };
    request("j/activeworkspace", [this, done](QJsonDocument workspace) {
        if (!workspace.isObject()) { done(false, true); return; }
        request("j/clients", [this, workspace, done](QJsonDocument clients) {
            if (!clients.isArray()) { done(false, true); return; }
            request("j/activewindow", [workspace, clients, done](QJsonDocument active) {
                if (!active.isObject()) { done(false, true); return; }
                // The active client may be on a visible special workspace.
                const auto window=active.object();
                const bool focusedFullscreen=window.value("mapped").toBool(true) && !window.value("hidden").toBool()
                    && (window.value("fullscreen").toInt() & 2) != 0;
                done(true, focusedFullscreen || workspaceFullscreen(workspace.object(), clients.array()));
            });
        });
    });
}
