/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include "rl26_user_content.h"

#include "rl26_save.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace Rl26UserContent {

namespace {

constexpr quint32 kPaSerialMagic = 0x1234EDBCu;
constexpr int kDecalSlots = 5000;
constexpr int kDecalHeaderSize = 121;
constexpr int kDecalNameSize = 64;

quint16 be16(const QByteArray& b, qsizetype at) {
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(b.constData()) + at);
}
quint32 be32(const QByteArray& b, qsizetype at) {
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()) + at);
}
quint16 le16(const QByteArray& b, qsizetype at) {
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(b.constData()) + at);
}
quint32 le32(const QByteArray& b, qsizetype at) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()) + at);
}

QString cString(const QByteArray& b, qsizetype at, int size) {
    const QByteArray raw = b.mid(at, size);
    const qsizetype end = raw.indexOf('\0');
    return QString::fromLatin1(end < 0 ? raw : raw.left(end)).trimmed();
}

// A decoded file, read again when it changes.
struct Cached {
    QDateTime stamp;
    qint64 size = -1;
    bool ok = false;
    QByteArray data;
};

bool loadFile(const QString& path, Cached& cache) {
    const QFileInfo info(path);
    if (!info.isFile()) {
        cache = Cached();
        return false;
    }
    if (cache.stamp == info.lastModified() && cache.size == info.size())
        return cache.ok;
    cache = Cached();
    cache.stamp = info.lastModified();
    cache.size = info.size();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const Rl26Save::File f = Rl26Save::decode(file.readAll());
    cache.ok = f.ok;
    cache.data = f.data;
    return cache.ok;
}

// --- Decals ------------------------------------------------------------------------------------------------------

struct DecalFile {
    Cached file;
    QList<Item> items;
    QHash<quint32, QByteArray> blobs;   // From the u32 size on.
    bool parsed = false;
};

QHash<QString, DecalFile>& decalCache() {
    static QHash<QString, DecalFile> cache;
    return cache;
}

QString decalPath(const QString& savePath) {
    return QDir(gameFolder(savePath)).filePath(u"Decals/DecalData.SAV"_s);
}

const DecalFile& decalFile(const QString& savePath) {
    const QString path = decalPath(savePath);
    DecalFile& df = decalCache()[path];
    const QDateTime before = df.file.stamp;
    const qint64 sizeBefore = df.file.size;
    const bool ok = loadFile(path, df.file);
    if (df.parsed && df.file.stamp == before && df.file.size == sizeBefore)
        return df;
    df.parsed = true;
    df.items.clear();
    df.blobs.clear();
    if (!ok)
        return df;
    const QByteArray& d = df.file.data;
    // The paSerial root: its type table, then the root object.
    if (d.size() < 8 || le32(d, 0) != kPaSerialMagic)
        return df;
    const int types = le16(d, 6);
    qsizetype p = 8 + 6 * qsizetype(types);
    quint32 rootSize = 0;
    for (int t = 0; t < types; ++t) {
        if (p + 6 > d.size())
            return df;
        const int members = le16(d, p);
        if (t == 0)
            rootSize = le32(d, p + 2);
        p += 6 + 12 * qsizetype(members);
    }
    const qsizetype headers = p + rootSize;
    const qsizetype index = headers + qsizetype(kDecalSlots) * kDecalHeaderSize;
    const qsizetype blobs = index + qsizetype(kDecalSlots) * 8;
    if (blobs > d.size())
        return df;
    QHash<quint32, quint32> offsets;
    for (int i = 0; i < kDecalSlots; ++i) {
        const quint32 id = le32(d, index + 8 * i);
        if (id != 0)
            offsets.insert(id, le32(d, index + 8 * i + 4));
    }
    for (int i = 0; i < kDecalSlots; ++i) {
        const qsizetype at = headers + qsizetype(i) * kDecalHeaderSize;
        const quint32 id = le32(d, at);
        if (id == 0 || !offsets.contains(id))
            continue;
        const qsizetype blob = blobs + offsets.value(id);
        if (blob + 8 > d.size())
            continue;
        const quint32 size = le32(d, blob);
        if (size < 5 || blob + 8 + size > d.size())
            continue;
        QString name = cString(d, at + 4, kDecalNameSize);
        if (name.isEmpty())
            name = u"Custom"_s;
        df.items << Item{id, name};
        df.blobs.insert(id, d.mid(blob, 8 + size));
    }
    return df;
}

