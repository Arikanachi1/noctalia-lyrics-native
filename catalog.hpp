#pragma once
#include <QtCore>

QString normalizedArtist(QString artist);
QJsonObject matchRecording(const QJsonArray &recordings, const QString &title, const QString &artist, double duration);
QString overridePath(const QString &trackId);
QJsonArray rankedLyrics(const QJsonArray &records, const QString &title, const QString &artist, double duration, int limit=30);
int catalogCommand(const QStringList &args);
