#ifndef RESTORE_WORKER_H
#define RESTORE_WORKER_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QObject>
#include <QString>

/// Puts the game back the way it was before Modify / Create, from games/<game>/<platform>/backup:
///
///  - V1 / V2 Create: backup/<blobset>          the whole original blobset is copied back.
///  - V1 / V2 Modify: backup/<blobset>.header   the original header (and the game data a grown header covered,
///                                              <blobset>.spill) is written back and the mod container removed.
///  - V3 / V4 Modify: backup/<blobset>          the original header is copied back, backup/<folder>/<file> assets are
///                                              moved back and assets Modify added are deleted.
///  - V4 Mod Loader:  <game>/BlobsetMods        removed (the game's files were never changed).
///
/// The header backups are kept (they're the originals), so Modify keeps working afterwards.
class RestoreWorker : public QObject {
    Q_OBJECT

public:
    // version: CoreDefs::BlobsetVersion (0 = V1 ... 3 = V4)
    explicit RestoreWorker(const QString& blobsetFile, int version, bool isBigEndian, const QString& backupDir,
                           int gameId, QObject* parent = nullptr);
    ~RestoreWorker() override = default;

public slots:
    void restore_backups();

signals:
    void progressUpdated(int percentage);
    void updateConsole(const QString& message);
    void updateStatus(const QString& result);
    void taskFinished(const QString& message);
    void taskSucceeded();   // Right before taskFinished() when the task worked.

private:
    QString m_blobsetFile;
    int m_version;
    bool m_isBigEndian;
    QString m_backupDir;
    int m_gameId;

    int restoreV1();   // Returns the number of things restored.
    int restoreV3V4();
};

#endif // RESTORE_WORKER_H
