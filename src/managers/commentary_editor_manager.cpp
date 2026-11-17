#include "commentary_editor_manager.h"

#include "app_orchestrator.h"
#include "blobset_asset_reader.h"
#include "blobset_entry_registry.h"
#include "compression_io.h"
#include "wem_file.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
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
// RL26's and Rugby 25's commentary files (blobset path hash, path). The commentary pair has the same path (so the
// same hash) in both.
constexpr KnownFile kFiles[] = {
    {"99b3b6554a5750d3", "sound\\lines.commentary.cmtry", "Commentary Lines"},
    {"eab8a37c7d0ebc40", "sound\\categoryinfo.commentary.cmtry", "Commentary Category Info"},
    {"01d4046906cce436", "sound\\lines.referee.dat", "Referee Lines"},                     // RL26.
    {"33b0057fd1fbd803", "sound\\categoryinfo.referee.cmtry", "Referee Category Info"},
    {"1237e800334320f9", "sound\\lines.announcer.dat", "Announcer Lines"},                 // Rugby 25.
    {"653250a988c75224", "sound\\categoryinfo.announcer.cmtry", "Announcer Category Info"},
    {"6a3af89b4d8e750a", "sound\\lines.tmoreferee.dat", "TMO Referee Lines"},
    {"eda867c16daa3ced", "sound\\categoryinfo.tmoreferee.cmtry", "TMO Referee Category Info"},
    {"0c4e4b61775252a8", "sound\\lines.career.dat", "Career Lines"},                       // Tiebreak, Cricket 24.
    {"381fc2571b104a7e", "sound\\lines.announcers.dat", "Announcer Lines"},
    {"fee8162874714384", "sound\\lines.chairumpires.dat", "Chair Umpire Lines"},
    {"c603e4a544b912e6", "sound\\lines.ftue.dat", "Tutorial (FTUE) Lines"},             // Cricket 24.
};
// AFL 23 and Cricket 24 keep their commentary (and Cricket 24 its announcer) lines in the same files as RL26 / Rugby
// 25, as "lines.commentary.dat" / "lines.announcer.dat", in Tiebreak's flat layout.
// The first four are RL26's; the others are listed only when the loaded blobset has them.
constexpr int kRl26Files = 4;

// A line's 16 byte id block: the sound id (10 digits, zero terminated), then RL26's u8 parts, u8 pronoun, u8 0,
// u8 phrase, u8 0, or Rugby 25's u8 speaker id, u8 parts, u8 phrase, u8 pronoun, u8 0.
struct IdLayout {
    int parts, pronoun, phrase, speaker;   // speaker -1: not stored.
};
constexpr IdLayout kRl26Layout{11, 12, 14, -1};
constexpr IdLayout kR25Layout{12, 14, 13, 11};
const IdLayout& layoutFor(bool r25) {
    return r25 ? kR25Layout : kRl26Layout;
}

constexpr int kConditionBytes = 64;
// sound/dialogueconditions (the sub-categories' conditions, see the header).
constexpr const char* kConditionsHash = "cef8f1afec442bf1";
constexpr int kConditionRecord = 120;
constexpr int kConditionNameBytes = 48;
constexpr qint32 kNoLimit = 100000;
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
    CommentaryEditorManager reader;
    try {
        // Tiebreak's files don't name their speakers' folders (the scripts' soundmap does), so no names from them.
        if (!reader.parseLinesAuto(unpackZstdChunks(dataIn)) || reader.m_flat)
            return {};
    } catch (const std::exception&) {
        return {};
    }
    QSet<QString> seen;
    for (const Speaker& sp : std::as_const(reader.m_speakers)) {
        const QString folder = QString::fromLatin1(sp.folder);
        for (const Category& c : std::as_const(sp.cats))
            for (const Line& line : std::as_const(c.lines))
                for (const QByteArray& part : std::as_const(line.parts)) {
                    if (part.startsWith("c:"))   // "c:<category>" plays another category's line.
                        continue;
                    const QString name = u"sound/"_s + folder + QString::fromLatin1(part);
                    if (!seen.contains(name)) {
                        seen.insert(name);
                        out << name;
                    }
                }
    }
    return out;
}

