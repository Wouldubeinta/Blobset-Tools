#include "commentary_editor_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "compression_io.h"
#include "wem_file.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <stdexcept>

using namespace Qt::StringLiterals;

namespace {

struct KnownFile {
    const char* hash;
    const char* path;
    const char* name;
};
// RL26's commentary files (blobset path hash, path).
constexpr KnownFile kFiles[] = {
    {"99b3b6554a5750d3", "sound\\lines.commentary.cmtry", "Commentary Lines"},
    {"eab8a37c7d0ebc40", "sound\\categoryinfo.commentary.cmtry", "Commentary Category Info"},
    {"01d4046906cce436", "sound\\lines.referee.dat", "Referee Lines"},
    {"33b0057fd1fbd803", "sound\\categoryinfo.referee.cmtry", "Referee Category Info"},
};

constexpr int kConditionBytes = 64;
constexpr int kIdBytes = 16;
constexpr int kInfoRecord = 28;

// The game stores these files as ZSTD chunks (u32 chunk size + 4, then a zstd frame, repeated). The blobset reader
// hands them over as stored when the header gives the same packed and unpacked size, and an extracted copy can
// still be packed: unpack it here. Anything else comes back as it is.
QByteArray unpackZstdChunks(const QByteArray& data) {
    constexpr quint32 kZstdMagic = 0xFD2FB528u;
    const auto u32 = [&data](qsizetype at) {
        return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()) + at);
    };
    if (data.size() < 8 || u32(4) != kZstdMagic)
        return data;
    QByteArray out;
    qsizetype at = 0;
    try {
        while (at + 8 <= data.size()) {
            const qsizetype size = qsizetype(u32(at)) - 4;
            if (size <= 0 || at + 4 + size > data.size() || u32(at + 4) != kZstdMagic)
                return data;
            out += CompressionIO::decompressZstd(data.mid(at + 4, size), true);
            at += 4 + size;
        }
    } catch (const std::exception&) {
        return data;
    }
    return at == data.size() && !out.isEmpty() ? out : data;
}

// The other way: 256 KB pieces, each a zstd frame behind its size + 4 (the game's chunk size).
QByteArray packZstdChunks(const QByteArray& data) {
    constexpr qsizetype kChunk = 262144;
    QByteArray out;
    for (qsizetype at = 0; at < data.size(); at += kChunk) {
        const QByteArray frame = CompressionIO::compressZstd(data.mid(at, kChunk));
        if (frame.isEmpty())
            throw std::runtime_error("ZSTD compression failed");
        uchar size[4];
        qToLittleEndian(quint32(frame.size() + 4), size);
        out.append(reinterpret_cast<const char*>(size), 4);
        out += frame;
    }
    return out;
}

QString toLocalPath(const QString& path) {
    return path.startsWith(u"file:"_s, Qt::CaseInsensitive) ? QUrl(path).toLocalFile() : path;
}

quint32 fnv1a(const QByteArray& text) {
    quint32 h = 0x811c9dc5u;
    for (const char c : std::as_const(text)) {
        h ^= static_cast<quint8>(c);
        h *= 0x01000193u;
    }
    return h;
}

// Bounds checked reads (a bad file throws, and the load says so).
struct Reader {
    const QByteArray& d;
    quint32 u32(qsizetype at) const {
        if (at < 0 || at + 4 > d.size())
            throw std::runtime_error("unexpected end of file");
        return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(d.constData()) + at);
    }
    QByteArray bytes(qsizetype at, qsizetype n) const {
        if (at < 0 || n < 0 || at + n > d.size())
            throw std::runtime_error("unexpected end of file");
        return d.mid(at, n);
    }
    // A zero terminated string; *next is set past the terminator.
    QByteArray cstr(qsizetype at, qsizetype* next = nullptr) const {
        if (at < 0 || at >= d.size())
            throw std::runtime_error("string outside the file");
        const qsizetype end = d.indexOf('\0', at);
        if (end < 0)
            throw std::runtime_error("unterminated string");
        if (next)
            *next = end + 1;
        return d.mid(at, end - at);
    }
    // A name table at `at` (offsets from `at`).
    QList<QByteArray> names(qsizetype at) const {
        const quint32 n = u32(at);
        if (n > 1000000)
            throw std::runtime_error("bad name table");
        QList<QByteArray> out;
        out.reserve(n);
        for (quint32 i = 0; i < n; ++i)
            out << cstr(at + u32(at + 4 + 4 * qsizetype(n) + 4 * qsizetype(i)));
        return out;
    }
};

void put32(QByteArray& out, quint32 v) {
    uchar b[4];
    qToLittleEndian(v, b);
    out.append(reinterpret_cast<const char*>(b), 4);
}
void set32(QByteArray& out, qsizetype at, quint32 v) {
    qToLittleEndian(v, reinterpret_cast<uchar*>(out.data()) + at);
}
void align16(QByteArray& out) {
    while (out.size() % 16)
        out.append('\0');
}
// A name table (count, hashes, offsets from the count, names).
QByteArray nameTable(const QList<QByteArray>& names) {
    QByteArray table;
    const quint32 n = quint32(names.size());
    put32(table, n);
    for (const QByteArray& name : std::as_const(names))
        put32(table, fnv1a(name));
    quint32 at = 4 + 8 * n;
    for (const QByteArray& name : std::as_const(names)) {
        put32(table, at);
        at += quint32(name.size()) + 1;
    }
    for (const QByteArray& name : std::as_const(names)) {
        table += name;
        table.append('\0');
    }
    return table;
}

// "Extern_Commentator_AndrewVoss" -> "Andrew Voss"; a Default bank is named from its folder.
QString speakerName(const QByteArray& folder, const QByteArray& bank) {
    QString b = QString::fromUtf8(bank);
    b = b.mid(b.lastIndexOf(u'_') + 1);
    if (!b.isEmpty() && b.compare(u"Default"_s, Qt::CaseInsensitive) != 0) {
        static const QRegularExpression camel(u"(?<=[a-z])(?=[A-Z])"_s);
        return b.replace(camel, u" "_s);
    }
    QString f = QString::fromUtf8(folder);
    while (f.endsWith(u'/'))
        f.chop(1);
    f = f.mid(f.lastIndexOf(u'/') + 1);
    static const QRegularExpression idPrefix(u"^id\\d+_"_s);
    f.remove(idPrefix);
    QStringList words = f.split(u'_', Qt::SkipEmptyParts);
    for (QString& w : words)
        w[0] = w[0].toUpper();
    return words.join(u' ');
}

bool validName(const QString& name) {
    static const QRegularExpression re(u"^[A-Za-z0-9_#+\\-.]+$"_s);
    return re.match(name).hasMatch();
}

} // namespace

CommentaryEditorManager::CommentaryEditorManager(QObject* parent) : QObject(parent) {}

bool CommentaryEditorManager::isCommentaryHash(const QString& hexHash) {
    bool ok = false;
    const quint64 value = hexHash.trimmed().toULongLong(&ok, 16);
    if (!ok)
        return false;
    for (const KnownFile& f : std::as_const(kFiles))
        if (QString::fromLatin1(f.hash).toULongLong(nullptr, 16) == value)
            return true;
    return false;
}

