#ifndef NAME_RESOLVER_WORKER_H
#define NAME_RESOLVER_WORKER_H

/*
 * Blobset Tools. Written by Wouldubeinta
 * Copyright (C) 2026 Wouldy Mods.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

/// Gives unknown / numbered files of the file mapping their real names.
///
/// The games address every asset by a hash of its name (see hash_utility.h), so a name can be confirmed by hashing it:
/// candidate names are collected from the game executables, from the strings inside the game's own assets
/// (materials, meshes, scripts, ...), from the names the mapping already has and from an optional user list
/// (games/<game>/<platform>/names.txt, one name per line). Every candidate is hashed with the asset types and
/// a numbered mapping entry with that hash gets renamed. Guessed types are only accepted when they fit what the
/// file is (a "model" has to be a .mesh / .mdl, ...), which keeps chance collisions of the 32 bit hashes out.
///
/// The mapping is rewritten in place (atomically); resolved_names.txt next to it lists what was renamed.
class NameResolverWorker : public QObject {
    Q_OBJECT

public:
    // version  : CoreDefs::BlobsetVersion (0 = V1 ... 3 = V4)
    // platform : CoreDefs::Platforms (0 = Windows, 1 = PS3, 2 = Xbox 360)
    // gameKey  : V3 only, see BlobsetHash::gameKeyFor()
    // userNames: optional names list (may not exist)
    explicit NameResolverWorker(const QString& blobsetFile, int version, bool isBigEndian, const QString& mappingFile,
                                const QString& userNames, const QString& gameKey, int gameId, int platform,
                                QObject* parent = nullptr);
    ~NameResolverWorker() override = default;

public slots:
    void resolve_names();

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
    QString m_mappingFile;
    QString m_userNames;
    QString m_gameKey;
    int m_gameId;
    int m_platform;

    QSet<QString> m_strings;   // Raw candidate strings (lower case, '/' separated).
    int m_lastPercent = -1;

    // Moves the progress bar (only when the percentage changes, so tight loops can call it freely) and,
    // with a status text, the status line next to it.
    void reportProgress(int percent, const QString& status = QString());
    // Maps `done` of `total` onto the from..to range of the progress bar.
    void reportRange(int from, int to, qint64 done, qint64 total, const QString& status = QString());

    void run();
    void harvestExecutables();
    void harvestUserNames();
    void harvestAssets(const QHash<quint64, QString>& unnamedPaths, const QStringList& allPaths);
    void addStrings(const QByteArray& data);
    void addString(QString s);
};

#endif // NAME_RESOLVER_WORKER_H
