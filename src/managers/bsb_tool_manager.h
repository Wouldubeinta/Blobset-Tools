#ifndef BSB_TOOL_MANAGER_H
#define BSB_TOOL_MANAGER_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include "txpk_tool_manager.h"

#include <QByteArray>
#include <QString>
#include <QVariantMap>

/// BSB Viewer (BSBViewer.qml) and BSB Creator (BSBCreator.qml), Blobset V1 games only. See bsb_file.h for the format.
///
/// A BSB is a Flash movie plus a TXPK with the movie's textures, so this is a TxpkToolManager: the textures are
/// browsed, previewed and extracted exactly like in the TXPK viewer, and the movie is added on top.
///
/// Extract All writes <name>.swf, the textures as .dds (at the paths the movie uses) and BSB_List.xml. The creator
/// rebuilds the BSB from that list: <name>.bsb (main section + vram section, the way Extract writes BSBs, which
/// Modify / Create split again by the BSB header) and <name>.xml with the blobset entry it replaces.
class BsbToolManager : public TxpkToolManager {
    Q_OBJECT
    QML_ELEMENT

    // { signature, compressed, version, fileLength, width, height, frameRate, frameCount, size }
    Q_PROPERTY(QVariantMap movieInfo READ movieInfo NOTIFY movieChanged)
    Q_PROPERTY(bool hasMovie READ hasMovie NOTIFY movieChanged)

public:
    explicit BsbToolManager(QObject* parent = nullptr);

    QVariantMap movieInfo() const { return m_movieInfo; }
    bool hasMovie() const { return !m_movie.isEmpty(); }

    // A .bsb on disk: main + vram in one file (Extract), or main with "<file>.vram" next to it (Save As).
    Q_INVOKABLE void loadBsb(const QString& bsbPath);
    Q_INVOKABLE void loadBsbFromBlobset(const QString& hexHash, const QString& workspacePath = QString());

    // --- Extraction ----------------------------------------------------------------------------------------
    Q_INVOKABLE void extractMovie(const QString& outPath);       // The Flash movie (.swf).
    Q_INVOKABLE void extractBsbAll(const QString& outDirPath);   // Movie + textures (.dds) + BSB_List.xml.

    // --- BSB Creator -----------------------------------------------------------------------------------------
    Q_INVOKABLE QVariantMap inspectBsbList(const QString& xmlPath, const QString& sourceDir = QString()) const;
    Q_INVOKABLE void createBsb(const QString& xmlPath, const QString& sourceDir, const QString& outBsbPath);

    // True when the loaded game uses Blobset V1 (the only games with BSBs).
    Q_INVOKABLE bool isV1Game() const;

signals:
    void movieChanged();

private:
    bool loadBsbBytes(const QByteArray& data, qint64 mainSize, const QString& sourceName);
    QString movieFileName() const;   // "<name>.swf"
    QString writeBsbList(const QString& outDir) const;

    QByteArray m_movie;
    QVariantMap m_movieInfo;
    QString m_baseName;              // Output names: "1019" for ui\bsb\1019.bsb.
};

#endif // BSB_TOOL_MANAGER_H