QStringList CommentaryEditorManager::soundNames(const QByteArray& dataIn) {
    QStringList out;
    const QByteArray data = unpackZstdChunks(dataIn);
    try {
        const Reader r{data};
        const quint32 nameLength = r.u32(4);
        if (data.size() < 12 || r.u32(0) != 8 + nameLength || nameLength > 64)
            return {};
        const qsizetype countAt = 8 + nameLength;
        const quint32 count = r.u32(countAt);
        if (count == 0 || count > 1000)
            return {};
        QSet<QString> seen;
        for (quint32 si = 0; si < count; ++si) {
            const qsizetype at = r.u32(countAt + 4 + 4 * qsizetype(si));
            const qsizetype table = r.u32(at);
            const quint32 folderLength = r.u32(at + 8);
            const QString folder = QString::fromLatin1(r.bytes(at + 12, folderLength));
            const qsizetype entries = r.u32(table);
            const quint32 cats = r.u32(table + 4);
            if (cats > 1000000)
                return {};
            for (quint32 ci = 0; ci < cats; ++ci) {
                qsizetype lineAt = r.u32(entries + 12 * qsizetype(ci));
                const quint32 lineCount = r.u32(entries + 12 * qsizetype(ci) + 4);
                if (lineCount > 100000)
                    return {};
                for (quint32 li = 0; li < lineCount; ++li) {
                    const QByteArray id = r.bytes(lineAt + kConditionBytes, kIdBytes);
                    qsizetype s = lineAt + kConditionBytes + kIdBytes;
                    const int parts = static_cast<quint8>(id.at(11));
                    for (int p = 0; p < parts; ++p) {
                        const QByteArray part = r.cstr(s, &s);
                        if (!part.startsWith("c:")) {   // "c:<category>" plays another category's line.
                            const QString name = u"sound/"_s + folder + QString::fromLatin1(part);
                            if (!seen.contains(name)) {
                                seen.insert(name);
                                out << name;
                            }
                        }
                    }
                    r.cstr(s, &s);   // The subtitle text.
                    lineAt = (s + 15) / 16 * 16;
                }
            }
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

QList<CommentaryEditorManager::SpeakerCategories> CommentaryEditorManager::speakerCategories(const QByteArray& dataIn) {
    QList<SpeakerCategories> out;
    const QByteArray data = unpackZstdChunks(dataIn);
    try {
        const Reader r{data};
        const quint32 nameLength = r.u32(4);
        if (data.size() < 12 || r.u32(0) != 8 + nameLength || nameLength > 64)
            return {};
        const qsizetype countAt = 8 + nameLength;
        const quint32 count = r.u32(countAt);
        if (count == 0 || count > 1000)
            return {};
        for (quint32 si = 0; si < count; ++si) {
            const qsizetype at = r.u32(countAt + 4 + 4 * qsizetype(si));
            const qsizetype table = r.u32(at);
            const quint32 folderLength = r.u32(at + 8);
            const QByteArray folder = r.bytes(at + 12, folderLength);
            const quint32 bankLength = r.u32(at + 12 + folderLength);
            const QByteArray bank = r.bytes(at + 16 + folderLength, bankLength);
            SpeakerCategories sp;
            sp.name = speakerName(folder, bank);
            for (const auto& nameList = r.names(table + 4); const QByteArray& name : nameList)
                sp.categories.insert(QString::fromLatin1(name));
            out << sp;
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

QString CommentaryEditorManager::fileName() const {
    QString name = m_workspacePath.isEmpty() ? m_filePath : m_workspacePath;
    name.replace(u'\\', u'/');
    return name.mid(name.lastIndexOf(u'/') + 1);
}

void CommentaryEditorManager::setStatus(bool ok, const QString& text) {
    m_statusOk = ok;
    m_statusText = text;
    emit statusChanged();
}

void CommentaryEditorManager::edited() {
    ++m_revision;
    emit revisionChanged();
    if (!m_modified) {
        m_modified = true;
        emit modifiedChanged();
    }
}

// --- Loading / saving ------------------------------------------------------------------------------------------

QVariantList CommentaryEditorManager::blobsetFiles() const {
    QVariantList out;
    for (const KnownFile& f : std::as_const(kFiles)) {
        BlobsetEntry entry;
        const bool available = BlobsetEntryRegistry::findEntry(QString::fromLatin1(f.hash), entry);
        out << QVariantMap{{u"name"_s, QString::fromLatin1(f.name)}, {u"path"_s, QString::fromLatin1(f.path)},
                           {u"hash"_s, QString::fromLatin1(f.hash)}, {u"available"_s, available}};
    }
    return out;
}

bool CommentaryEditorManager::loadFromBlobset(const QString& hexHash, const QString& workspacePathIn) {
    BlobsetEntry entry;
    if (!BlobsetEntryRegistry::findEntry(hexHash.toLower(), entry)) {
        setStatus(false, u"This file (hash 0x%1) isn't in the loaded blobset."_s.arg(hexHash.toUpper()));
        return false;
    }
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator) {
        setStatus(false, u"The application orchestrator is unavailable."_s);
        return false;
    }
    QByteArray data;
    try {
        data = BlobsetAssetReader::read(entry, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
                                        orchestrator->platform());
    } catch (const std::exception& ex) {
        setStatus(false, QString::fromUtf8(ex.what()));
        return false;
    }
    // The four files' own paths (what the mods folder needs), else the one given.
    QString path = workspacePathIn;
    for (const KnownFile& f : std::as_const(kFiles))
        if (QString::fromLatin1(f.hash).toULongLong(nullptr, 16) == hexHash.toULongLong(nullptr, 16))
            path = QString::fromLatin1(f.path);
    path.replace(u'\\', u'/');
    // Your edited copy in the mods folder (Save to Mods, New line from WAV) when there is one, so its changes stay.
    if (!path.isEmpty() && orchestrator->hasModFile(path)) {
        const QString modPath = orchestrator->modFilePath(path);
        QFile mod(modPath);
        if (mod.open(QIODevice::ReadOnly) && parse(mod.readAll(), path, path, modPath)) {
            setStatus(true, u"Opened your edited copy in the mods folder (%1)."_s.arg(QDir::toNativeSeparators(modPath)));
            return true;
        }
    }
    return parse(data, path.isEmpty() ? u"0x"_s + hexHash.toUpper() : path, path, QString());
}

bool CommentaryEditorManager::loadFile(const QString& pathOrUrl) {
    const QString path = toLocalPath(pathOrUrl);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    return parse(file.readAll(), QFileInfo(path).fileName(), QString(), path);
}

bool CommentaryEditorManager::parse(const QByteArray& dataIn, const QString& sourceName, const QString& workspacePath,
                                    const QString& filePath) {
    const QByteArray data = unpackZstdChunks(dataIn);
    const QString kindBefore = m_kind;
    QList<Speaker> speakers = m_speakers;
    QList<InfoCategory> info = m_info;
    QByteArray title = m_title;
    bool ok = false;
    QString kind;
    try {
        // A category info file starts with its count twice (u32 count, u32 records, u32 count).
        if (data.size() >= 12 && Reader{data}.u32(0) == Reader{data}.u32(8) && Reader{data}.u32(4) <= quint32(data.size())) {
            ok = parseInfo(data);
            kind = u"categories"_s;
        } else {
            ok = parseLines(data);
            kind = u"lines"_s;
        }
    } catch (const std::exception& ex) {
        setStatus(false, u"%1 isn't a commentary file this editor knows (%2)."_s.arg(sourceName, QString::fromUtf8(ex.what())));
        ok = false;
    }
    if (!ok) {
        m_speakers = speakers;
        m_info = info;
        m_title = title;
        m_kind = kindBefore;
        if (m_statusOk)
            setStatus(false, u"%1 isn't a commentary file this editor knows."_s.arg(sourceName));
        return false;
    }
    m_kind = kind;
    m_packed = data.size() != dataIn.size() || data != dataIn;
    m_sourceName = sourceName;
    m_workspacePath = workspacePath;
    m_filePath = filePath;
    m_modified = false;
    ++m_revision;
    emit loadedChanged();
    emit modifiedChanged();
    emit revisionChanged();
    if (m_kind == u"lines"_s) {
        int lineCount = 0;
        for (const Speaker& s : std::as_const(m_speakers))
            for (const Category& c : std::as_const(s.cats))
                lineCount += int(c.lines.size());
        setStatus(true, u"Loaded %1: %2 speakers, %3 lines."_s.arg(sourceName).arg(m_speakers.size()).arg(lineCount));
    } else {
        setStatus(true, u"Loaded %1: %2 categories."_s.arg(sourceName).arg(m_info.size()));
    }
    return true;
}

bool CommentaryEditorManager::parseLines(const QByteArray& data) {
    const Reader r{data};
    const quint32 nameLength = r.u32(4);
    if (r.u32(0) != 8 + nameLength || nameLength > 64)
        throw std::runtime_error("no lines header");
    QByteArray title = r.bytes(8, nameLength);
    const qsizetype countAt = 8 + nameLength;
    const quint32 count = r.u32(countAt);
    if (count == 0 || count > 1000)
        throw std::runtime_error("bad speaker count");
    QList<Speaker> speakers;
    for (quint32 si = 0; si < count; ++si) {
        Speaker sp;
        const qsizetype at = r.u32(countAt + 4 + 4 * qsizetype(si));
        sp.id = r.u32(countAt + 4 + 4 * qsizetype(count) + 4 * qsizetype(si));
        const qsizetype table = r.u32(at);
        qsizetype q = at + 8;
        const quint32 folderLength = r.u32(q);
        sp.folder = r.bytes(q + 4, folderLength);
        q += 4 + folderLength;
        const quint32 bankLength = r.u32(q);
        sp.bank = r.bytes(q + 4, bankLength);
        q += 4 + bankLength;
        if (q != table)
            throw std::runtime_error("bad speaker header");
        const qsizetype entries = r.u32(table);
        const QList<QByteArray> names = r.names(table + 4);
        for (int ci = 0; ci < names.size(); ++ci) {
            Category cat;
            cat.name = names.at(ci);
            const qsizetype e = entries + 12 * qsizetype(ci);
            qsizetype lineAt = r.u32(e);
            const quint32 lineCount = r.u32(e + 4);
            const qsizetype subs = r.u32(e + 8);
            if (lineCount > 100000)
                throw std::runtime_error("bad line count");
            for (quint32 li = 0; li < lineCount; ++li) {
                Line line;
                line.conditions = r.bytes(lineAt, kConditionBytes);
                line.idBlock = r.bytes(lineAt + kConditionBytes, kIdBytes);
                qsizetype s = lineAt + kConditionBytes + kIdBytes;
                const int parts = static_cast<quint8>(line.idBlock.at(11));
                for (int p = 0; p < parts; ++p)
                    line.parts << r.cstr(s, &s);
                line.text = line.originalText = r.cstr(s, &s);
                cat.lines << line;
                lineAt = (s + 15) / 16 * 16;
            }
            if (subs)
                cat.subs = r.names(subs);
            sp.cats << cat;
        }
        speakers << sp;
    }
    m_title = title;
    m_speakers = speakers;
    m_info.clear();
    return true;
}

bool CommentaryEditorManager::parseInfo(const QByteArray& data) {
    const Reader r{data};
    const quint32 count = r.u32(0);
    const qsizetype records = r.u32(4);
    if (count == 0 || count > 100000 || records + qsizetype(count) * kInfoRecord > data.size())
        throw std::runtime_error("bad category info header");
    const QList<QByteArray> names = r.names(8);
    QList<InfoCategory> info;
    for (int i = 0; i < names.size(); ++i)
        info << InfoCategory{names.at(i), r.bytes(records + qsizetype(i) * kInfoRecord, kInfoRecord)};
    m_info = info;
    m_speakers.clear();
    m_title = "Category Info";
    return true;
}

QByteArray CommentaryEditorManager::serialize() const {
    const QByteArray data = m_kind == u"lines"_s ? serializeLines() : serializeInfo();
    return m_packed ? packZstdChunks(data) : data;
}

QByteArray CommentaryEditorManager::serializeLines() const {
    QByteArray out;
    put32(out, 8 + quint32(m_title.size()));
    put32(out, quint32(m_title.size()));
    out += m_title;
    const qsizetype count = m_speakers.size();
    put32(out, quint32(count));
    const qsizetype offsetsAt = out.size();
    for (qsizetype i = 0; i < count; ++i)
        put32(out, 0);
    for (const Speaker& sp : std::as_const(m_speakers))
        put32(out, sp.id);
    for (qsizetype si = 0; si < count; ++si) {
        const Speaker& sp = m_speakers.at(si);
        align16(out);   // Between speakers only: the file ends where the last one does.
        const qsizetype start = out.size();
        set32(out, offsetsAt + 4 * si, quint32(start));
        const qsizetype table = start + 8 + 4 + sp.folder.size() + 4 + sp.bank.size();
        put32(out, quint32(table));
        put32(out, quint32(start + 8 + 4 + sp.folder.size()));
        put32(out, quint32(sp.folder.size()));
        out += sp.folder;
        put32(out, quint32(sp.bank.size()));
        out += sp.bank;
        QList<QByteArray> names;
        for (const Category& c : std::as_const(sp.cats))
            names << c.name;
        const QByteArray tableBytes = nameTable(names);
        put32(out, quint32(table + 4 + tableBytes.size()));
        out += tableBytes;
        const qsizetype entries = out.size();
        out.append(QByteArray(12 * sp.cats.size(), '\0'));
        align16(out);
        QList<qsizetype> firsts;
        for (const Category& c : std::as_const(sp.cats)) {
            firsts << (c.lines.isEmpty() ? 0 : out.size());
            for (const Line& line : std::as_const(c.lines)) {
                out += line.conditions;
                QByteArray id = line.idBlock;
                id[11] = char(quint8(line.parts.size()));
                out += id;
                for (const QByteArray& part : std::as_const(line.parts)) {
                    out += part;
                    out.append('\0');
                }
                out += line.text;
                out.append('\0');
                align16(out);
            }
        }
        for (int ci = 0; ci < sp.cats.size(); ++ci) {
            const Category& c = sp.cats.at(ci);
            qsizetype subs = 0;
            if (!c.subs.isEmpty()) {
                subs = out.size();
                out += nameTable(c.subs);
            }
            set32(out, entries + 12 * ci, quint32(firsts.at(ci)));
            set32(out, entries + 12 * ci + 4, quint32(c.lines.size()));
            set32(out, entries + 12 * ci + 8, quint32(subs));
        }
    }
    return out;
}

QByteArray CommentaryEditorManager::serializeInfo() const {
    QList<QByteArray> names;
    for (const InfoCategory& c : std::as_const(m_info))
        names << c.name;
    const QByteArray table = nameTable(names);
    QByteArray out;
    put32(out, quint32(m_info.size()));
    put32(out, quint32(8 + table.size()));
    out += table;
    for (const InfoCategory& c : std::as_const(m_info))
        out += c.record;
    return out;
}

bool CommentaryEditorManager::writeFile(const QString& path, const QByteArray& data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        setStatus(false, u"Could not create the folder of %1."_s.arg(QDir::toNativeSeparators(path)));
        return false;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        setStatus(false, u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
        return false;
    }
    return true;
}

QString CommentaryEditorManager::saveToMods() {
    if (!loaded())
        return {};
    if (m_workspacePath.isEmpty()) {
        setStatus(false, u"This file wasn't opened from the blobset, so its place in the mods folder isn't known: use Save As."_s);
        return {};
    }
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    const QString mods = orchestrator ? orchestrator->modsFolderPath() : QString();
    if (mods.isEmpty()) {
        setStatus(false, u"No game is loaded, so there's no mods folder."_s);
        return {};
    }
    QString path = QDir::cleanPath(QDir(mods).filePath(m_workspacePath));
    QByteArray bytes;
    try {
        bytes = serialize();
    } catch (const std::exception& ex) {
        setStatus(false, QString::fromUtf8(ex.what()));
        return {};
    }
    if (!writeFile(path, bytes))
        return {};
    m_filePath = path;
    m_modified = false;
    emit modifiedChanged();
    setStatus(true, u"Saved to %1. Run Modify to put it in the game."_s.arg(QDir::toNativeSeparators(path)));
    return path;
}

bool CommentaryEditorManager::saveAs(const QString& pathOrUrl) {
    if (!loaded())
        return false;
    const QString path = toLocalPath(pathOrUrl);
    QByteArray bytes;
    try {
        bytes = serialize();
    } catch (const std::exception& ex) {
        setStatus(false, QString::fromUtf8(ex.what()));
        return false;
    }
    if (!writeFile(path, bytes))
        return false;
    m_filePath = path;
    m_modified = false;
    emit modifiedChanged();
    setStatus(true, u"Saved to %1."_s.arg(QDir::toNativeSeparators(path)));
    return true;
}

// --- Lines ---------------------------------------------------------------------------------------------------

CommentaryEditorManager::Category* CommentaryEditorManager::categoryAt(int speaker, int category) {
    if (speaker < 0 || speaker >= m_speakers.size() || category < 0 || category >= m_speakers.at(speaker).cats.size())
        return nullptr;
    return &m_speakers[speaker].cats[category];
}
const CommentaryEditorManager::Category* CommentaryEditorManager::categoryAt(int speaker, int category) const {
    if (speaker < 0 || speaker >= m_speakers.size() || category < 0 || category >= m_speakers.at(speaker).cats.size())
        return nullptr;
    return &m_speakers.at(speaker).cats.at(category);
}
CommentaryEditorManager::Line* CommentaryEditorManager::lineAt(int speaker, int category, int index) {
    Category* c = categoryAt(speaker, category);
    return c && index >= 0 && index < c->lines.size() ? &c->lines[index] : nullptr;
}
const CommentaryEditorManager::Line* CommentaryEditorManager::lineAt(int speaker, int category, int index) const {
    const Category* c = categoryAt(speaker, category);
    return c && index >= 0 && index < c->lines.size() ? &c->lines.at(index) : nullptr;
}

quint64 CommentaryEditorManager::condition(const Line& line, int field) {
    return qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(line.conditions.constData()) + 8 * field);
}
void CommentaryEditorManager::setCondition(Line& line, int field, quint64 value) {
    qToLittleEndian(value, reinterpret_cast<uchar*>(line.conditions.data()) + 8 * field);
}

namespace {
constexpr int kSubMask = 4;   // The condition that holds a line's sub-categories.
QStringList maskNames(quint64 mask, const QList<QByteArray>& subs) {
    QStringList out;
    for (int b = 0; b < subs.size() && b < 64; ++b)
        if (mask & (quint64(1) << b))
            out << QString::fromUtf8(subs.at(b));
    return out;
}
} // namespace

QString CommentaryEditorManager::lineId(const Line& line) {
    const int end = line.idBlock.indexOf('\0');
    return QString::fromLatin1(line.idBlock.left(end < 0 || end > 11 ? 11 : end));
}

bool CommentaryEditorManager::lineMatches(const Line& line, const QString& query) {
    if (QString::fromUtf8(line.text).contains(query, Qt::CaseInsensitive) || lineId(line).contains(query))
        return true;
    for (const QByteArray& part : std::as_const(line.parts))
        if (QString::fromUtf8(part).contains(query, Qt::CaseInsensitive))
            return true;
    return false;
}

QVariantList CommentaryEditorManager::speakers() const {
    QVariantList out;
    for (int i = 0; i < m_speakers.size(); ++i) {
        const Speaker& s = m_speakers.at(i);
        int lineCount = 0;
        for (const Category& c : std::as_const(s.cats))
            lineCount += int(c.lines.size());
        out << QVariantMap{{u"index"_s, i}, {u"id"_s, s.id}, {u"name"_s, speakerName(s.folder, s.bank)},
                           {u"folder"_s, QString::fromUtf8(s.folder)}, {u"bank"_s, QString::fromUtf8(s.bank)},
                           {u"categories"_s, int(s.cats.size())}, {u"lines"_s, lineCount}};
    }
    return out;
}

QVariantList CommentaryEditorManager::categories(int speaker, const QString& filterIn) const {
    QVariantList out;
    if (speaker < 0 || speaker >= m_speakers.size())
        return out;
    const QString filter = filterIn.trimmed();
    const QList<Category>& cats = m_speakers.at(speaker).cats;
    for (int i = 0; i < cats.size(); ++i) {
        const Category& c = cats.at(i);
        const QString name = QString::fromUtf8(c.name);
        int matches = 0;
        if (!filter.isEmpty()) {
            for (const Line& line : std::as_const(c.lines))
                if (lineMatches(line, filter))
                    ++matches;
            if (matches == 0 && !name.contains(filter, Qt::CaseInsensitive))
                continue;
        }
        out << QVariantMap{{u"index"_s, i}, {u"name"_s, name}, {u"lines"_s, int(c.lines.size())},
                           {u"subcategories"_s, int(c.subs.size())}, {u"match"_s, matches}};
    }
    return out;
}

QVariantList CommentaryEditorManager::lines(int speaker, int category) const {
    QVariantList out;
    const Category* c = categoryAt(speaker, category);
    if (!c)
        return out;
    for (int i = 0; i < c->lines.size(); ++i) {
        const Line& line = c->lines.at(i);
        QStringList parts;
        for (const QByteArray& p : std::as_const(line.parts))
            parts << QString::fromUtf8(p);
        out << QVariantMap{{u"index"_s, i}, {u"soundId"_s, lineId(line)}, {u"text"_s, QString::fromUtf8(line.text)},
                           {u"parts"_s, parts.join(u"  +  "_s)}, {u"gender"_s, static_cast<quint8>(line.idBlock.at(12))},
                           {u"subNames"_s, maskNames(condition(line, kSubMask), c->subs).join(u", "_s)},
                           {u"flag"_s, line.idBlock.at(14) != 0},
                           {u"modified"_s, line.added || line.text != line.originalText}, {u"added"_s, line.added}};
    }
    return out;
}

QVariantMap CommentaryEditorManager::line(int speaker, int category, int index) const {
    const Line* line = lineAt(speaker, category, index);
    if (!line)
        return {};
    QStringList parts;
    for (const QByteArray& p : std::as_const(line->parts))
        parts << QString::fromUtf8(p);
    QStringList conditions;
    for (int f = 0; f < kConditionBytes / 8; ++f)
        conditions << u"0x"_s + u"%1"_s.arg(condition(*line, f), 16, 16, QChar(u'0')).toUpper();
    const quint64 mask = condition(*line, kSubMask);
    QVariantList subFlags;
    const Category* c = categoryAt(speaker, category);
    for (int b = 0; b < c->subs.size() && b < 64; ++b)
        subFlags << bool(mask & (quint64(1) << b));
    return {{u"id"_s, lineId(*line)}, {u"text"_s, QString::fromUtf8(line->text)},
            {u"originalText"_s, line->added ? QString() : QString::fromUtf8(line->originalText)},
            {u"parts"_s, parts}, {u"gender"_s, static_cast<quint8>(line->idBlock.at(12))},
            {u"flag"_s, line->idBlock.at(14) != 0}, {u"conditions"_s, conditions},
            {u"subcategories"_s, subcategories(speaker, category)}, {u"subFlags"_s, subFlags}, {u"added"_s, line->added}};
}

QStringList CommentaryEditorManager::subcategories(int speaker, int category) const {
    QStringList out;
    if (const Category* c = categoryAt(speaker, category))
        for (const QByteArray& s : std::as_const(c->subs))
            out << QString::fromUtf8(s);
    return out;
}

namespace {
// "commentary/id1_andrewvoss/" (the folder as the lines file stores it, maybe with its terminator).
QString speakerFolder(const QByteArray& folder) {
    QString f = QString::fromLatin1(folder);
    while (f.endsWith(QChar(u'\0')))
        f.chop(1);
    f.replace(u'\\', u'/');
    if (!f.isEmpty() && !f.endsWith(u'/'))
        f += u'/';
    return f.toLower();
}

quint64 soundHash(const QString& sound) {
    quint64 h = 0xCBF29CE484222325ull;
    for (const auto& cList = sound.toLower().replace(u'\\', u'/').toLatin1(); const char c : cList) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001B3ull;
    }
    return h;
}

QString hashKey(quint64 hash) {
    return QString::number(hash, 16).rightJustified(16, u'0');
}
} // namespace

bool CommentaryEditorManager::isNameCategory(const QString& category) const {
    const QString c = category.toLower();
    return c.startsWith(u"surname_"_s) || c.startsWith(u"team_"_s) || c.startsWith(u"firstname_"_s)
           || c.startsWith(u"nickname_"_s);
}

int CommentaryEditorManager::speakerForFolder(const QString& folderIn) const {
    QString folder = QString(folderIn).replace(u'\\', u'/').toLower();
    if (folder.startsWith(u"sound/"_s))
        folder = folder.mid(6);
    if (!folder.endsWith(u'/'))
        folder += u'/';
    for (int i = 0; i < m_speakers.size(); ++i)
        if (speakerFolder(m_speakers.at(i).folder) == folder)
            return i;
    return -1;
}

QString CommentaryEditorManager::checkSoundName(int speaker, const QString& name) const {
    if (speaker < 0 || speaker >= m_speakers.size())
        return u"Pick a speaker."_s;
    static const QRegularExpression valid(u"^[a-z0-9_]{1,60}$"_s);
    if (!valid.match(name).hasMatch())
        return u"Use lower case letters, digits and _ (up to 60)."_s;
    const QByteArray bytes = name.toLatin1();
    for (const auto& cList = m_speakers.at(speaker).cats; const Category& c : cList)
        for (const Line& l : std::as_const(c.lines))
            for (const QByteArray& part : std::as_const(l.parts))
                if (part == bytes)
                    return u"A line of %1's (%2) already plays %3."_s
                        .arg(speakerName(m_speakers.at(speaker).folder, m_speakers.at(speaker).bank), QString::fromUtf8(c.name), name);
    const QString sound = u"sound/"_s + speakerFolder(m_speakers.at(speaker).folder) + name;
    BlobsetEntry entry;
    if (BlobsetEntryRegistry::findEntry(BlobsetEntryRegistry::makeKey(soundHash(sound)), entry))
        return u"The game already has a sound named %1."_s.arg(name);
    return {};
}

QString CommentaryEditorManager::suggestSoundName(int speaker, const QString& wavPathOrUrl) const {
    if (speaker < 0 || speaker >= m_speakers.size())
        return {};
    QString base = QFileInfo(toLocalPath(wavPathOrUrl)).completeBaseName().toLower();
    static const QRegularExpression notNameChars(u"[^a-z0-9_]+"_s);
    static const QRegularExpression underscores(u"_+"_s);
    base.replace(notNameChars, u"_"_s);
    base.replace(underscores, u"_"_s);
    while (base.startsWith(u'_'))
        base.remove(0, 1);
    while (base.endsWith(u'_'))
        base.chop(1);
    if (base.isEmpty())
        base = u"new_line"_s;
    // The speaker's prefix: "commentary/id1_andrewvoss/" -> "id1_".
    const QString folder = speakerFolder(m_speakers.at(speaker).folder).section(u'/', -2, -2);
    const QString prefix = folder.section(u'_', 0, 0) + u'_';
    if (folder.contains(u'_') && !base.startsWith(prefix))
        base.prepend(prefix);
    base = base.left(60);
    QString name = base;
    for (int n = 2; !checkSoundName(speaker, name).isEmpty() && n < 1000; ++n)
        name = base.left(55) + u'_' + QString::number(n);
    return name;
}

QVariantMap CommentaryEditorManager::newSoundInfo(int speaker, const QString& name) const {
    if (speaker < 0 || speaker >= m_speakers.size())
        return {};
    const QString sound = u"sound/"_s + speakerFolder(m_speakers.at(speaker).folder) + name.toLower();
    return {{u"sound"_s, sound}, {u"path"_s, QDir::toNativeSeparators(sound + u".wem"_s)},
            {u"pathHash"_s, u"0x"_s + hashKey(soundHash(sound)).toUpper()}};
}

QString CommentaryEditorManager::defaultWemHash(const QString& wavPathOrUrl) const {
    const QString path = toLocalPath(wavPathOrUrl);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    if (path.endsWith(u".wem"_s, Qt::CaseInsensitive))   // A WEM keeps its own hash chunk unless one is typed.
        return QString::fromLatin1(WemFile::hashOf(bytes).toHex());
    return QString::fromLatin1(WemFile::defaultHash(bytes).toHex());
}

int CommentaryEditorManager::addLineFromWav(int speaker, int category, const QString& wavPathOrUrl, const QString& nameIn,
                                            const QString& text, int gender, bool phrase, const QString& wemHash) {
    Category* c = categoryAt(speaker, category);
    if (!c || m_kind != u"lines"_s) {
        setStatus(false, u"Pick the category the line goes in."_s);
        return -1;
    }
    AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator || orchestrator->modsFolderPath().isEmpty()) {
        setStatus(false, u"Load the game first: the new sound goes into its mods folder."_s);
        return -1;
    }
    if (m_workspacePath.isEmpty()) {
        setStatus(false, u"Open the lines file from the blobset (not a file on disk), so it can be saved to the mods folder with the new line."_s);
        return -1;
    }
    const QString name = nameIn.trimmed().toLower();
    if (const QString why = checkSoundName(speaker, name); !why.isEmpty()) {
        setStatus(false, why);
        return -1;
    }
    QByteArray hash;
    const QString hashText = QString(wemHash).remove(u' ').remove(u"0x"_s, Qt::CaseInsensitive);
    if (!hashText.isEmpty()) {
        static const QRegularExpression hex(u"^[0-9a-fA-F]{32}$"_s);
        if (!hex.match(hashText).hasMatch()) {
            setStatus(false, u"The WEM hash is 32 hex digits (16 bytes)."_s);
            return -1;
        }
        hash = QByteArray::fromHex(hashText.toLatin1());
    }
    const QString wavPath = toLocalPath(wavPathOrUrl);
    QFile wavFile(wavPath);
    if (!wavFile.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Could not read %1."_s.arg(QDir::toNativeSeparators(wavPath)));
        return -1;
    }
    const QByteArray wav = wavFile.readAll();

    // The speaker's own sounds' extra chunks (the first of its sounds the blobset has).
    const Speaker& sp = m_speakers.at(speaker);
    const QString folder = speakerFolder(sp.folder);
    QByteArray templateWem;
    for (const Category& cat : std::as_const(sp.cats)) {
        for (const Line& l : std::as_const(cat.lines)) {
            for (const QByteArray& part : std::as_const(l.parts)) {
                if (part.startsWith("c:"))
                    continue;
                BlobsetEntry entry;
                if (!BlobsetEntryRegistry::findEntry(BlobsetEntryRegistry::makeKey(soundHash(u"sound/"_s + folder + QString::fromLatin1(part))), entry))
                    continue;
                try {
                    templateWem = BlobsetAssetReader::read(entry, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
                                                           orchestrator->platform());
                } catch (const std::exception&) {
                }
                break;
            }
            if (!templateWem.isEmpty())
                break;
        }
        if (!templateWem.isEmpty())
            break;
    }
    QString error;
    const QByteArray wem = WemFile::newFromFile(wav, wavPath.endsWith(u".wem"_s, Qt::CaseInsensitive), templateWem, hash, &error);
    if (wem.isEmpty()) {
        setStatus(false, QFileInfo(wavPath).fileName() + u": "_s + error);
        return -1;
    }
    const QVariantMap info = newSoundInfo(speaker, name);
    const QString wemPath = info.value(u"sound"_s).toString() + u".wem"_s;
    const QVariantMap written = orchestrator->writeModFile(wemPath, wem);
    if (!written.value(u"ok"_s).toBool()) {
        setStatus(false, written.value(u"message"_s).toString());
        return -1;
    }
    orchestrator->registerNewAsset(wemPath, soundHash(info.value(u"sound"_s).toString()));

    // The line: the category's first line's conditions, a new sound id (unused by this file's lines).
    QSet<QString> ids;
    for (const Speaker& s : std::as_const(m_speakers))
        for (const Category& cat : std::as_const(s.cats))
            for (const Line& l : std::as_const(cat.lines))
                ids.insert(lineId(l));
    quint32 id = quint32(soundHash(info.value(u"sound"_s).toString()) & 0xFFFFFFFFu);
    while (ids.contains(QString::number(id).rightJustified(10, u'0')) || id == 0)
        ++id;
    Line line;
    line.conditions = c->lines.isEmpty() ? QByteArray(kConditionBytes, '\0') : c->lines.first().conditions;
    line.idBlock = QByteArray(kIdBytes, '\0');
    const QByteArray idText = QString::number(id).rightJustified(10, u'0').toLatin1();
    memcpy(line.idBlock.data(), idText.constData(), 10);
    line.idBlock[11] = 1;
    line.idBlock[12] = char(quint8(qBound(0, gender, 2)));
    line.idBlock[14] = phrase ? 1 : 0;   // A whole sentence (names spliced into sentences have 0).
    line.parts << name.toLatin1();
    line.text = text.toUtf8();
    line.added = true;
    c->lines << line;
    edited();
    const int index = int(c->lines.size()) - 1;

    const QString saved = saveToMods();
    if (saved.isEmpty())
        return index;   // The status says why the lines weren't saved; the WEM is in the mods folder.
    setStatus(true, u"Added %1 to %2 (%3, path hash %4, WEM %5) and saved the lines to the mods folder. Click Modify "
                    u"to put them into the game."_s
                        .arg(name, QString::fromUtf8(c->name), info.value(u"path"_s).toString(),
                             info.value(u"pathHash"_s).toString(), WemFile::describe(wem)));
    return index;
}

bool CommentaryEditorManager::setLineText(int speaker, int category, int index, const QString& text) {
    Line* line = lineAt(speaker, category, index);
    if (!line)
        return false;
    QByteArray bytes = text.toUtf8();
    bytes.replace('\0', "");
    if (bytes == line->text)
        return false;
    line->text = bytes;
    edited();
    return true;
}

bool CommentaryEditorManager::setLineParts(int speaker, int category, int index, const QStringList& partsIn) {
    Line* line = lineAt(speaker, category, index);
    if (!line)
        return false;
    QList<QByteArray> parts;
    for (const QString& p : std::as_const(partsIn)) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            parts << t.toUtf8();
    }
    if (parts.size() > 255) {
        setStatus(false, u"A line can have at most 255 parts."_s);
        return false;
    }
    if (parts == line->parts)
        return false;
    line->parts = parts;
    edited();
    return true;
}

bool CommentaryEditorManager::setLineId(int speaker, int category, int index, const QString& idIn) {
    Line* line = lineAt(speaker, category, index);
    if (!line)
        return false;
    const QString id = idIn.trimmed();
    static const QRegularExpression digits(u"^\\d{1,10}$"_s);
    if (!digits.match(id).hasMatch() || id.toULongLong() > 0xffffffffULL) {
        setStatus(false, u"A sound id is a number up to 4294967295 (the game's lines use 10 digits, e.g. 0909938095)."_s);
        return false;
    }
    const QByteArray bytes = id.toLatin1();
    if (lineId(*line).toLatin1() == bytes)
        return false;
    for (int i = 0; i < 11; ++i)
        line->idBlock[i] = i < bytes.size() ? bytes.at(i) : '\0';
    edited();
    return true;
}

bool CommentaryEditorManager::setLineGender(int speaker, int category, int index, int gender) {
    Line* line = lineAt(speaker, category, index);
    if (!line || gender < 0 || gender > 255 || static_cast<quint8>(line->idBlock.at(12)) == gender)
        return false;
    line->idBlock[12] = char(quint8(gender));
    edited();
    return true;
}

bool CommentaryEditorManager::setLineSubcategory(int speaker, int category, int index, int subcategory, bool on) {
    Line* line = lineAt(speaker, category, index);
    const Category* c = categoryAt(speaker, category);
    if (!line || subcategory < 0 || subcategory >= c->subs.size() || subcategory >= 64)
        return false;
    const quint64 bit = quint64(1) << subcategory;
    const quint64 mask = condition(*line, kSubMask);
    const quint64 next = on ? (mask | bit) : (mask & ~bit);
    if (next == mask)
        return false;
    setCondition(*line, kSubMask, next);
    edited();
    return true;
}

bool CommentaryEditorManager::setLineFlag(int speaker, int category, int index, bool on) {
    Line* line = lineAt(speaker, category, index);
    if (!line || (line->idBlock.at(14) != 0) == on)
        return false;
    line->idBlock[14] = on ? 1 : 0;
    edited();
    return true;
}

bool CommentaryEditorManager::setLineCondition(int speaker, int category, int index, int field, const QString& valueIn) {
    Line* line = lineAt(speaker, category, index);
    if (!line || field < 0 || field >= kConditionBytes / 8)
        return false;
    QString value = valueIn.trimmed();
    bool ok = false;
    const quint64 v = value.startsWith(u"0x"_s, Qt::CaseInsensitive) ? QStringView(value).mid(2).toULongLong(&ok, 16) : value.toULongLong(&ok, 10);
    if (!ok) {
        setStatus(false, u"A condition is a 64 bit mask (hex as 0x..., or decimal)."_s);
        return false;
    }
    if (condition(*line, field) == v)
        return false;
    setCondition(*line, field, v);
    edited();
    return true;
}

bool CommentaryEditorManager::revertLineText(int speaker, int category, int index) {
    Line* line = lineAt(speaker, category, index);
    if (!line || line->added || line->text == line->originalText)
        return false;
    line->text = line->originalText;
    edited();
    return true;
}

int CommentaryEditorManager::addLine(int speaker, int category, int from) {
    Category* c = categoryAt(speaker, category);
    if (!c)
        return -1;
    Line line;
    if (from >= 0 && from < c->lines.size()) {
        line = c->lines.at(from);
    } else {
        line.conditions = QByteArray(kConditionBytes, '\0');
        line.idBlock = QByteArray(kIdBytes, '\0');
        memcpy(line.idBlock.data(), "0000000000", 10);
        line.parts << QByteArray("new_sound");
    }
    line.originalText.clear();
    line.added = true;
    c->lines << line;
    edited();
    setStatus(true, from >= 0 ? u"Copied the line to the end of %1. Give it the sound it plays (its id and parts)."_s.arg(QString::fromUtf8(c->name))
                              : u"Added a line to %1. Set its sound id, parts and text."_s.arg(QString::fromUtf8(c->name)));
    return int(c->lines.size()) - 1;
}

bool CommentaryEditorManager::removeLine(int speaker, int category, int index) {
    Category* c = categoryAt(speaker, category);
    if (!c || index < 0 || index >= c->lines.size())
        return false;
    c->lines.removeAt(index);
    edited();
    return true;
}

bool CommentaryEditorManager::moveLine(int speaker, int category, int index, int delta) {
    Category* c = categoryAt(speaker, category);
    if (!c || index < 0 || index >= c->lines.size())
        return false;
    const int to = index + delta;
    if (to < 0 || to >= c->lines.size() || to == index)
        return false;
    c->lines.move(index, to);
    edited();
    return true;
}

int CommentaryEditorManager::addCategory(int speaker, const QString& nameIn) {
    if (speaker < 0 || speaker >= m_speakers.size())
        return -1;
    const QString name = nameIn.trimmed();
    if (!validName(name)) {
        setStatus(false, u"A category name is letters, digits and _ (e.g. try_reply)."_s);
        return -1;
    }
    Speaker& s = m_speakers[speaker];
    for (const Category& c : std::as_const(s.cats))
        if (QString::fromUtf8(c.name) == name) {
            setStatus(false, u"%1 already has a %2 category."_s.arg(speakerName(s.folder, s.bank), name));
            return -1;
        }
    Category c;
    c.name = name.toUtf8();
    s.cats << c;
    edited();
    setStatus(true, u"Added the %1 category. Add its lines; give it settings in the category info file if it needs them."_s.arg(name));
    return int(s.cats.size()) - 1;
}

bool CommentaryEditorManager::renameCategory(int speaker, int category, const QString& nameIn) {
    Category* c = categoryAt(speaker, category);
    const QString name = nameIn.trimmed();
    if (!c || QString::fromUtf8(c->name) == name)
        return false;
    if (!validName(name)) {
        setStatus(false, u"A category name is letters, digits and _ (e.g. try_reply)."_s);
        return false;
    }
    for (const Category& other : std::as_const(m_speakers.at(speaker).cats))
        if (QString::fromUtf8(other.name) == name) {
            setStatus(false, u"There's already a %1 category."_s.arg(name));
            return false;
        }
    c->name = name.toUtf8();
    edited();
    return true;
}

bool CommentaryEditorManager::removeCategory(int speaker, int category) {
    if (!categoryAt(speaker, category))
        return false;
    m_speakers[speaker].cats.removeAt(category);
    edited();
    return true;
}

bool CommentaryEditorManager::setSubcategories(int speaker, int category, const QStringList& namesIn) {
    Category* c = categoryAt(speaker, category);
    if (!c)
        return false;
    QList<QByteArray> names;
    for (const QString& n : std::as_const(namesIn)) {
        const QString t = n.trimmed();
        if (t.isEmpty())
            continue;
        if (!validName(t)) {
            setStatus(false, u"\"%1\": a sub-category name is letters, digits and _ # + - . only."_s.arg(t));
            return false;
        }
        names << t.toUtf8();
    }
    if (names.size() > 64) {
        setStatus(false, u"A category can have at most 64 sub-categories."_s);
        return false;
    }
    if (names == c->subs)
        return false;
    // Each line keeps the sub-categories it had by name (they may have moved, been renamed away or removed).
    for (Line& line : c->lines) {
        const quint64 mask = condition(line, kSubMask);
        quint64 next = 0;
        for (int b = 0; b < names.size(); ++b) {
            const int old = int(c->subs.indexOf(names.at(b)));
            if (old >= 0 && old < 64 && (mask & (quint64(1) << old)))
                next |= quint64(1) << b;
        }
        setCondition(line, kSubMask, next);
    }
    c->subs = names;
    edited();
    return true;
}

QVariantMap CommentaryEditorManager::find(const QString& queryIn, int speaker, int category, int index, bool backward) const {
    const QString query = queryIn.trimmed();
    if (query.isEmpty() || m_speakers.isEmpty())
        return {};
    // Every line in order, as one sequence; start just after (or before) the current one and wrap.
    struct Pos { int s, c, l; };
    QList<Pos> all;
    int current = -1;
    for (int s = 0; s < m_speakers.size(); ++s)
        for (int c = 0; c < m_speakers.at(s).cats.size(); ++c)
            for (int l = 0; l < m_speakers.at(s).cats.at(c).lines.size(); ++l) {
                if (s == speaker && c == category && l == index)
                    current = int(all.size());
                all << Pos{s, c, l};
            }
    const int n = int(all.size());
    if (n == 0)
        return {};
    int start = current;
    if (start < 0)
        start = backward ? 0 : n - 1;
    for (int k = 1; k <= n; ++k) {
        const int i = ((start + (backward ? -k : k)) % n + n) % n;
        const Pos& p = all.at(i);
        if (lineMatches(m_speakers.at(p.s).cats.at(p.c).lines.at(p.l), query))
            return {{u"speaker"_s, p.s}, {u"category"_s, p.c}, {u"line"_s, p.l}};
    }
    return {};
}

int CommentaryEditorManager::countMatches(const QString& queryIn) const {
    const QString query = queryIn.trimmed();
    if (query.isEmpty())
        return 0;
    int n = 0;
    for (const Speaker& s : std::as_const(m_speakers))
        for (const Category& c : std::as_const(s.cats))
            for (const Line& line : std::as_const(c.lines))
                if (lineMatches(line, query))
                    ++n;
    return n;
}

namespace {
QString csvField(const QString& value) {
    QString v = value;
    v.replace(u'"', u"\"\""_s);
    return u'"' + v + u'"';
}
// One CSV record (quoted fields may hold commas, quotes and line breaks).
QStringList csvRecord(const QString& text, qsizetype& at) {
    QStringList fields;
    QString field;
    bool quoted = false;
    while (at < text.size()) {
        const QChar ch = text.at(at++);
        if (quoted) {
            if (ch == u'"') {
                if (at < text.size() && text.at(at) == u'"') {
                    field += u'"';
                    ++at;
                } else {
                    quoted = false;
                }
            } else {
                field += ch;
            }
        } else if (ch == u'"') {
            quoted = true;
        } else if (ch == u',') {
            fields << field;
            field.clear();
        } else if (ch == u'\n' || ch == u'\r') {
            if (ch == u'\r' && at < text.size() && text.at(at) == u'\n')
                ++at;
            break;
        } else {
            field += ch;
        }
    }
    fields << field;
    return fields;
}
} // namespace

bool CommentaryEditorManager::exportCsv(const QString& pathOrUrl) {
    if (m_kind != u"lines"_s)
        return false;
    QString csv = u"Speaker,Category,Line,Id,Sub-categories,Parts,Text\r\n"_s;
    for (const Speaker& s : std::as_const(m_speakers))
        for (const Category& c : std::as_const(s.cats))
            for (int i = 0; i < c.lines.size(); ++i) {
                const Line& line = c.lines.at(i);
                QStringList parts;
                for (const QByteArray& p : std::as_const(line.parts))
                    parts << QString::fromUtf8(p);
                csv += csvField(QString::fromUtf8(s.folder)) + u',' + csvField(QString::fromUtf8(c.name)) + u','
                       + QString::number(i + 1) + u',' + csvField(lineId(line)) + u','
                       + csvField(maskNames(condition(line, kSubMask), c.subs).join(u'|')) + u','
                       + csvField(parts.join(u'|')) + u',' + csvField(QString::fromUtf8(line.text)) + u"\r\n"_s;
            }
    const QString path = toLocalPath(pathOrUrl);
    QSaveFile out(path);
    const QByteArray bytes = QByteArray("\xEF\xBB\xBF") + csv.toUtf8();
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        setStatus(false, u"Could not write %1: %2"_s.arg(QDir::toNativeSeparators(path), out.errorString()));
        return false;
    }
    setStatus(true, u"Exported the lines to %1."_s.arg(QDir::toNativeSeparators(path)));
    return true;
}