QList<CommentaryEditorManager::SpeakerCategories> CommentaryEditorManager::speakerCategories(const QByteArray& dataIn,
                                                                                            const QString& fileName) {
    QList<SpeakerCategories> out;
    const QByteArray data = unpackZstdChunks(dataIn);
    // A flat file (AFL 23, Cricket 24, Tiebreak): each speaker's categories that have lines for it.
    if (data.size() >= 12 && Reader{data}.u32(4) == 12 + 4 * qint64(Reader{data}.u32(0))) {
        CommentaryEditorManager reader;
        try {
            if (!reader.parseFlat(data))
                return {};
        } catch (const std::exception&) {
            return {};
        }
        reader.assignFlatFolders(fileName);
        for (const Speaker& sp : std::as_const(reader.m_speakers)) {
            SpeakerCategories s;
            s.name = speakerName(sp.folder, QByteArray());
            for (const Category& c : sp.cats)
                if (!c.lines.isEmpty())
                    s.categories.insert(QString::fromLatin1(c.name));
            if (!s.categories.isEmpty())
                out << s;
        }
        return out;
    }
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
    // The files the loaded blobset has (RL26's four, Rugby 25's six); RL26's when it has none of them.
    QVariantList out, rl26;
    int index = 0;
    for (const KnownFile& f : std::as_const(kFiles)) {
        BlobsetEntry entry;
        const bool available = BlobsetEntryRegistry::findEntry(QString::fromLatin1(f.hash), entry);
        // The mapped name when the file has its real one (AFL 23's / Cricket 24's are ".dat", RL26's ".cmtry").
        QString path = QString::fromLatin1(f.path);
        if (const AppOrchestrator* orchestrator = AppOrchestrator::instance(); available && orchestrator)
            if (const QString mapped = orchestrator->pathFromHash(QString::fromLatin1(f.hash));
                mapped.startsWith(u"sound"_s, Qt::CaseInsensitive))
                path = QString(mapped).replace(u'/', u'\\');
        const QVariantMap file{{u"name"_s, QString::fromLatin1(f.name)}, {u"path"_s, path},
                               {u"hash"_s, QString::fromLatin1(f.hash)}, {u"available"_s, available}};
        if (available)
            out << file;
        if (index++ < kRl26Files)
            rl26 << file;
    }
    return out.isEmpty() ? rl26 : out;
}

QStringList CommentaryEditorManager::pronouns() const {
    if (m_r25)
        return {u"Any player"_s, u"He / him"_s, u"She / her"_s, u"They / them"_s};
    return {u"They / them (any player)"_s, u"He / him"_s, u"She / her"_s};
}

bool CommentaryEditorManager::parseLinesAuto(const QByteArray& data) {
    // RL26's or Rugby 25's id block: the one whose lines rebuild the file exactly (the other reads garbage parts, or
    // runs off the end).
    for (const bool r25 : {false, true}) {
        try {
            if (parseLines(data, r25) && serializeLines() == data)
                return true;
        } catch (const std::exception&) {
        }
    }
    // Tiebreak's layout (no speakers, see the header).
    try {
        if (parseFlat(data) && serializeLines() == data)
            return true;
    } catch (const std::exception&) {
    }
    // None rebuilds it: read it as RL26's (a file this editor wrote, ...), or Tiebreak's when it has that header.
    if (data.size() >= 12 && Reader{data}.u32(4) == 12 + 4 * qint64(Reader{data}.u32(0)))
        return parseFlat(data);
    return parseLines(data, false);
}

