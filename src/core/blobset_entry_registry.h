#ifndef BLOBSET_ENTRY_REGISTRY_H
#define BLOBSET_ENTRY_REGISTRY_H

#include "blobset_file.h"

#include <QHash>
#include <QList>
#include <QReadWriteLock>
#include <QString>

#include <atomic>

// Process-wide lookup of the loaded blobset's entries by path hash, shared by everything that turns a hash into an
// entry (DDS previews, audio, viewers, ...). Thread safe: the image provider and the worker pools read it while the
// UI thread replaces it.
class BlobsetEntryRegistry {
public:
    BlobsetEntryRegistry() = delete;

    // Replaces every entry. Each one is keyed by its 64-bit hash, or its zero-extended legacy 32-bit hash.
    static void replaceAll(const QList<BlobsetEntry>& entries);

    // Looks an entry up by hex hash string (any casing / padding). Returns false if not found.
    static bool findEntry(const QString& hexHash, BlobsetEntry& out);

    // Looks an entry up by its blobset header index. False if not found.
    static bool findByIndex(quint32 index, BlobsetEntry& out);

    static void clear();

    // Changes every time the entries are replaced or cleared (lets callers cache data read for an entry).
    static quint64 generation();

    // Canonical key: exactly 16 lowercase hex digits, zero-padded (matches getHashFromPath).
    static QString makeKey(quint64 hash);

    // Accepts any casing / padding a caller might pass and returns the canonical key,
    // or an empty string if the input isn't valid hex.
    static QString normalizeKey(const QString& hex);

private:
    static QReadWriteLock s_lock;
    static QHash<quint64, BlobsetEntry> s_entryMap;   // Hash -> entry.
    static QHash<quint32, quint64> s_indexMap;        // Header index -> hash.
    static std::atomic<quint64> s_generation;
};

#endif // BLOBSET_ENTRY_REGISTRY_H