bool CommentaryEditorManager::importCsv(const QString& pathOrUrl) {
    if (m_kind != u"lines"_s)
        return false;
    const QString path = toLocalPath(pathOrUrl);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(false, u"Could not open %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    qsizetype at = 0;
    const QStringList header = csvRecord(text, at);
    const int colSpeaker = int(header.indexOf(u"Speaker"_s)), colCat = int(header.indexOf(u"Category"_s));
    const int colLine = int(header.indexOf(u"Line"_s)), colParts = int(header.indexOf(u"Parts"_s));
    const int colText = int(header.indexOf(u"Text"_s));
    if (colSpeaker < 0 || colCat < 0 || colLine < 0 || colText < 0) {
        setStatus(false, u"%1 needs Speaker, Category, Line and Text columns (as Export CSV writes them)."_s.arg(QFileInfo(path).fileName()));
        return false;
    }
    int changed = 0, missing = 0;
    while (at < text.size()) {
        const QStringList f = csvRecord(text, at);
        if (f.size() <= std::max({colSpeaker, colCat, colLine, colText}))
            continue;
        const QByteArray folder = f.at(colSpeaker).toUtf8(), cat = f.at(colCat).toUtf8();
        const int index = f.at(colLine).toInt() - 1;
        Line* target = nullptr;
        for (Speaker& s : m_speakers)
            if (s.folder == folder)
                for (Category& c : s.cats)
                    if (c.name == cat && index >= 0 && index < c.lines.size())
                        target = &c.lines[index];
        if (!target) {
            ++missing;
            continue;
        }
        bool any = false;
        const QByteArray newText = f.at(colText).toUtf8();
        if (newText != target->text) {
            target->text = newText;
            any = true;
        }
        if (colParts >= 0 && colParts < f.size()) {
            QList<QByteArray> parts;
            for (const auto& pList = f.at(colParts).split(u'|', Qt::SkipEmptyParts); const QString& p : pList)
                parts << p.trimmed().toUtf8();
            if (!parts.isEmpty() && parts != target->parts) {
                target->parts = parts;
                any = true;
            }
        }
        if (any)
            ++changed;
    }
    if (changed)
        edited();
    setStatus(true, u"Imported %1: %2 lines changed%3."_s.arg(QFileInfo(path).fileName()).arg(changed)
                        .arg(missing ? u", %1 rows not found"_s.arg(missing) : QString()));
    return true;
}

// --- Category info ---------------------------------------------------------------------------------------------

QVariantList CommentaryEditorManager::infoRows(const QString& filterIn) const {
    QVariantList out;
    const QString filter = filterIn.trimmed();
    for (int i = 0; i < m_info.size(); ++i) {
        const InfoCategory& c = m_info.at(i);
        const QString name = QString::fromUtf8(c.name);
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive))
            continue;
        const uchar* r = reinterpret_cast<const uchar*>(c.record.constData());
        QVariantList values;
        for (int f = 0; f < 6; ++f) {
            const quint32 bits = qFromLittleEndian<quint32>(r + 4 * f);
            float v;
            memcpy(&v, &bits, 4);
            values << double(v);
        }
        out << QVariantMap{{u"index"_s, i}, {u"name"_s, name},
                           {u"hash"_s, u"0x%1"_s.arg(fnv1a(c.name), 8, 16, QChar(u'0')).toUpper().replace(u"0X"_s, u"0x"_s)},
                           {u"values"_s, values}, {u"type"_s, int(r[24])}, {u"flag"_s, int(r[25])},
                           {u"extra"_s, QVariantList{int(r[26]), int(r[27])}}};
    }
    return out;
}