bool CommentaryEditorManager::parseFlat(const QByteArray& data) {
    const IdLayout& layout = kR25Layout;
    const Reader r{data};
    const quint32 count = r.u32(0);
    if (count > 100000 || r.u32(4) != 12 + 4 * qint64(count))
        throw std::runtime_error("no Tiebreak lines header");
    const QList<QByteArray> names = r.names(r.u32(4));
    if (names.size() != qsizetype(count))
        throw std::runtime_error("bad category names");
    const qsizetype entries = r.u32(8);
    // Every category's lines, by the speaker id each line has (byte 11 of its id block).
    QMap<quint32, Speaker> bySpeaker;
    QList<QList<QByteArray>> subs;
    QList<QList<quint32>> orders;
    for (quint32 ci = 0; ci < count; ++ci) {
        subs << r.names(r.u32(12 + 4 * qsizetype(ci)));
        orders << QList<quint32>();
        const qsizetype e = entries + 12 * qsizetype(ci);
        qsizetype lineAt = r.u32(e);
        const quint32 lineCount = r.u32(e + 4);
        if (lineCount > 100000 || r.u32(e + 8) != ci)
            throw std::runtime_error("bad category entry");
        for (quint32 li = 0; li < lineCount; ++li) {
            Line line;
            line.conditions = r.bytes(lineAt, kConditionBytes);
            line.idBlock = r.bytes(lineAt + kConditionBytes, kIdBytes);
            qsizetype s = lineAt + kConditionBytes + kIdBytes;
            const int parts = static_cast<quint8>(line.idBlock.at(layout.parts));
            for (int p = 0; p < parts; ++p)
                line.parts << r.cstr(s, &s);
            line.text = line.originalText = r.cstr(s, &s);
            const quint32 id = static_cast<quint8>(line.idBlock.at(layout.speaker));
            orders.last() << id;
            Speaker& sp = bySpeaker[id];
            sp.id = id;
            while (sp.cats.size() <= qsizetype(ci))
                sp.cats << Category();
            sp.cats[ci].lines << line;
            lineAt = (s + 15) / 16 * 16;
        }
    }
    if (bySpeaker.isEmpty())
        bySpeaker[0].id = 0;
    QList<Speaker> speakers;
    for (Speaker& sp : bySpeaker) {
        while (sp.cats.size() < qsizetype(count))
            sp.cats << Category();
        for (quint32 ci = 0; ci < count; ++ci) {
            sp.cats[ci].name = names.at(ci);
            sp.cats[ci].subs = subs.at(ci);
            sp.cats[ci].flatOrder = orders.at(ci);
        }
        sp.folder = "id" + QByteArray::number(sp.id) + '/';   // Until assignFlatFolders knows the real one.
        speakers << sp;
    }
    m_title = "Lines";
    m_speakers = speakers;
    m_r25 = true;
    m_flat = true;
    m_info.clear();
    return true;
}

QByteArray CommentaryEditorManager::serializeFlat() const {
    // Speaker 0's categories (names, sub-categories) are every speaker's; a category's lines are the speakers' in
    // id order (the game's files keep them that way).
    const QList<Category> cats = m_speakers.isEmpty() ? QList<Category>() : m_speakers.first().cats;
    const qsizetype count = cats.size();
    QByteArray out;
    put32(out, quint32(count));
    put32(out, quint32(12 + 4 * count));
    put32(out, 0);
    out.append(QByteArray(4 * count, '\0'));
    QList<QByteArray> names;
    for (const Category& c : cats)
        names << c.name;
    out += nameTable(names);
    const qsizetype entries = out.size();
    set32(out, 8, quint32(entries));
    out.append(QByteArray(12 * count, '\0'));
    align16(out);
    QList<qsizetype> firsts;
    QList<quint32> counts;
    for (qsizetype ci = 0; ci < count; ++ci) {
        qsizetype first = 0;
        quint32 lines = 0;
        // The lines in the file's order (each speaker's next line for each id in flatOrder), then any left over (added
        // ones) speaker by speaker.
        QList<QPair<const Speaker*, const Line*>> order;
        QHash<quint32, qsizetype> taken;
        QHash<quint32, const Speaker*> byId;
        for (const Speaker& sp : m_speakers)
            byId.insert(sp.id, &sp);
        for (const quint32 id : cats.at(ci).flatOrder) {
            const Speaker* sp = byId.value(id);
            if (!sp || ci >= sp->cats.size())
                continue;
            qsizetype& k = taken[id];
            if (k < sp->cats.at(ci).lines.size())
                order << qMakePair(sp, &sp->cats.at(ci).lines.at(k++));
        }
        for (const Speaker& sp : m_speakers) {
            if (ci >= sp.cats.size())
                continue;
            for (qsizetype k = taken.value(sp.id); k < sp.cats.at(ci).lines.size(); ++k)
                order << qMakePair(&sp, &sp.cats.at(ci).lines.at(k));
        }
        for (const auto& [speaker, linePtr] : std::as_const(order)) {
            const Speaker& sp = *speaker;
            const Line& line = *linePtr;
            if (lines++ == 0)
                first = out.size();
            out += line.conditions;
            QByteArray id = line.idBlock;
            id[kR25Layout.parts] = char(quint8(line.parts.size()));
            id[kR25Layout.speaker] = char(quint8(sp.id));
            out += id;
            for (const QByteArray& part : line.parts) {
                out += part;
                out.append('\0');
            }
            out += line.text;
            out.append('\0');
            align16(out);
        }
        firsts << first;
        counts << lines;
    }
    for (qsizetype ci = 0; ci < count; ++ci) {
        set32(out, 12 + 4 * ci, quint32(out.size()));
        out += nameTable(cats.at(ci).subs);
        set32(out, entries + 12 * ci, quint32(firsts.at(ci)));
        set32(out, entries + 12 * ci + 4, counts.at(ci));
        set32(out, entries + 12 * ci + 8, quint32(ci));
    }
    return out;
}