// --- Levels -------------------------------------------------------------------------------------------------------

struct LevelFile {
    Cached file;
    QList<Item> items;
    bool parsed = false;
};

QHash<QString, LevelFile>& levelCache() {
    static QHash<QString, LevelFile> cache;
    return cache;
}

bool printableName(const QByteArray& d, qsizetype at) {
    if (at + kDecalNameSize > d.size() || !QChar::fromLatin1(d.at(at)).isLetterOrNumber())
        return false;
    int i = 0;
    for (; i < kDecalNameSize && d.at(at + i) != '\0'; ++i)
        if (uchar(d.at(at + i)) < 32 || uchar(d.at(at + i)) > 126)
            return false;
    for (; i < kDecalNameSize; ++i)
        if (d.at(at + i) != '\0')
            return false;
    return true;
}

} // namespace

QString gameFolder(const QString& savePath) {
    if (!savePath.isEmpty()) {
        QDir dir = QFileInfo(savePath).absoluteDir();
        if (dir.cdUp() && (dir.exists(u"Decals"_s) || dir.exists(u"Levels"_s)))
            return dir.absolutePath();
    }
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir(home).filePath(u"Saved Games/Rugby League 26"_s);
}

QList<Item> decals(const QString& savePath) {
    return decalFile(savePath).items;
}

QString decalName(const QString& savePath, quint32 id) {
    for (const Item& i : decalFile(savePath).items)
        if (i.id == id)
            return i.name;
    return {};
}

QImage renderDecalData(const QByteArray& data, int size) {
    if (data.size() < 13 || size <= 0)
        return {};
    const quint32 length = le32(data, 0);
    const QByteArray c = data.mid(8, qMin<qsizetype>(length, data.size() - 8));
    // The shapes, in 0..65535 units.
    struct Shape {
        QPainterPath path;
        QColor colour;
    };
    QList<Shape> shapes;
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    bool ended = false;
    QPointF at;
    qsizetype p = 5;
    while (p < c.size()) {
        const quint8 op = quint8(c.at(p++));
        switch (op) {
        case 0x02:
            path = QPainterPath();
            path.setFillRule(Qt::OddEvenFill);
            ended = false;
            break;
        case 0x08:
            if (p + 4 > c.size())
                return {};
            at = QPointF(be16(c, p), be16(c, p + 2));
            path.moveTo(at);
            p += 4;
            break;
        case 0x0C: {
            if (p + 12 > c.size())
                return {};
            const QPointF end(be16(c, p), be16(c, p + 2));
            const QPointF c1(be16(c, p + 4), be16(c, p + 6));
            const QPointF c2(be16(c, p + 8), be16(c, p + 10));
            path.cubicTo(c1, c2, end);
            at = end;
            p += 12;
            break;
        }
        case 0x03:
            path.closeSubpath();
            ended = true;
            break;
        case 0x06: {
            if (p + 4 > c.size())
                return {};
            const QColor colour(quint8(c.at(p)), quint8(c.at(p + 1)), quint8(c.at(p + 2)), quint8(c.at(p + 3)));
            p += 4;
            if (ended && !path.isEmpty())
                shapes << Shape{path, colour};
            path = QPainterPath();
            path.setFillRule(Qt::OddEvenFill);
            ended = false;
            break;
        }
        case 0x01: case 0x07: case 0x09: case 0x0D: case 0x0E:
            break;
        default:
            p = c.size();   // Something new: draw what's been read.
            break;
        }
    }
    if (shapes.isEmpty())
        return {};
    QRectF bounds;
    for (const Shape& s : shapes)
        bounds |= s.path.boundingRect();
    if (bounds.width() <= 0 || bounds.height() <= 0)
        return {};
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const double margin = size * 0.04;
    const double scale = qMin((size - 2 * margin) / bounds.width(), (size - 2 * margin) / bounds.height());
    painter.translate(size / 2.0, size / 2.0);
    painter.scale(scale, scale);
    painter.translate(-bounds.center());
    painter.setPen(Qt::NoPen);
    for (const Shape& s : shapes)
        painter.fillPath(s.path, s.colour);
    painter.end();
    return image;
}