bool CommentaryEditorManager::setInfoValue(int index, int field, double value) {
    if (index < 0 || index >= m_info.size() || field < 0 || field >= 6)
        return false;
    const float v = float(value);
    quint32 bits;
    memcpy(&bits, &v, 4);
    uchar* p = reinterpret_cast<uchar*>(m_info[index].record.data()) + 4 * field;
    if (qFromLittleEndian<quint32>(p) == bits)
        return false;
    qToLittleEndian(bits, p);
    edited();
    return true;
}

bool CommentaryEditorManager::setInfoByte(int index, int field, int value) {
    if (index < 0 || index >= m_info.size() || field < 0 || field >= 4 || value < 0 || value > 255)
        return false;
    QByteArray& r = m_info[index].record;
    if (static_cast<quint8>(r.at(24 + field)) == value)
        return false;
    r[24 + field] = char(quint8(value));
    edited();
    return true;
}

int CommentaryEditorManager::addInfoCategory(const QString& nameIn, int copyFrom) {
    if (m_kind != u"categories"_s)
        return -1;
    const QString name = nameIn.trimmed();
    if (!validName(name)) {
        setStatus(false, u"A category name is letters, digits and _ (e.g. try_reply)."_s);
        return -1;
    }
    const QByteArray bytes = name.toUtf8();
    for (const InfoCategory& c : std::as_const(m_info))
        if (c.name == bytes) {
            setStatus(false, u"There's already a %1 category."_s.arg(name));
            return -1;
        }
    InfoCategory c{bytes, copyFrom >= 0 && copyFrom < m_info.size() ? m_info.at(copyFrom).record : m_info.value(0).record};
    if (c.record.size() != kInfoRecord)
        c.record = QByteArray(kInfoRecord, '\0');
    // The game's lists are "default", "subcategorydefault", then A-Z: keep that.
    int at = int(m_info.size());
    for (int i = 2; i < m_info.size(); ++i)
        if (m_info.at(i).name > bytes) {
            at = i;
            break;
        }
    m_info.insert(at, c);
    edited();
    setStatus(true, u"Added %1 (with the settings of %2)."_s.arg(name, QString::fromUtf8(copyFrom >= 0 && copyFrom < m_info.size() ? m_info.at(copyFrom >= at ? copyFrom + 1 : copyFrom).name : QByteArray("default"))));
    return at;
}

bool CommentaryEditorManager::removeInfoCategory(int index) {
    if (index < 0 || index >= m_info.size())
        return false;
    if (index < 2) {
        setStatus(false, u"default and subcategorydefault are the settings every other category falls back on: they stay."_s);
        return false;
    }
    m_info.removeAt(index);
    edited();
    return true;
}