void CommentaryEditorManager::assignFlatFolders(const QString& fileName) {
    // "lines.chairumpires.dat" -> chairumpires; its speakers' folders are "chairumpires/id<N>_<name>/", named in the
    // scripts' soundmap, so they're taken from the game's (resolved) sound paths: sound\chairumpires\id1_dlauritsen\...
    const QString file = QString(fileName).replace(u'\\', u'/').toLower();
    const qsizetype at = file.lastIndexOf(u"lines."_s);
    const QString kind = at < 0 ? QString() : file.mid(at + 6).section(u'.', 0, 0);
    static const QHash<QString, QString> titles = {{u"chairumpires"_s, u"Chair Umpires"_s},
                                                   {u"announcers"_s, u"Announcers"_s}, {u"career"_s, u"Career"_s},
                                                   {u"commentary"_s, u"Commentary"_s}, {u"announcer"_s, u"Announcer"_s},
                                                   {u"ftue"_s, u"Tutorial (FTUE)"_s}};
    m_title = titles.value(kind, kind.isEmpty() ? u"Lines"_s : kind).toUtf8();
    if (kind.isEmpty())
        return;
    QHash<quint32, QString> folders;
    if (const AppOrchestrator* orchestrator = AppOrchestrator::instance()) {
        const QString root = u"sound/"_s + kind + u"/id"_s;
        static const QRegularExpression speaker(u"^(id(\\d+)_[a-z0-9_]+)/"_s);
        for (const QString& path : orchestrator->mappedPaths()) {
            if (!path.startsWith(root))
                continue;
            if (const auto m = speaker.match(path.mid(root.size() - 2)); m.hasMatch())
                folders.insert(m.captured(2).toUInt(), m.captured(1));
        }
    }
    for (Speaker& sp : m_speakers)
        sp.folder = (kind + u'/' + folders.value(sp.id, u"id%1"_s.arg(sp.id)) + u'/').toUtf8();
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
    // The file's mapped path (what the mods folder needs), else the known files' own, else the one given.
    QString path = workspacePathIn;
    for (const KnownFile& f : std::as_const(kFiles))
        if (QString::fromLatin1(f.hash).toULongLong(nullptr, 16) == hexHash.toULongLong(nullptr, 16))
            path = QString::fromLatin1(f.path);
    if (const QString mapped = orchestrator->pathFromHash(hexHash); mapped.startsWith(u"sound"_s, Qt::CaseInsensitive))
        path = mapped;
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
    const bool r25 = m_r25;
    const bool flat = m_flat;
    bool ok = false;
    QString kind;
    try {
        // A category info file starts with its count twice (u32 count, u32 records, u32 count).
        if (data.size() >= 12 && Reader{data}.u32(0) == Reader{data}.u32(8) && Reader{data}.u32(4) <= quint32(data.size())) {
            ok = parseInfo(data);
            kind = u"categories"_s;
        } else {
            ok = parseLinesAuto(data);
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
        m_r25 = r25;
        m_flat = flat;
        m_kind = kindBefore;
        if (m_statusOk)
            setStatus(false, u"%1 isn't a commentary file this editor knows."_s.arg(sourceName));
        return false;
    }
    m_kind = kind;
    if (m_kind == u"lines"_s && m_flat)
        assignFlatFolders(workspacePath.isEmpty() ? (filePath.isEmpty() ? sourceName : filePath) : workspacePath);
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
        loadConditions();
        int lineCount = 0;
        for (const Speaker& s : std::as_const(m_speakers))
            for (const Category& c : std::as_const(s.cats))
                lineCount += int(c.lines.size());
        setStatus(true, u"Loaded %1: %2 speakers, %3 lines%4."_s.arg(sourceName).arg(m_speakers.size()).arg(lineCount)
                            .arg(m_conditions.isEmpty() ? QString()
                                                        : u", %1 dialogue conditions"_s.arg(m_conditions.size())));
    } else {
        setStatus(true, u"Loaded %1: %2 categories."_s.arg(sourceName).arg(m_info.size()));
    }
    return true;
}