QImage renderDecal(const QString& savePath, quint32 id, int size) {
    return renderDecalData(decalFile(savePath).blobs.value(id), size);
}

QString decalImageUrl(const QString& savePath, quint32 id, int size) {
    const DecalFile& df = decalFile(savePath);
    const QByteArray blob = df.blobs.value(id);
    if (blob.isEmpty())
        return {};
    // Named by the logo's data, so an edited logo gets a new image (the Image doesn't cache a stale one).
    const QString key = QString::fromLatin1(QCryptographicHash::hash(blob, QCryptographicHash::Md5).toHex().left(16));
    const QString dir = QDir(QDir::tempPath()).filePath(u"blobset_tools_decals"_s);
    const QString file = QDir(dir).filePath(u"%1_%2_%3.png"_s.arg(id).arg(key).arg(size));
    if (!QFileInfo::exists(file)) {
        const QImage image = renderDecalData(blob, size);
        if (image.isNull() || !QDir().mkpath(dir) || !image.save(file, "PNG"))
            return {};
    }
    return QUrl::fromLocalFile(file).toString();
}

QList<Item> customStadiums(const QString& savePath) {
    const QString path = QDir(gameFolder(savePath)).filePath(u"Levels/LevelData.SAV"_s);
    LevelFile& lf = levelCache()[path];
    const QDateTime before = lf.file.stamp;
    const qint64 sizeBefore = lf.file.size;
    const bool ok = loadFile(path, lf.file);
    if (lf.parsed && lf.file.stamp == before && lf.file.size == sizeBefore)
        return lf.items;
    lf.parsed = true;
    lf.items.clear();
    if (!ok)
        return lf.items;
    const QByteArray& d = lf.file.data;
    if (d.size() < 4 + 4 + kDecalNameSize)
        return lf.items;
    const quint32 count = be32(d, 0);
    if (count == 0 || count > 1000)
        return lf.items;
    // The first stadium follows the count; the others (their layouts' sizes vary) are found by their id + name.
    qsizetype at = 4;
    for (quint32 n = 0; n < count && at + 4 + kDecalNameSize <= d.size(); ++n) {
        if (n > 0) {
            qsizetype found = -1;
            for (qsizetype q = at; q + 4 + kDecalNameSize <= d.size(); ++q) {
                if (d.at(q) != 0 || d.at(q + 1) != 0)
                    continue;
                const quint32 id = be32(d, q);
                if (id >= 1024 && id < 0x10000 && printableName(d, q + 4)) {
                    found = q;
                    break;
                }
            }
            if (found < 0)
                break;
            at = found;
        }
        QString name = cString(d, at + 4, kDecalNameSize);
        lf.items << Item{be32(d, at), name.isEmpty() ? u"Custom Stadium"_s : name};
        at += 4 + kDecalNameSize;
    }
    return lf.items;
}

QString customStadiumName(const QString& savePath, quint32 id) {
    if (id == kNoLevel)
        return {};
    for (const Item& i : customStadiums(savePath))
        if (i.id == id)
            return i.name;
    return u"Custom Stadium"_s;
}

} // namespace Rl26UserContent
