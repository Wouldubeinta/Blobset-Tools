#include "blobset_entry_registry.h"

QReadWriteLock BlobsetEntryRegistry::s_lock;
QHash<quint64, BlobsetEntry> BlobsetEntryRegistry::s_entryMap;
QHash<quint32, quint64> BlobsetEntryRegistry::s_indexMap;
std::atomic<quint64> BlobsetEntryRegistry::s_generation{0};

quint64 BlobsetEntryRegistry::generation() {
    return s_generation.load(std::memory_order_acquire);
}

namespace {
quint64 keyOf(const BlobsetEntry& entry) {
    return entry.folderFileHash64 != 0 ? entry.folderFileHash64 : quint64(entry.folderFileHash32);
}
} // namespace

QString BlobsetEntryRegistry::makeKey(quint64 hash) {
    return QString::number(hash, 16).rightJustified(16, QLatin1Char('0'));
}

QString BlobsetEntryRegistry::normalizeKey(const QString& hex) {
    bool ok = false;
    const quint64 value = hex.trimmed().toULongLong(&ok, 16);
    return ok ? makeKey(value) : QString();
}

void BlobsetEntryRegistry::replaceAll(const QList<BlobsetEntry>& entries) {
    // Built outside the lock, so readers are only blocked for the swap.
    QHash<quint64, BlobsetEntry> map;
    QHash<quint32, quint64> indices;
    map.reserve(entries.size());
    indices.reserve(entries.size());
    for (const BlobsetEntry& entry : entries) {
        const quint64 key = keyOf(entry);
        if (key == 0)
            continue;
        map.insert(key, entry);
        indices.insert(entry.fileIndex, key);
    }

    QWriteLocker locker(&s_lock);
    s_entryMap.swap(map);
    s_indexMap.swap(indices);
    s_generation.fetch_add(1, std::memory_order_acq_rel);
}

bool BlobsetEntryRegistry::findEntry(const QString& hexHash, BlobsetEntry& out) {
    bool ok = false;
    const quint64 key = hexHash.trimmed().toULongLong(&ok, 16);
    if (!ok)
        return false;

    QReadLocker locker(&s_lock);
    const auto it = s_entryMap.constFind(key);
    if (it == s_entryMap.constEnd())
        return false;
    out = it.value();
    return true;
}

bool BlobsetEntryRegistry::findByIndex(quint32 index, BlobsetEntry& out) {
    QReadLocker locker(&s_lock);
    const auto key = s_indexMap.constFind(index);
    if (key == s_indexMap.constEnd())
        return false;
    out = s_entryMap.value(key.value());
    return true;
}

void BlobsetEntryRegistry::clear() {
    QWriteLocker locker(&s_lock);
    s_entryMap.clear();
    s_indexMap.clear();
    s_generation.fetch_add(1, std::memory_order_acq_rel);
}