bool CommentaryEditorManager::parseLines(const QByteArray& data, bool r25) {
    const IdLayout& layout = layoutFor(r25);
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
                const int parts = static_cast<quint8>(line.idBlock.at(layout.parts));
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
    m_r25 = r25;
    m_flat = false;
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
    if (m_flat)
        return serializeFlat();
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
                id[layoutFor(m_r25).parts] = char(quint8(line.parts.size()));
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
    // New sub-categories need their conditions in sound/dialogueconditions too.
    QString conditionsNote;
    if (m_kind == u"lines"_s && m_conditionsAdded > 0) {
        if (m_conditionsPath.isEmpty()) {
            conditionsNote = u" Warning: the dialogue conditions file isn't in the file mapping, so the %1 new "
                             u"condition(s) weren't saved (run Resolve File Names)."_s.arg(m_conditionsAdded);
        } else {
            const QString conditionsFile = QDir::cleanPath(QDir(mods).filePath(m_conditionsPath));
            if (!writeFile(conditionsFile, serializeConditions()))
                return {};
            conditionsNote = u" Added %1 condition(s) to %2."_s.arg(m_conditionsAdded)
                                 .arg(QDir::toNativeSeparators(conditionsFile));
            m_conditionsAdded = 0;
        }
    }
    m_filePath = path;
    m_modified = false;
    emit modifiedChanged();
    setStatus(true, u"Saved to %1.%2 Run Modify to put it in the game."_s.arg(QDir::toNativeSeparators(path), conditionsNote));
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
    setStatus(true, u"Saved to %1.%2"_s.arg(QDir::toNativeSeparators(path),
                                          m_conditionsAdded > 0 ? u" The %1 new dialogue condition(s) are only saved by "
                                                                  u"Save to Mods."_s.arg(m_conditionsAdded)
                                                                : QString()));
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
                           {u"parts"_s, parts.join(u"  +  "_s)}, {u"gender"_s, static_cast<quint8>(line.idBlock.at(layoutFor(m_r25).pronoun))},
                           {u"subNames"_s, maskNames(condition(line, kSubMask), c->subs).join(u", "_s)},
                           {u"flag"_s, line.idBlock.at(layoutFor(m_r25).phrase) != 0},
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
    QStringList subTexts;
    const Category* c = categoryAt(speaker, category);
    for (int b = 0; b < c->subs.size() && b < 64; ++b) {
        subFlags << bool(mask & (quint64(1) << b));
        subTexts << describeCondition(QString::fromUtf8(c->subs.at(b)));
    }
    return {{u"id"_s, lineId(*line)}, {u"text"_s, QString::fromUtf8(line->text)},
            {u"originalText"_s, line->added ? QString() : QString::fromUtf8(line->originalText)},
            {u"parts"_s, parts}, {u"gender"_s, static_cast<quint8>(line->idBlock.at(layoutFor(m_r25).pronoun))},
            {u"flag"_s, line->idBlock.at(layoutFor(m_r25).phrase) != 0}, {u"conditions"_s, conditions},
            {u"subcategories"_s, subcategories(speaker, category)}, {u"subFlags"_s, subFlags},
            {u"subTexts"_s, subTexts}, {u"added"_s, line->added}};
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
    const IdLayout& layout = layoutFor(m_r25);
    line.idBlock[layout.parts] = 1;
    line.idBlock[layout.pronoun] = char(quint8(qBound(0, gender, int(pronouns().size()) - 1)));
    line.idBlock[layout.phrase] = phrase ? 1 : 0;   // A whole sentence (names spliced into sentences have 0).
    if (layout.speaker >= 0)   // Rugby 25: the speaker's id, like its other lines.
        line.idBlock[layout.speaker] = c->lines.isEmpty() ? char(quint8(m_speakers.at(speaker).id))
                                                          : c->lines.first().idBlock.at(layout.speaker);
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
    const int at = layoutFor(m_r25).pronoun;
    if (!line || gender < 0 || gender > 255 || static_cast<quint8>(line->idBlock.at(at)) == gender)
        return false;
    line->idBlock[at] = char(quint8(gender));
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
    const int at = layoutFor(m_r25).phrase;
    if (!line || (line->idBlock.at(at) != 0) == on)
        return false;
    line->idBlock[at] = on ? 1 : 0;
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
        if (const IdLayout& layout = layoutFor(m_r25); layout.speaker >= 0)
            line.idBlock[layout.speaker] = char(quint8(m_speakers.at(speaker).id));
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
    if (m_flat) {   // Tiebreak: the categories are every speaker's.
        for (Speaker& other : m_speakers)
            other.cats << c;
    } else {
        s.cats << c;
    }
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
    if (m_flat)
        for (Speaker& other : m_speakers)
            if (category < other.cats.size())
                other.cats[category].name = c->name;
    edited();
    return true;
}

