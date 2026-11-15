#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
struct LyricLine { double seconds; QString text; };
QVector<LyricLine> parseLrc(const QString &text);
int currentLine(const QVector<LyricLine> &lines, double seconds);
QString cleanTitle(QString title);
QJsonArray jsonLines(const QVector<LyricLine> &lines);
QString normalizedName(QString name);
double recordScore(const QJsonObject &record, const QString &title, const QString &artist, double duration);