bool CommentaryEditorManager::removeCategory(int speaker, int category) {
    if (!categoryAt(speaker, category))
        return false;
    if (m_flat) {
        for (Speaker& other : m_speakers)
            if (category < other.cats.size())
                other.cats.removeAt(category);
    } else {
        m_speakers[speaker].cats.removeAt(category);
    }
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
        if (!validName(t.startsWith(u'!') ? t.mid(1) : t)) {   // "!name": the condition's opposite.
            setStatus(false, u"\"%1\": a sub-category name is letters, digits and _ # + - . only (and ! first)."_s.arg(t));
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
    // Each name is a dialogue condition: new ones are added to sound/dialogueconditions (saved with Save to Mods),
    // as long as the game has the variable they test.
    QList<Condition> added;
    if (!m_conditions.isEmpty()) {
        for (const QByteArray& name : std::as_const(names)) {
            if (m_conditionIndex.contains(name) || std::any_of(added.cbegin(), added.cend(),
                                                               [&name](const Condition& a) { return a.name == name; }))
                continue;
            Condition condition;
            if (const QString why = conditionFromName(name, &condition); !why.isEmpty()) {
                setStatus(false, why);
                return false;
            }
            added << condition;
        }
    }
    for (const Condition& condition : std::as_const(added)) {
        m_conditionIndex.insert(condition.name, int(m_conditions.size()));
        m_conditions << condition;
    }
    m_conditionsAdded += int(added.size());
    if (!added.isEmpty()) {
        QStringList addedNames;
        for (const Condition& a : std::as_const(added))
            addedNames << QString::fromUtf8(a.name);
        setStatus(true, u"New dialogue condition(s): %1. They're added to sound/dialogueconditions by Save to Mods."_s
                            .arg(addedNames.join(u", "_s)));
    }
    // Each line keeps the sub-categories it had by name (they may have moved, been renamed away or removed). Tiebreak:
    // the category is every speaker's.
    QList<Category*> targets{c};
    if (m_flat)
        for (int s = 0; s < m_speakers.size(); ++s)
            if (s != speaker && category < m_speakers[s].cats.size())
                targets << &m_speakers[s].cats[category];
    for (Category* target : std::as_const(targets)) {
        for (Line& line : target->lines) {
            const quint64 mask = condition(line, kSubMask);
            quint64 next = 0;
            for (int b = 0; b < names.size(); ++b) {
                const int old = int(target->subs.indexOf(names.at(b)));
                if (old >= 0 && old < 64 && (mask & (quint64(1) << old)))
                    next |= quint64(1) << b;
            }
            setCondition(line, kSubMask, next);
        }
        target->subs = names;
    }
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

// --- Dialogue conditions -----------------------------------------------------------------------------------------

void CommentaryEditorManager::loadConditions() {
    m_conditions.clear();
    m_conditionIndex.clear();
    m_variables.clear();
    m_conditionsPath.clear();
    m_conditionsAdded = 0;

    BlobsetEntry entry;
    const AppOrchestrator* orchestrator = AppOrchestrator::instance();
    if (!orchestrator || !BlobsetEntryRegistry::findEntry(QString::fromLatin1(kConditionsHash), entry))
        return;   // Not RL26 (or no game loaded): the sub-categories are just names.
    m_conditionsPath = orchestrator->pathFromHash(QString::fromLatin1(kConditionsHash)).replace(u'\\', u'/');

    QByteArray data;
    try {
        // An edited copy in the mods folder wins, like the lines file.
        QFile mod(m_conditionsPath.isEmpty() ? QString() : orchestrator->modFilePath(m_conditionsPath));
        if (!m_conditionsPath.isEmpty() && orchestrator->hasModFile(m_conditionsPath) && mod.open(QIODevice::ReadOnly))
            data = mod.readAll();
        else
            data = BlobsetAssetReader::read(entry, orchestrator->assetLocation(), orchestrator->blobsetVersion(),
                                            orchestrator->platform());
        data = unpackZstdChunks(data);

        const Reader r{data};
        const quint32 count = r.u32(0);
        if (count > 100000 || 4 + qsizetype(count) * kConditionRecord > data.size())
            throw std::runtime_error("bad dialogue conditions file");
        const auto field = [&data](qsizetype at) {
            const QByteArray bytes = data.mid(at, kConditionNameBytes);
            const qsizetype end = bytes.indexOf('\0');
            return end >= 0 ? bytes.left(end) : bytes;
        };
        for (quint32 i = 0; i < count; ++i) {
            const qsizetype at = 4 + qsizetype(i) * kConditionRecord;
            Condition c;
            c.name = field(at);
            c.variable = field(at + kConditionNameBytes);
            c.negate = r.u32(at + 96);
            c.ranges = r.u32(at + 100);
            for (int v = 0; v < 4; ++v)
                c.values[v] = qint32(r.u32(at + 104 + 4 * v));
            m_conditionIndex.insert(c.name, int(m_conditions.size()));
            m_variables.insert(c.variable);
            m_conditions << c;
        }
    } catch (const std::exception&) {
        m_conditions.clear();
        m_conditionIndex.clear();
        m_variables.clear();
    }
}

QString CommentaryEditorManager::conditionFromName(const QByteArray& nameIn, Condition* out) const {
    const QString name = QString::fromUtf8(nameIn);
    if (nameIn.size() >= kConditionNameBytes)
        return u"\"%1\": a condition name is at most %2 characters."_s.arg(name).arg(kConditionNameBytes - 1);
    Condition c;
    c.name = nameIn;
    QString rest = name;
    if (rest.startsWith(u'!')) {
        c.negate = 1;
        rest.remove(0, 1);
    }
    // "<variable>_#<range>[_#<range>]": N, N+, N- or A-B.
    static const QRegularExpression range(u"^(\\d+)(?:(\\+)|-(\\d+)?)?$"_s);
    const QStringList pieces = rest.split(u"_#"_s);
    QString variable = pieces.first();
    QList<std::pair<qint32, qint32>> ranges;
    for (qsizetype i = 1; i < pieces.size(); ++i) {
        const auto m = range.match(pieces.at(i));
        if (!m.hasMatch())
            return u"\"%1\": \"#%2\" isn't a value the game understands (use #5, #5+, #5- or #5-8)."_s
                .arg(name, pieces.at(i));
        const qint32 v = m.captured(1).toInt();
        if (!m.captured(2).isEmpty())
            ranges.append({v, kNoLimit});
        else if (!m.captured(3).isEmpty())
            ranges.append({v, m.captured(3).toInt()});
        else if (pieces.at(i).endsWith(u'-'))
            ranges.append({-kNoLimit, v});
        else
            ranges.append({v, v});
        variable += u"_X"_s;
    }
    if (ranges.size() > 2)
        return u"\"%1\": a condition tests at most two values."_s.arg(name);
    c.variable = variable.toUtf8();
    if (!m_variables.contains(c.variable)) {
        QString base = variable;
        base.remove(u"_X"_s);
        return u"\"%1\": the game has no condition on \"%2\"%3. Pick one from the list (or a new value of one)."_s
            .arg(name, base, ranges.isEmpty() ? QString() : u" with %1 value(s)"_s.arg(ranges.size()));
    }
    c.ranges = quint32(std::max<qsizetype>(1, ranges.size()));
    for (qsizetype i = 0; i < ranges.size(); ++i) {
        c.values[2 * i] = ranges.at(i).first;
        c.values[2 * i + 1] = ranges.at(i).second;
    }
    if (out)
        *out = c;
    return {};
}

QString CommentaryEditorManager::conditionText(const Condition& c) {
    QString variable = QString::fromUtf8(c.variable);
    const int values = int(variable.count(u"_X"_s));
    variable.remove(u"_X"_s);
    if (values == 0)
        return c.negate ? u"not %1"_s.arg(variable) : variable;
    const auto rangeText = [](qint32 lo, qint32 hi) {
        if (lo == hi)
            return u"%1"_s.arg(lo);
        if (hi >= kNoLimit)
            return u"%1 or more"_s.arg(lo);
        if (lo <= -kNoLimit)
            return u"%1 or less"_s.arg(hi);
        return u"%1 to %2"_s.arg(lo).arg(hi);
    };
    QString text = variable + u" is "_s + rangeText(c.values[0], c.values[1]);
    if (values > 1)
        text += u", then "_s + rangeText(c.values[2], c.values[3]);
    return c.negate ? u"not ("_s + text + u")"_s : text;
}

QString CommentaryEditorManager::describeCondition(const QString& name) const {
    if (m_conditions.isEmpty())
        return {};
    const QByteArray key = name.toUtf8();
    if (const auto it = m_conditionIndex.constFind(key); it != m_conditionIndex.constEnd())
        return conditionText(m_conditions.at(it.value()));
    Condition c;
    const QString why = conditionFromName(key, &c);
    return why.isEmpty() ? conditionText(c) + u" (new)"_s : u"Unknown: "_s + why;
}

QVariantList CommentaryEditorManager::conditionList(const QString& filterIn) const {
    const QString filter = filterIn.trimmed();
    QList<const Condition*> sorted;
    sorted.reserve(m_conditions.size());
    for (const Condition& c : std::as_const(m_conditions))
        sorted << &c;
    std::sort(sorted.begin(), sorted.end(), [](const Condition* a, const Condition* b) {
        const QByteArray ka = a->name.startsWith('!') ? a->name.mid(1) : a->name;
        const QByteArray kb = b->name.startsWith('!') ? b->name.mid(1) : b->name;
        return ka != kb ? ka < kb : a->name < b->name;
    });
    QVariantList out;
    for (const Condition* c : std::as_const(sorted)) {
        const QString name = QString::fromUtf8(c->name);
        const QString text = conditionText(*c);
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive) && !text.contains(filter, Qt::CaseInsensitive))
            continue;
        out << QVariantMap{{u"name"_s, name}, {u"text"_s, text}};
    }
    return out;
}

QByteArray CommentaryEditorManager::serializeConditions() const {
    QByteArray out;
    out.reserve(4 + m_conditions.size() * kConditionRecord);
    put32(out, quint32(m_conditions.size()));
    for (const Condition& c : std::as_const(m_conditions)) {
        out += c.name.left(kConditionNameBytes - 1).leftJustified(kConditionNameBytes, '\0', true);
        out += c.variable.left(kConditionNameBytes - 1).leftJustified(kConditionNameBytes, '\0', true);
        put32(out, c.negate);
        put32(out, c.ranges);
        for (const qint32 v : c.values)
            put32(out, quint32(v));
    }
    return out;
}
