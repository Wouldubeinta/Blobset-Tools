#include "name_resolver_worker.h"
#include "name_resolver_skinned_names.h"
#include "name_resolver_rll4_names.h"

#include "blobset_asset_reader.h"
#include "commentary_editor_manager.h"
#include "blobset_file.h"
#include "core_defs.h"
#include "file_mapping_worker.h"
#include "hash_utility.h"
#include "compression_io.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <exception>

using namespace Qt::StringLiterals;

#include "rll4_hair_styles.inc"
#include "rll3_hair_beards.inc"

namespace {

constexpr qsizetype kMinStringLength = 4;
constexpr qsizetype kMaxStringLength = 240;
constexpr qint64 kMaxExecutableBytes = 256LL * 1024 * 1024;
constexpr quint32 kMaxAssetMainBytes = 1024 * 1024;   // Larger assets are geometry / pixels, not names.
// Unnamed files are read up to this: the big V4 meshes keep their material (texture paths -> model folders) at the end.
constexpr quint32 kMaxUnnamedAssetMainBytes = 16 * 1024 * 1024;
// Mem packs list their files' names up front (Rugby 25's noesis\video.mempack, 28 MB, names its three menu videos).
constexpr quint32 kMaxMemPackMainBytes = 64 * 1024 * 1024;
constexpr int kMaxHarvestedAssets = 40000;

/// Which mapped file kinds a type can be. A guessed type is only accepted when the file's current extension is listed;
/// a type spelt out in a string ("x.mempack") is also accepted for types that aren't in this table.
const QHash<QString, QStringList>& compatibleExtensions() {
    static const QHash<QString, QStringList> table = {
        {u"texture"_s, {u"dds"_s, u"txpk"_s}},
        {u"texpack"_s, {u"txpk"_s, u"txpl"_s}},   // A texture pack, or (V4) the list of a model's textures.
        {u"modelcomponent"_s, {u"mdl"_s}},         // V4 character parts (heads, hair, beards, ...).
        {u"blendshapes"_s, {u"mdat"_s}},           // ... and their blend shapes.
        {u"hjoffsets"_s, {u"hjo"_s}},
        {u"nodefile"_s, {u"crowd"_s, u"dat"_s}},
        {u"collision"_s, {u"col"_s}},
        {u"vbo"_s, {u"idx"_s}},                       // RLL4 / RLL3: a character part's vertex buffer (model\index).
        // .mob: RLL4's crowd models ("MOB!"). .m3mp: AFL Live's models are mem packs of one "<name>.gz.pc" (the
        // generated player heads "generatedheads/<initial><surname>", stadium fields / crowds, impostors).
        {u"model"_s, {u"mesh"_s, u"mdl"_s, u"mdat"_s, u"idx"_s, u"mob"_s, u"m3mp"_s}},
        {u"mempack"_s, {u"m3mp"_s}},
        {u"morphpack"_s, {u"idx"_s, u"meshpack"_s}},  // RLL2's head / hair morphs: "heads/<player>.morphpack".
        {u"material"_s, {u"mat"_s}},
        {u"anim"_s, {u"anim"_s, u"anims"_s}},
        // V4: "animations/<name>.animset" is mapped as a pack (.anims); RLL2 as a clip pack (.clippack).
        {u"animset"_s, {u"animset"_s, u"anims"_s, u"clippack"_s}},
        {u"seq"_s, {u"anim"_s}},                      // Sequences (camera / UI / prop animation): anim\cinematic.
        {u"bank"_s, {u"bank"_s, u"bnk"_s}},
        {u"soundbank"_s, {u"bank"_s, u"bnk"_s}},
        {u"wem"_s, {u"wem"_s, u"midi"_s}},         // Wwise MIDI sources are "sound/<id>.wem" too (Rugby 25).
        {u"bnk"_s, {u"bnk"_s}},
        {u"fsb"_s, {u"fsb"_s}},
        {u"gz"_s, {u"clips"_s}},                       // AFL Live's animation clip sets ("animations/<set>.gz").
        {u"wav"_s, {u"wav"_s}},
        {u"fnt"_s, {u"glyphs"_s, u"bmf"_s}},           // ... RLL2's bitmap fonts (font\N.bmf): "fonts/<name>.fnt".                     // A font's glyph table (font\glyphs), next to its _sdf texture.
    };
    return table;
}

/// Mapped kinds whose main section is worth scanning for names (materials name their textures, meshes their
/// materials and model folders, scripts their data files, ...).
bool worthScanning(const QString& extension) {
    static const QStringList kinds = {u"mat"_s, u"mesh"_s, u"mdl"_s, u"mdat"_s, u"luadata"_s, u"str"_s,
                                      u"names"_s, u"txt"_s, u"xml"_s, u"animset"_s, u"obj"_s, u"dat"_s,
                                      u"anims"_s, u"mob"_s, u"col"_s, u"glyphs"_s,
                                      // Texture lists / packs name their textures ("<model>/textures/..."), and
                                      // the V4 helper joint offsets, crowd nodes and body shapes their own names.
                                      u"txpl"_s, u"txpk"_s, u"hjo"_s, u"crowd"_s, u"bshp"_s,
                                      // Mem packs: noesis\xamls names every XAML ("<view>.xaml" -> its .texpack).
                                      u"m3mp"_s,
                                      // FMOD Designer projects (AFL Live's sound\afl.fev) name their wave banks.
                                      u"fev"_s,
                                      // RLL2's bitmap fonts keep their source ("...\intermediate\common\fonts\x.ttf").
                                      u"bmf"_s};
    return kinds.contains(extension);
}

QString extensionOf(const QString& mappedPath) {
    const qsizetype slash = mappedPath.lastIndexOf(u'\\');
    const qsizetype dot = mappedPath.lastIndexOf(u'.');
    return dot > slash ? mappedPath.mid(dot + 1).toLower() : QString();
}

/// Numbered names the file mapping invents for files it can't name ("model\static\123.mesh", "unknown\...").
bool isUnnamed(const QString& mappedPath) {
    if (mappedPath.startsWith(u"unknown\\"_s, Qt::CaseInsensitive))
        return true;
    // No folder: a chance match of an earlier resolver with a junk string ("blockiilen.wav", RLL2's commentary line
    // sound\commentary\0337124914.sound): open to its real name again. Every game keeps its files in folders.
    if (!mappedPath.contains(u'\\'))
        return true;
    QString name = mappedPath.mid(mappedPath.lastIndexOf(u'\\') + 1);
    name = name.left(name.indexOf(u'.'));
    if (name.isEmpty())
        return false;
    for (const QChar c : std::as_const(name))
        if (!c.isDigit())
            return false;
    return true;
}

bool isTypeWord(const QString& word) {
    if (word.size() < 2 || word.size() > 16)   // "modelcomponent"
        return false;
    for (const QChar c : std::as_const(word))
        if (c < u'a' || c > u'z')
            return false;
    return true;
}

} // namespace

NameResolverWorker::NameResolverWorker(const QString& blobsetFile, int version, bool isBigEndian,
                                       const QString& mappingFile, const QString& userNames, const QString& gameKey,
                                       int gameId, int platform, QObject* parent)
    : QObject(parent),
      m_blobsetFile(blobsetFile),
      m_version(version),
      m_isBigEndian(isBigEndian),
      m_mappingFile(mappingFile),
      m_userNames(userNames),
      m_gameKey(gameKey),
      m_gameId(gameId),
      m_platform(platform) {}

void NameResolverWorker::reportProgress(int percent, const QString& status) {
    percent = std::clamp(percent, 0, 100);
    if (percent == m_lastPercent)
        return;
    m_lastPercent = percent;
    emit progressUpdated(percent);
    if (!status.isEmpty())
        emit updateStatus(status);
}

void NameResolverWorker::reportRange(int from, int to, qint64 done, qint64 total, const QString& status) {
    const qint64 span = std::max<qint64>(1, total);
    reportProgress(from + int((to - from) * std::min(done, span) / span), status);
}

void NameResolverWorker::resolve_names() {
    m_lastPercent = -1;
    try {
        run();
    } catch (const std::exception& ex) {
        emit taskFinished(u"Resolving file names failed.\n\n"_s + QString::fromUtf8(ex.what()));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Candidate strings
// ---------------------------------------------------------------------------------------------------------------

void NameResolverWorker::addString(QString s) {
    s = s.trimmed().toLower();
    s.replace(u'\\', u'/');
    // Build machine paths ("d:/commit01/rl4/intermediate/common/fonts/numbers_sharks.ttf"): the part after the
    // build's data root is the asset's name.
    if (const qsizetype root = s.indexOf(u"/intermediate/common/"_s); root >= 0)
        s.remove(0, root + 21);
    if (s.size() < kMinStringLength || s.size() > kMaxStringLength || s.contains(u'%') || s.contains(u':'))
        return;

    // Build paths some tools leave in front of the real name.
    // ("data/blobs/": RLL3's texture packs name their textures by the pack's build path,
    // "data/blobs/characters/strapping/models/x/x/0/mat_map" in "characters/strapping/models/x/x.texpack".)
    for (const QString& prefix : {u"convant2-temp-intermediate/"_s, u"ant2-temp-intermediate/"_s, u"data/"_s, u"blobs/"_s}) {
        if (s.startsWith(prefix))
            s.remove(0, prefix.size());
    }
    while (s.startsWith(u"./"_s))
        s.remove(0, 2);
    while (s.startsWith(u'/'))
        s.remove(0, 1);
    while (s.endsWith(u'/') || s.endsWith(u'.'))
        s.chop(1);

    if (s.size() >= 2)
        m_strings.insert(s);
}

void NameResolverWorker::addStrings(const QByteArray& data) {
    const auto isNameChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'
               || c == '.' || c == '/' || c == '\\';
    };

    qsizetype start = -1;
    for (qsizetype i = 0; i <= data.size(); ++i) {
        const bool inName = i < data.size() && isNameChar(data[i]);
        if (inName && start < 0) {
            start = i;
        } else if (!inName && start >= 0) {
            const qsizetype length = i - start;
            if (length >= kMinStringLength && length <= kMaxStringLength)
                addString(QString::fromLatin1(data.constData() + start, length));
            start = -1;
        }
    }
}

void NameResolverWorker::addCharacterItems(const QByteArray& script) {
    // The compiled script's strings (LuaJIT keeps each behind a type byte of 5 + its length, so a name 40 characters
    // or longer runs into the "name" key in front of it: "name.adammcburney_moustache_pencil_handlebar02").
    static const QRegularExpression itemName(u"^[a-z][a-z0-9_]{2,63}$"_s);
    qsizetype start = -1;
    for (qsizetype i = 0; i <= script.size(); ++i) {
        const char c = i < script.size() ? script[i] : '\0';
        const bool inName = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
                            || c == '-' || c == '.' || c == '/';
        if (inName && start < 0) {
            start = i;
        } else if (!inName && start >= 0) {
            const QString s = QString::fromLatin1(script.constData() + start, i - start).toLower();
            if (itemName.match(s).hasMatch())
                m_characterItems.insert(s);
            if (s.size() >= 45 && s.startsWith(u"name"_s) && itemName.match(s.mid(5)).hasMatch())
                m_characterItems.insert(s.mid(5));
            start = -1;
        }
    }
    // The constants by their type byte (5 + length), which also finds the long ones that ran into the key in front
    // ("f_top_soft03_collar02_button_short" '6' "f_clothes_top_soft03_collar02_button_short_female").
    const auto isWordChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    for (qsizetype i = 0; i < script.size(); ++i) {
        const int length = static_cast<quint8>(script[i]) - 5;
        if (length < 3 || length > 64 || i + 1 + length > script.size())
            continue;
        bool word = true;
        for (qsizetype k = i + 1; k <= i + length && word; ++k)
            word = isWordChar(script[k]);
        if (!word)
            continue;
        const QString s = QString::fromLatin1(script.constData() + i + 1, length).toLower();
        if (itemName.match(s).hasMatch())
            m_characterItems.insert(s);
    }
}

void NameResolverWorker::addLuaConstants(const QByteArray& scripts) {
    // The compiled scripts' string constants by their LuaJIT type byte (5 + length): every one, also the ones 40
    // characters or longer, whose printable type byte runs them into their neighbours for addStrings
    // ("cutscene_new_serve_singles_receiver_ao_rb", a cutscene's animsetName in Tiebreak).
    const auto isWordChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    for (qsizetype i = 0; i < scripts.size(); ++i) {
        const int length = static_cast<quint8>(scripts[i]) - 5;
        if (length < 3 || length > 64 || i + 1 + length > scripts.size())
            continue;
        bool word = true;
        for (qsizetype k = i + 1; k <= i + length && word; ++k)
            word = isWordChar(scripts[k]);
        if (word)
            m_luaWords.insert(QString::fromLatin1(scripts.constData() + i + 1, length).toLower());
    }
}

void NameResolverWorker::harvestExecutables() {
    // The game folder (or the folders above it: RL26 / Cricket keep the blobset in a "data" sub folder).
    QDir dir = QFileInfo(m_blobsetFile).absoluteDir();
    QFileInfoList executables;
    for (int level = 0; level < 3; ++level) {
        const QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo& file : std::as_const(files)) {
            const QString suffix = file.suffix().toLower();
            const bool executable = suffix == u"exe"_s || suffix == u"xex"_s || suffix == u"elf"_s
                                    || suffix == u"self"_s || file.fileName().compare(u"EBOOT.BIN"_s, Qt::CaseInsensitive) == 0;
            if (executable && file.size() <= kMaxExecutableBytes)
                executables.append(file);
        }
        if (!dir.cdUp())
            break;
    }

    QStringList scanned;
    for (qsizetype i = 0; i < executables.size(); ++i) {
        const QFileInfo& file = executables[i];
        emit updateStatus(u"Reading names from %1..."_s.arg(file.fileName()));
        reportRange(3, 8, i, executables.size());
        {
            QFile in(file.absoluteFilePath());
            if (!in.open(QIODevice::ReadOnly))
                continue;
            const qsizetype before = m_strings.size();
            addStrings(in.readAll());
            scanned.append(u"%1 (%2 strings)"_s.arg(file.fileName()).arg(m_strings.size() - before));
        }
    }
    reportProgress(8);

    emit updateConsole(scanned.isEmpty() ? u"No game executable found next to the blobset."_s
                                         : u"Scanned executables: "_s + scanned.join(u", "_s));
}

void NameResolverWorker::harvestUserNames() {
    QFile in(m_userNames);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit updateConsole(u"Tip: add your own names (one per line) to %1"_s.arg(QDir::toNativeSeparators(m_userNames)));
        return;
    }
    int count = 0;
    while (!in.atEnd()) {
        const QString line = QString::fromUtf8(in.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        addString(line);
        ++count;
    }
    emit updateConsole(u"Read %1 name(s) from %2"_s.arg(count).arg(QFileInfo(m_userNames).fileName()));
}

namespace {

/// The files of a mem pack whose file data is compressed as a whole (RLL2's mempacks\lua.mempack.retail, Rugby 25's:
/// "M3MP" (or "PM3M" big endian), u32 count, then 16 byte entries {data offset, size, name offset, 0} from offset 16,
/// the names, then a table of {offset, compressed size, uncompressed size} chunks (32 KB each, LZMA with 5 byte
/// properties, or stored) up to the first chunk). Empty if the mem pack isn't laid out like that.
QByteArray packedMemPackData(const QByteArray& pack) {
    if (pack.size() < 32)
        return {};
    const bool big = pack.startsWith("PM3M");
    if (!big && !pack.startsWith("M3MP"))
        return {};
    const auto u32 = [&](qsizetype at) -> quint32 {
        return at + 4 <= pack.size() ? (big ? qFromBigEndian<quint32>(pack.constData() + at)
                                             : qFromLittleEndian<quint32>(pack.constData() + at))
                                     : 0;
    };
    const quint32 count = u32(4);
    if (count == 0 || 16 + qint64(count) * 16 > pack.size())
        return {};
    quint32 lastName = 0;
    for (quint32 i = 0; i < count; ++i)
        lastName = std::max(lastName, u32(16 + qsizetype(i) * 16 + 8));
    const qsizetype nameEnd = pack.indexOf('\0', lastName);
    if (lastName == 0 || nameEnd < 0)
        return {};
    // The chunk table follows the names straight away (its first offset can start with a 0 byte, so no padding is
    // skipped), or at the next 4 / 16 byte boundary.
    qsizetype table = -1;
    for (const qsizetype at : {nameEnd + 1, (nameEnd + 4) / 4 * 4, (nameEnd + 16) / 16 * 16}) {
        const quint32 first = u32(at);
        if (first > quint32(at) && first <= quint32(pack.size()) && (first - at) % 12 == 0 && u32(at + 8) == 0x8000) {
            table = at;
            break;
        }
    }
    if (table < 0)
        return {};
    const quint32 first = u32(table);
    QByteArray out;
    for (qsizetype at = table; at < qsizetype(first); at += 12) {
        const quint32 offset = u32(at), size = u32(at + 4), unpacked = u32(at + 8);
        if (unpacked == 0 || unpacked > 0x10000 || qint64(offset) + size > pack.size())
            return {};
        const QByteArray chunk = pack.mid(offset, size);
        if (size == unpacked) {
            out += chunk;
        } else {
            // V4 (Rugby 25) compresses the chunks with zstd, the older games with LZMA.
            const bool zstd = chunk.size() >= 4 && qFromLittleEndian<quint32>(chunk.constData()) == 0xFD2FB528u;
            const QByteArray data = zstd ? CompressionIO::decompressZstd(chunk, true)
                                         : CompressionIO::decompressLzma(chunk, unpacked);
            if (data.isEmpty())
                return out;   // What was read so far still names things.
            out += data;
        }
    }
    return out;
}

/// One file of such a mem pack, by the end of its name ("design/characters/assetmapitems.lua"), out of the unpacked
/// data packedMemPackData returned. Empty if it isn't in there.
QByteArray memPackFile(const QByteArray& pack, const QByteArray& unpacked, const QByteArray& nameEnd) {
    if (unpacked.isEmpty() || pack.size() < 16)
        return {};
    const bool big = pack.startsWith("PM3M");
    const auto u32 = [&](qsizetype at) -> quint32 {
        return at + 4 <= pack.size() ? (big ? qFromBigEndian<quint32>(pack.constData() + at)
                                             : qFromLittleEndian<quint32>(pack.constData() + at))
                                     : 0;
    };
    const quint32 count = u32(4);
    for (quint32 i = 0; i < count && 16 + qsizetype(i) * 16 + 16 <= pack.size(); ++i) {
        const qsizetype entry = 16 + qsizetype(i) * 16;
        const quint32 offset = u32(entry), size = u32(entry + 4), name = u32(entry + 8);
        const qsizetype end = pack.indexOf('\0', name);
        if (name == 0 || end < 0)
            continue;
        if (QByteArrayView(pack.constData() + name, end - name).endsWith(nameEnd)
            && qint64(offset) + size <= unpacked.size())
            return unpacked.mid(offset, size);
    }
    return {};
}

} // namespace

void NameResolverWorker::harvestAssets(const QHash<quint64, QString>& mappedPaths, const QStringList& allPaths) {
    // The names the mapping already has (their folders often name texture packs / models).
    for (const QString& path : std::as_const(allPaths))
        if (!isUnnamed(path))
            addString(path);

    BlobsetFile blobset;
    {
        QFile headerFile(m_blobsetFile);
        if (!headerFile.open(QIODevice::ReadOnly)) {
            emit updateConsole(u"Could not open the blobset, its assets weren't scanned for names."_s);
            return;
        }
        QDataStream in(&headerFile);
        if (!blobset.read(in, static_cast<CoreDefs::BlobsetVersion>(m_version), m_isBigEndian, m_gameId)) {
            emit updateConsole(u"Could not read the blobset header, its assets weren't scanned for names."_s);
            return;
        }
    }

    const bool hash64 = m_version == static_cast<int>(CoreDefs::BlobsetVersion::V4);
    QList<const BlobsetEntry*> targets;
    for (const BlobsetEntry& entry : std::as_const(blobset.entries)) {
        const quint64 hash = hash64 ? entry.folderFileHash64 : quint64(entry.folderFileHash32);
        const auto mapped = mappedPaths.constFind(hash);
        if (mapped == mappedPaths.constEnd()
            || !(worthScanning(extensionOf(mapped.value()))
                 // RLL2's FMOD project is a RIFF file, mapped as sound\wav\N.wav (named "sound\rl4.fev.wav").
                 // (Once named: a first run names it from the executable, the next reads its banks.)
                 || mapped.value().contains(u".fev"_s, Qt::CaseInsensitive)))
            continue;
        // The scripts' flat Lua data (Rugby 25's flatluadata.blob.dat, 13 MB) holds the script-config tables: the
        // sound banks, ...
        const bool scripts = extensionOf(mapped.value()) == u"m3mp"_s
                             || mapped.value().startsWith(u"flatluadata"_s, Qt::CaseInsensitive);
        if (entry.mainUnCompressedSize == 0
            || entry.mainUnCompressedSize > (extensionOf(mapped.value()) == u"m3mp"_s ? kMaxMemPackMainBytes
                                             : isUnnamed(mapped.value()) || scripts ? kMaxUnnamedAssetMainBytes
                                                                                    : kMaxAssetMainBytes))
            continue;
        // Past the cap only the mem packs (the scripts: Rugby 25 has more models than the cap, ahead of its Lua).
        if (targets.size() >= kMaxHarvestedAssets && extensionOf(mapped.value()) != u"m3mp"_s)
            continue;
        targets.append(&entry);
    }

    emit updateStatus(u"Scanning %1 assets for names..."_s.arg(targets.size()));
    reportProgress(10);
    int failures = 0;
    for (qsizetype i = 0; i < targets.size(); ++i) {
        try {
            qint64 mainSize = 0;
            const QByteArray asset = BlobsetAssetReader::read(*targets[i], m_blobsetFile, m_version, m_platform, &mainSize, false);
            addStrings(asset);
            // RLL2's (and Rugby 25's) Lua mem pack keeps its scripts compressed: their strings name the stadiums'
            // texture packs, the animation sets, ...
            const QByteArray scripts = packedMemPackData(asset);
            addStrings(scripts);
            // Rugby 25: the character items (heads, hair, beards, helmets, ...) the asset map builds model names from.
            addCharacterItems(memPackFile(asset, scripts, "design/characters/assetmapitems.lua"));
            addLuaConstants(scripts);
        } catch (const std::exception&) {
            ++failures;   // A damaged asset just doesn't contribute names.
        }
        reportRange(10, 60, i + 1, targets.size(),
                    u"Scanning assets for names (%1 of %2)..."_s.arg(i + 1).arg(targets.size()));
    }
    emit updateConsole(u"Scanned %1 assets for names%2."_s.arg(targets.size() - failures)
                           .arg(failures ? u" (%1 couldn't be read)"_s.arg(failures) : QString()));
}

namespace {

/// The media ids a Wwise sound bank plays: its embedded media (DIDX) and its sound objects' sources (HIRC type 2:
/// u32 id, u32 plugin, u8 stream type, u32 source id - the layout of the banks RL26 ships, version 150).
QList<quint32> bankMediaIds(const QByteArray& bank) {
    QList<quint32> ids;
    const auto u32 = [&bank](qsizetype at) {
        return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bank.constData()) + at);
    };
    qsizetype p = 0;
    while (p + 8 <= bank.size()) {
        const QByteArray tag = bank.mid(p, 4);
        const qsizetype size = u32(p + 4);
        const qsizetype body = p + 8;
        if (size < 0 || body + size > bank.size())
            break;
        if (tag == "DIDX") {
            for (qsizetype i = 0; i + 12 <= size; i += 12)
                ids << u32(body + i);
        } else if (tag == "HIRC" && size >= 4) {
            const quint32 count = u32(body);
            qsizetype q = body + 4;
            for (quint32 i = 0; i < count && q + 5 <= body + size; ++i) {
                const quint8 type = static_cast<quint8>(bank.at(q));
                const qsizetype objectSize = u32(q + 1);
                if (type == 2 && objectSize >= 13 && q + 5 + 13 <= bank.size())
                    ids << u32(q + 5 + 9);
                // Music tracks (type 11): u32 id, u8 flags, u32 source count, then 14 byte sources {u32 plugin,
                // u8 stream type, u32 source id, u32 in memory size, u8 flags} (Rugby 25's player vocals' MIDI).
                if (type == 11 && objectSize >= 9) {
                    const quint32 sources = u32(q + 5 + 5);
                    for (quint32 k = 0; k < sources && 9 + qsizetype(k + 1) * 14 <= objectSize; ++k)
                        ids << u32(q + 5 + 9 + qsizetype(k) * 14 + 5);
                }
                q += 5 + objectSize;
            }
        }
        p = body + size;
    }
    return ids;
}

quint64 fnv1a64Raw(const QString& text) {
    return BlobsetHash::fnv1a64(BlobsetHash::normalise(text.toStdString()));
}

} // namespace

void NameResolverWorker::harvestSound(const QHash<quint64, QString>& mappedPaths, QHash<quint64, QString>& direct) {
    if (m_version == static_cast<int>(CoreDefs::BlobsetVersion::V1)
        || m_version == static_cast<int>(CoreDefs::BlobsetVersion::V2)) {
        // RLL2's commentary / referee lines: "sound/commentary/<10 digit id>.sound" (mapped sound\wav\N.wav), the ids
        // listed in "sound/lines.commentary" / "sound/lines.referee".
        BlobsetFile blobset;
        {
            QFile headerFile(m_blobsetFile);
            if (!headerFile.open(QIODevice::ReadOnly))
                return;
            QDataStream in(&headerFile);
            if (!blobset.read(in, static_cast<CoreDefs::BlobsetVersion>(m_version), m_isBigEndian, m_gameId))
                return;
        }
        static const QRegularExpression soundId(u"(?<![0-9])[0-9]{10}(?![0-9])"_s);
        int sounds = 0;
        for (const char* type : {"commentary", "referee"}) {
            const quint64 hash = BlobsetHash::hashForAsset(m_version, {u"sound/lines"_s, QString::fromLatin1(type)},
                                                           m_platform, m_gameKey);
            for (const BlobsetEntry& entry : std::as_const(blobset.entries)) {
                if (quint64(entry.folderFileHash32) != hash)
                    continue;
                QByteArray lines;
                try {
                    lines = BlobsetAssetReader::read(entry, m_blobsetFile, m_version, m_platform, nullptr, false);
                } catch (const std::exception&) {
                    break;
                }
                QSet<QString> ids;
                for (auto it = soundId.globalMatch(QString::fromLatin1(lines)); it.hasNext();)
                    ids.insert(it.next().captured());
                for (const QString& id : std::as_const(ids))
                    addString(u"sound/%1/%2.sound"_s.arg(QLatin1String(type), id));
                sounds += int(ids.size());
                break;
            }
        }
        if (sounds > 0)
            emit updateConsole(u"Sound: %1 commentary / referee line id(s)."_s.arg(sounds));
        Q_UNUSED(mappedPaths);
        Q_UNUSED(direct);
        return;
    }
    if (m_version != static_cast<int>(CoreDefs::BlobsetVersion::V4))
        return;

    // The sound data files' own names (their types are spelt out, so they pass as explicit pairs).
    for (const char* name : {"sound/lines.commentary", "sound/lines.referee", "sound/categoryinfo.commentary",
                             "sound/categoryinfo.referee", "sound/dialogue.mempack", "sound/init.bnk", "sound/crowd.bnk"})
        addString(QString::fromLatin1(name));
    // The Wwise banks the scripts load (script-config/design/sound/soundmap_banks.lua): "sound/<name>.bnk".
    for (const char* bank : {"environment", "intro_only_cutscenes", "loudspeaker_crowd_mx_clapalong", "loudspeaker_global",
                             "onfield", "onfieldrefereevocals", "particles_sfx", "playervocals", "refereewhistle",
                             "audio_input", "ui", "ui_frontend_only", "music_ace_candy_apple_red",
                             "music_captain_joz_i_got_money", "music_flint_its_all_the_same",
                             "music_frank_bentley_untouchable", "music_mogli_the_iceburg_ghost", "music_naems_chatter",
                             "music_out_of_flux_hellbeast", "music_raw_ghost_witch", "music_raz_burg_voltage_vendetta",
                             "music_roy_matz_eye_of_the_storm", "music_sam_lux_big_black_bull", "music_soul_j_winner",
                             "music_taheda_nuclear", "music_the_cliff_the_gamer", "music_tomas_herudek_smash_your_enemies",
                             "music_vic_sage_mvp", "music_wav_haven_the_main_event"})
        addString(u"sound/%1.bnk"_s.arg(QLatin1String(bank)));
    // Rugby 25's front end music, a bank per track ("FrontEndMusic_BeautifulCatastrophe" in soundmap_banks, in the
    // flat Lua data): "sound/<bank>.bnk".
    {
        const QStringList strings(m_strings.cbegin(), m_strings.cend());
        for (const QString& s : strings)
            if (s.startsWith(u"frontendmusic_"_s) && !s.contains(u'/') && !s.contains(u'.'))
                addString(u"sound/%1.bnk"_s.arg(s));
    }
    // The commentary's dialogue conditions table ("dist_ball_to_try_line_#20+", ...), hashed by its whole name like
    // the lines.
    direct.insert(fnv1a64Raw(u"sound/dialogueconditions"_s), u"sound\\dialogueconditions.dat"_s);
    // The PS5 controller vibration (haptics) sounds: the 38 wavs vibration_config.lua names, PCM, by FNV-1a 32 of
    // their lower case names.
    direct.insert(fnv1a64Raw(u"sound/vibration/vibrationdata"_s), u"sound\\vibration\\vibrationdata.dat"_s);
    direct.insert(fnv1a64Raw(u"noesis/textstyles"_s), u"noesis\\textstyles.obj"_s);

    BlobsetFile blobset;
    {
        QFile headerFile(m_blobsetFile);
        if (!headerFile.open(QIODevice::ReadOnly))
            return;
        QDataStream in(&headerFile);
        if (!blobset.read(in, static_cast<CoreDefs::BlobsetVersion>(m_version), m_isBigEndian, m_gameId))
            return;
    }
    QHash<quint64, const BlobsetEntry*> byHash;
    for (const BlobsetEntry& entry : std::as_const(blobset.entries))
        byHash.insert(entry.folderFileHash64, &entry);
    const auto read = [&](const BlobsetEntry* entry) -> QByteArray {
        try {
            return BlobsetAssetReader::read(*entry, m_blobsetFile, m_version, m_platform, nullptr, false);
        } catch (const std::exception&) {
            return {};
        }
    };

    // Sound banks: their media are "sound/<media id>.wem".
    emit updateStatus(u"Reading the sound banks..."_s);
    int banks = 0, mediaIds = 0;
    for (auto it = mappedPaths.cbegin(); it != mappedPaths.cend(); ++it) {
        if (!it.value().endsWith(u".bnk"_s, Qt::CaseInsensitive))
            continue;
        const auto entry = byHash.constFind(it.key());
        if (entry == byHash.constEnd())
            continue;
        const QList<quint32> ids = bankMediaIds(read(entry.value()));
        for (const quint32 id : std::as_const(ids))
            addString(u"sound/%1.wem"_s.arg(id));
        mediaIds += int(ids.size());
        ++banks;
    }

    // Commentary / referee lines: "sound/commentary/id1_andrewvoss/id1_try_27" -> sound\commentary\...\id1_try_27.wem
    int sounds = 0;
    for (const char* type : {"commentary", "referee"}) {
        const auto entry = byHash.constFind(BlobsetHash::hashV4(u"sound/lines"_s, QString::fromLatin1(type)));
        if (entry == byHash.constEnd())
            continue;
        const QStringList names = CommentaryEditorManager::soundNames(read(entry.value()));
        for (const QString& name : std::as_const(names)) {
            QString path = name;
            path.replace(u'/', u'\\');
            direct.insert(fnv1a64Raw(name), path + u".wem"_s);
        }
        sounds += int(names.size());
    }
    // Rugby 25's lines (sound\lines.commentary.cmtry, lines.announcer.dat, lines.tmoreferee.dat) are laid out
    // differently: each speaker's folder ("commentary/id4_frenchcomm1/") and the line names spoken in it
    // ("id4_team_up_laos", named for the speaker's id) are strings in the file, the sound is
    // "sound/<folder><name>" (hashed without a type, like RL26's).
    // Tiebreak's (announcers, chairumpires, career) don't name their speakers' folders: design/sound/soundmap.lua does
    // (filePath = "chairUmpires/id1_DLauritsen/"), so the scripts' folders of that form are used for every file.
    QHash<QString, QString> scriptFolders;
    {
        static const QRegularExpression scriptFolder(u"^([a-z0-9_]+/(id\\d+_)[a-z0-9_]+)/?$"_s);
        for (const QString& s : std::as_const(m_strings))
            if (const auto m = scriptFolder.match(s); m.hasMatch())
                scriptFolders.insert(m.captured(1) + u'/', m.captured(2));
    }
    // The speakers' names in the folders already mapped ("sound\\commentary\\id4_alisonmitchell\\..."): Cricket 24's
    // lines.ftue.dat names no folder, and its speaker ("id2_" lines) is one of the commentators:
    // "sound/ftue/id2_alisonmitchell/id2_ftue_batting1_1".
    QSet<QString> speakerNames;
    {
        static const QRegularExpression mappedSpeaker(u"^sound[\\\\/][a-z0-9_]+[\\\\/]id\\d+_([a-z0-9_]+)[\\\\/]"_s);
        for (const QString& path : mappedPaths)
            if (const auto m = mappedSpeaker.match(path.toLower()); m.hasMatch())
                speakerNames.insert(m.captured(1));
    }
    for (const char* type : {"commentary", "announcer", "tmoreferee", "referee", "announcers", "chairumpires", "career",
                             "ftue"}) {
        const auto entry = byHash.constFind(BlobsetHash::hashV4(u"sound/lines"_s, QString::fromLatin1(type)));
        if (entry == byHash.constEnd())
            continue;
        QByteArray data = read(entry.value());
        // Still in the game's chunks (u32 chunk size + 4, then a zstd frame or the stored bytes)?
        if (data.size() >= 8 && qFromLittleEndian<quint32>(data.constData() + 4) == 0xFD2FB528u) {
            QByteArray unpacked;
            qsizetype p = 0;
            while (p + 4 <= data.size()) {
                const qsizetype n = qFromLittleEndian<quint32>(data.constData() + p);
                if (n < 4 || p + n > data.size())
                    break;
                const QByteArray chunk = data.mid(p + 4, n - 4);
                unpacked += chunk.size() >= 4 && qFromLittleEndian<quint32>(chunk.constData()) == 0xFD2FB528u
                                ? CompressionIO::decompressZstd(chunk, true) : chunk;
                p += n;
            }
            data = unpacked;
        }
        // (Searched for inside the strings: the lengths around a folder can be printable bytes that run into it, which
        // hid "commentary/id2_jonathandavies/" and its 1,706 lines.)
        static const QRegularExpression speaker(u"([a-z0-9_]+/(id\\d+_)[a-z0-9_]+/)"_s);
        // Apostrophes and hyphens too: Cricket 24's "id3_surname_mid_o'donnell", "id1_team_mid_have_south-east".
        static const QRegularExpression line(u"^[a-z0-9_'\\-]+$"_s);
        QHash<QString, QString> folders;   // Folder -> its lines' prefix ("id4_").
        QSet<QString> lines;
        qsizetype start = -1;
        for (qsizetype i = 0; i <= data.size(); ++i) {
            const bool printable = i < data.size() && data[i] > ' ' && data[i] < 127;
            if (printable && start < 0) {
                start = i;
            } else if (!printable && start >= 0) {
                const QString token = QString::fromLatin1(data.constData() + start, i - start).toLower();
                bool folder = false;
                for (auto it = speaker.globalMatch(token); it.hasNext();) {
                    const auto m = it.next();
                    folders.insert(m.captured(1), m.captured(2));
                    folder = true;
                }
                if (!folder && token.size() >= 4 && line.match(token).hasMatch())
                    lines.insert(token);
                start = -1;
            }
        }
        for (auto f = scriptFolders.cbegin(); f != scriptFolders.cend(); ++f)
            folders.insert(f.key(), f.value());
        for (auto f = folders.cbegin(); f != folders.cend(); ++f)
            speakerNames.insert(f.key().section(u'/', -2, -2).mid(f.value().size()));
        // Speakers ("idN_" lines) without a folder: "<type>/idN_<speaker>/" for every speaker name known.
        static const QRegularExpression linePrefix(u"^(id\\d+_)"_s);
        QSet<QString> prefixes;
        for (const QString& name : std::as_const(lines))
            if (const auto m = linePrefix.match(name); m.hasMatch())
                prefixes.insert(m.captured(1));
        for (const QString& prefix : std::as_const(prefixes)) {
            if (std::find(folders.cbegin(), folders.cend(), prefix) != folders.cend())
                continue;
            for (const QString& name : std::as_const(speakerNames))
                folders.insert(QString::fromLatin1(type) + u'/' + prefix + name + u'/', prefix);
        }
        int named = 0;
        for (auto f = folders.cbegin(); f != folders.cend(); ++f) {
            for (const QString& name : std::as_const(lines)) {
                if (!name.startsWith(f.value()))
                    continue;
                const QString sound = u"sound/"_s + f.key() + name;
                QString path = sound;
                path.replace(u'/', u'\\');
                direct.insert(fnv1a64Raw(sound), path + u".wem"_s);
                ++named;
            }
        }
        sounds += named;
    }
    if (banks > 0 || sounds > 0)
        emit updateConsole(u"Sound: %1 media id(s) from %2 sound bank(s), %3 commentary / referee sound(s)."_s
                               .arg(mediaIds).arg(banks).arg(sounds));
}

// RL26's animation sets ("animations/<name>.animset", the game's anim\packs\N.anims) and a few other files whose
// names are only in the game's scripts / cutscene data, not in anything the resolver scans, so they're listed
// here; they only match files that hash to them.
void NameResolverWorker::deriveAnimationSetNames() {
    static const char* const kAnimationSets[] = {
        "cameocameras", "cutscene_rl1_test_cutscene_asset_01", "cutscene_rl1_test_dof_01", "cutscene_rl1_test_sync_01",
        "cutscenecaptains", "cutscenecaptains16", "cutscenecaptainschallenge", "cutscenechampions01",
        "cutscenecheerleadertry_01", "cutscenecheerleadertry_02", "cutscenecheerleadertry_03",
        "cutsceneconversion_l_01", "cutsceneconversion_l_02", "cutsceneconversion_l_03", "cutsceneconversion_l_04",
        "cutsceneconversion_l_05", "cutsceneconversion_l_fail_01", "cutsceneconversion_l_fail_02",
        "cutsceneconversion_l_success_01", "cutscenecorridor", "cutscenefulltime01", "cutscenefulltimedraw01",
        "cutscenefulltimegf", "cutscenegffulltimecelebration01", "cutscenegffulltimecelebration01_cam06",
        "cutscenegfintroaway01", "cutscenegfintrohome01", "cutscenegfintrotunnelawayteam01",
        "cutscenegfintrotunnelawayteam01_cam01", "cutscenegfintrotunnelawayteam01_cam02",
        "cutscenegfintrotunnelawayteam01_cam03", "cutscenegfintrotunnelhometeam01",
        "cutscenegfintrotunnelhometeam01_cam01", "cutscenegfintrotunnelhometeam01_cam02",
        "cutscenegfnationalanthem01", "cutscenehalftimewalkoff01", "cutsceneinjuries", "cutsceneintroawayteam01",
        "cutsceneintrogf_awayteam", "cutsceneintrogf_common", "cutsceneintrogf_hometeam",
        "cutsceneintrogf_hometeamawayidle", "cutsceneintrohometeam01", "cutscenerl1trophypres",
        "cutscenescreenshotkick", "cutscenesendoff_01", "cutscenesinbin_01", "cutscenespreloaded",
        "cutscenestadiumshots", "cutscenestormpromo", "cutsceneteaserkick", "cutscenetrailershot_01",
        "cutscenetrailershot_02", "cutscenetrailershot_03", "cutscenetrailershot_04", "cutscenetrailershot_05",
        "cutscenetrailershot_06", "cutscenetrailershot_07", "cutscenetrailershot_08", "cutscenetrailershot_09",
        "cutscenetrophy", "cutscenetrycelebratehug01", "cutscenetrycelebratehug02", "cutscenetrycelebratehug03",
        "cutscenetrycelebratehug04", "cutscenetrycelebratehug05", "cutscenetrycelebratehug06",
        "cutscenetrycelebratehug07", "cutscenetrycelebratehug08", "cutscenetrycelebratehugrunoff01",
        "cutscenetrycelebratelow5hug01", "cutscenetrycelebratemrbowarrow01", "cutscenetrycelebratescrufflehair01",
        "cutscenetrycelebratescrufflehair02", "cutscenetrycelebratetapshoulder01", "cutscenetryneutraltapchest01",
        "facial", "ingame", "master", "movement", "rigdelta", "rl1_backcover_screenshot_animset", "rl1_conversion_l_01",
        "rl1_conversion_l_02", "rl1_conversion_l_03", "rl1_conversion_l_04", "rl1_conversion_l_05",
        "rl1_conversion_l_fail_01", "rl1_conversion_l_fail_02", "rl1_conversion_l_success_01",
        "rl1_conversion_negative_01", "rl1_conversion_negative_02", "rl1_conversion_negative_03",
        "rl1_conversion_neutral_01", "rl1_conversion_neutral_01_b", "rl1_conversion_neutral_02",
        "rl1_conversion_neutral_03", "rl1_conversion_neutral_04", "rl1_conversion_neutral_05",
        "rl1_conversion_positive", "rl1_cs_trycelebrate_back_jump_01_animset",
        "rl1_cs_trycelebrate_back_pat_01_animset", "rl1_cs_trycelebrate_ball_kick_stands_03",
        "rl1_cs_trycelebrate_camera_interact_01_animset", "rl1_cs_trycelebrate_camera_interact_02_animset",
        "rl1_cs_trycelebrate_camera_interact_03_animset", "rl1_cs_trycelebrate_camera_interact_07_animset",
        "rl1_cs_trycelebrate_camera_interact_09_animset", "rl1_cs_trycelebrate_fist_pump_01",
        "rl1_cs_trycelebrate_fwd_low_energy_01_animset", "rl1_cs_trycelebrate_ground_getup_01",
        "rl1_cs_trycelebrate_group_01_animset", "rl1_cs_trycelebrate_high_five_01_animset",
        "rl1_cs_trycelebrate_low_high_five_01_animset", "rl1_cs_trycelebrate_low_high_five_02_animset",
        "rl1_cs_trycelebrate_mid_slide_01", "rl1_cs_trycelebrate_power_pose_01_animset",
        "rl1_cs_trycelebrate_team_mate_grab_01_animset", "rl1_cs_trycelebrate_template_animset",
        "rl1_cs_trycelebrate_wing_halfback_handshake_01_animset", "rl1_gf_corridor_home_away",
        "rl1_halftime_walkoff_01_animset", "rl1_intro_gf_home_away_team_01", "rl1_intro_lockers_away",
        "rl1_intro_lockers_home", "rl1_og_trycelebrate_hug_01_animset", "rl1_og_trycelebrate_hug_02_animset",
        "rl1_og_trycelebrate_hug_03_animset", "rl1_og_trycelebrate_hug_04_animset",
        "rl1_og_trycelebrate_hug_05_animset", "rl1_og_trycelebrate_hug_06_animset",
        "rl1_og_trycelebrate_hug_07_animset", "rl1_og_trycelebrate_hug_08_animset",
        "rl1_og_trycelebrate_hug_runoff_01_animset", "rl1_og_trycelebrate_low5_hug_01_animset",
        "rl1_og_trycelebrate_scruffhair_01_animset", "rl1_ss_press_release_animset", "rl1_stadium_team_logos_animset",
        "rl1_teaser_run_animset", "rl1_test_eyeline_01_animset", "rl1_trailer_animset", "screenshot_stadium_01",
        "tackle", "teamshieldprops", "testcutscene", "ui", "uifacial",
    };
    for (const char* name : kAnimationSets)
        addString(u"animations/"_s + QLatin1String(name) + u".animset"_s);
    // RLL4's pre-match intro for the kicking / receiving team, and its crowd animations.
    for (const char* name : {"cutsceneintrohometeamkickoff01", "cutsceneintrohometeamreceiving01",
                             "cutsceneintroawayteamkickoff01", "cutsceneintroawayteamreceiving01", "crowdset",
                             "cutscenetrycelebratechesttap01", "cutsceneintrograndfinal_01",
                             // RLL3's (found from their clips: "p1_cs_trycelebrate_scruffhair_01", ...).
                             "cutscenetrycelebratescruffhair01", "cutscenetrycelebratescruffhair02",
                             "cutscenetrycelebratethrowball01", "cutscenetrycelebratethrowball02", "cutscenecameo",
                             "cutscenecheerleader", "cheerleaderfacial", "cheerleader", "cutscenefulltime_01",
                             "cutscenechampions_01", "cutscenegffulltime_01", "props", "tackletry",
                             "cutsceneintrohometeamkickinghome01", "cutsceneintrohometeamrecivinghome01",   // (sic)
                             "cutscenehalftimewalkoff_01", "cutscenetrycelebratehuglow01", "cutsceneintrocorridor",
                             // Rugby 25: every cutscene animation set its scripts name (design/cutscenes/
                             // cutscenes_config.lua: animsetName, loaded as "animations/" .. name .. ".animset").
                             "fiftytwentytwo", "fulltimesemiwinlose", "postconversion", "prepenalty",
                             "r25gameplayshots", "r25lockerroom", "rugby24teasertrailer", "rugby25launchtrailer",
                             "cutsceneanthem01", "cutscenecaptains", "cutscenecaptains16",
                             "cutscenecelebrateballthrowgroup_01", "cutscenecelebrateintenseslideyell_01",
                             "cutscenecelebraterollhug_01", "cutscenecelebrate_ball_throw_hug_group",
                             "cutscenechampions01", "cutsceneconversionconcept", "cutsceneconversiontsuccess24_01",
                             "cutsceneconversion_l_01", "cutsceneconversion_l_02", "cutsceneconversion_l_03",
                             "cutsceneconversion_l_04", "cutsceneconversion_l_05", "cutsceneconversion_l_fail_01",
                             "cutsceneconversion_l_fail_02", "cutsceneconversion_l_success_01", "cutsceneformation",
                             "cutscenefulltime01", "cutscenefulltimedraw01", "cutscenefulltimewinlose",
                             "cutscenegffulltimecelebration01", "cutscenegffulltimecelebration01_cam06",
                             "cutscenegfintroaway01", "cutscenegfintrohome01", "cutscenegfintrotunnelawayteam01",
                             "cutscenegfintrotunnelawayteam01_cam01", "cutscenegfintrotunnelawayteam01_cam02",
                             "cutscenegfintrotunnelawayteam01_cam03", "cutscenegfintrotunnelhometeam01",
                             "cutscenegfintrotunnelhometeam01_cam01", "cutscenegfintrotunnelhometeam01_cam02",
                             "cutscenegfnationalanthem01", "cutscenehalftimewalkoff24", "cutscenehandshakes",
                             "cutsceneinjuries", "cutsceneinterchange", "cutsceneintroawayteam01",
                             "cutsceneintrocorridorwalk", "cutsceneintrohometeam01",
                             "cutsceneintrohometeamkickoff01", "cutsceneintrohometeamreceiving01",
                             "cutsceneintrolockers", "cutsceneintrowalkon", "cutsceneintrowalkonnoentrance",
                             "cutscenepostreplay", "cutsceneprelineout", "cutsceneprescrum", "cutscenepromo",
                             "cutscenerefereecard", "cutscenerefereepenalty", "cutscenescreenshots",
                             "cutscenescreenshots_with_face", "cutscenescreenshot_ui_poses", "cutscenesendoff_01",
                             "cutscenesinbin_01", "cutscenestadiumshots", "cutscenetrailershot_01",
                             "cutscenetrailershot_02", "cutscenetrailershot_03", "cutscenetrailershot_04",
                             "cutscenetrailershot_06", "cutscenetrailershot_07", "cutscenetrailershot_08",
                             "cutscenetrailershot_09", "cutscenetrophycelebration24", "cutscenetrycelebratehug01",
                             "cutscenetrycelebratehug02", "cutscenetrycelebratehug03", "cutscenetrycelebratehug04",
                             "cutscenetrycelebratehug05", "cutscenetrycelebratehug06", "cutscenetrycelebratehug07",
                             "cutscenetrycelebratehug08", "cutscenetrycelebratehugrunoff01",
                             "cutscenetrycelebratelow5hug01", "cutscenetrycelebratescrufflehair01",
                             "cutscenetrycelebratescrufflehair02", "cutscenetrycelebratetapshoulder01",
                             "cutscenetrycelebrates24", "cutscenetryneutraltapchest01", "fxgameplaytests",
                             // Not in the scripts (found from its clips: stadium fly around / wide shots).
                             "cutscenestadiumwideshots24"})
        addString(u"animations/"_s + QLatin1String(name) + u".animset"_s);

    // RLL3 / RLL4's FMOD Studio banks: design/sound/soundmap.lua's "sound/" .. ProjectName .. "/<bank>.bank" (RLL4's
    // ProjectName "rll4", RLL3's "rll3"), and the Academy / training project's own banks in "sound/<project>_academy/"
    // (its strings bank lists them). Mapped as sound\bank\N.bank; the names just don't match in the other game.
    static const char* const kFmodBanks[] = {
        "master", "master.strings", "loaded_ingame", "loaded_ingame_currentgen", "loaded_ingame_playerbanter", "loaded_menus_frontend",
        "loaded_menus_ingame", "loaded_traininganz", "loaded_trainingindoors", "streamed_cinematic", "streamed_ingame",
        "streamed_ingame_ref1", "streamed_ingame_ref2", "streamed_ingame_ref3", "streamed_ingame_ref4",
        "streamed_ingame_ref5", "streamed_ingame_ref_rll2", "streamed_ingame_smallcrowd", "streamed_loudspeaker",
        "streamed_music_frontend", "streamed_music_ingame", "streamed_training",
        // Not in the scripts: from the master strings bank's list.
        "loaded_menus_training", "loaded_anz", "loaded_indoors", "streamed_ingame_rll2", "streamed_music_training"};
    // RLL3's team intro banks ("sfx_plinth1_<team>" / "sfx_plinth2_<team>"): "streamed_ingame_team_<team>".
    static const char* const kFmodTeams[] = {
        // From the team database's commentary names (the game's own spelling: "belguim").
        "belguim", "oldhamrough",
        "australia", "away", "barrow", "batley", "bigant", "blacks", "bradford", "broncos",
        "bulldogs", "burleighbears", "canada", "canterburybankstown", "castleford", "catalans", "centralcapras", "centralcoastcenturions",
        "city", "cookislands", "country", "coventry", "cowboys", "cronullasutherland", "crusaders", "czechrepublic",
        "denmark", "dewsbury", "doncaster", "dragons", "eaststigers", "eels", "england", "featherstone",
        "fiji", "france", "gateshead", "germany", "gloucestershire", "greece", "halifax", "hemel",
        "holland", "home", "huddersfield", "hullfc", "hullrovers", "hungary", "hunslet", "illawarracutters",
        "indigenousallstars", "ipswichjets", "ireland", "italy", "jamaica", "keighley", "knights", "latvia",
        "lebanon", "leeds", "leigh", "london", "londonbroncos", "londonskolars", "mackaycutters", "malta",
        "manlywarringah", "morocco", "mounties", "newtownjets", "northernpride", "northsdevils", "northsydneybears", "norway",
        "nrlallstars", "nsw", "nz", "oxford", "panthers", "png", "pnghunters", "qld",
        "rabbitohs", "raiders", "redcliffedolphins", "reds", "rochdalehornets", "roosters", "russia", "salford",
        "samoa", "scotland", "seaeagles", "serbia", "sharks", "sheffield", "southafrica", "southslogan",
        "southwales", "spain", "sthelens", "storm", "sunshinecoast", "sweden", "swinton", "tigers",
        "titans", "tonga", "townsville", "trublu", "tweedheads", "ukraine", "usa", "wakefield",
        "wales", "warrington", "warriors", "wentworthvillemagpies", "westernsuburbsmagpies", "whitehaven", "widnes", "wigan",
        "windsorwolves", "workington", "wyong", "wynnummanly", "york",
    };
    for (const char* project : {"rll3", "rll4"}) {
        for (const char* bank : kFmodBanks) {
            addString(u"sound/%1/%2.bank"_s.arg(QLatin1String(project), QLatin1String(bank)));
            addString(u"sound/%1_academy/%2.bank"_s.arg(QLatin1String(project), QLatin1String(bank)));
        }
        for (const char* team : kFmodTeams)
            addString(u"sound/%1/streamed_ingame_team_%2.bank"_s.arg(QLatin1String(project), QLatin1String(team)));
    }

    // Other files only the scripts name: the gameplay action / play sets (mapped as objects\N.obj), the crowd's
    // animations and the stadium creator's shared texture pack.
    static const char* const kScriptFiles[] = {
        "actionset/actionset.dat", "actionset/collectset.dat", "actionset/contestset.dat",
        "actionset/frontendset.dat", "actionset/kickset.dat", "actionset/placekickset.dat",
        "actionset/playtheballset.dat", "actionset/scrumset.dat", "actionset/strikeset.dat",
        "actionset/tackleset.dat", "actionset/tryset.dat", "actionset/tennisset.dat", "textures/whitelist.bin", "naturalmovement/naturalmovement.dat",
        "plays/formationset.dat", "plays/playset.dat", "crowd/anims.dat", "stadiumcreator/shared.texpack",
        "animations/animunit.animunit",   // The animation units table (anim\sets\N.animset).
        "animations/cheerleader.animunit",   // RLL3: the cheerleaders' animation unit.
        "actiongraph/actiongraph.dat",       // RLL3: the action graph (objects\N.obj), RLL4 split it into actionset/*.dat.
        "setplays/setplays.blob",            // RLL3: the set plays (positions per play).
        "animations/cheerleader.motionset",  // RLL3: the cheerleaders' motion set.
        "animations/motionset.motionset",   // The gameplay motion set (every animation by name, with "_m" mirrors).
        // The player skeletons ("rigs/%s.rig": joints plus the beard / hair cloth joints).
        "rigs/player_male.rig", "rigs/player_female.rig",
        // Shared texture packs (RLL4).
        "props/common.texpack", "characters/hair/textures/shared.texpack", "textures/ingame.texpack",
        // RLL4: the texture lists (texture_names\N.names), the commentary lines and the cricket rig it shares.
        "characters/player/bat.texsrc", "characters/player/body.texsrc", "characters/player/head.texsrc",
        "characters/player/headgear.texsrc", "characters/player/uniform.texsrc", "sound/lines.commentary",
        "rigs/cricket_male.rig",
        // RLL4's sequences (anim\cinematic\N.anim).
        "sequences/transitionanims.seq", "sequences/rll3_cameras.seq", "sequences/shield_sparks.seq",
        "sequences/videorefereegeneric.seq", "props/flamejet/flamejet.seq", "props/flamejet/flamejet_tower.seq",
        // The weather by climate (hourly temperature, rain, humidity, wind; the stadiums pick one).
        "climate/arid.blob", "climate/mediterranean.blob", "climate/mountains.blob", "climate/temperate.blob",
        "climate/tropical.blob",
    };
    for (const char* name : kScriptFiles)
        addString(QString::fromLatin1(name));

    // The wrinkle maps ("animations/wrinklemaps/" + the asset map's wrinkleConfig + ".wrinklemap"): the uniform
    // tops' / bottoms' and the heads' wrinkle weights.
    static const char* const kWrinkleMaps[] = {
        "f_clothes_shorts01_female", "f_clothes_shorts02_female", "f_clothes_shorts03_female", "m_clothes_shorts01_male",
        "player_master_head_male", "player_master_head_female", "tier1_head_male", "tier1_head_female",
    };
    for (const char* name : kWrinkleMaps)
        addString(u"animations/wrinklemaps/"_s + QLatin1String(name) + u".wrinklemap"_s);
    // The tops: <sex>_clothes_top_<style>_short_<male|female>.
    for (const char* style : {"flat01", "flat02", "round01", "round02", "round03", "round05", "round05_tucked", "soft02",
                              "vneck01", "vneck06_tucked"}) {
        addString(u"animations/wrinklemaps/m_clothes_top_%1_short_male.wrinklemap"_s.arg(QLatin1String(style)));
        addString(u"animations/wrinklemaps/f_clothes_top_%1_short_female.wrinklemap"_s.arg(QLatin1String(style)));
    }
}

void NameResolverWorker::deriveCharacterNames(const QStringList& allPaths) {
    static const QRegularExpression helperJoints(u"^hj_offsets_([a-z0-9_]+)\\.lua$"_s);
    static const QRegularExpression crowdNodes(u"^([a-z0-9_]+_crowdnodes)$"_s);
    static const QRegularExpression trophy(u"^([a-z0-9_]+_trophy)_(?:male|female)\\.lua$"_s);
    // A jiggly joints file (body_shapes\N.bshp) names the script it was made from ("ashes_trophy_male.lua"); the
    // game loads it as "characters/jigglyjoints/<that name>.jiggly".
    static const QRegularExpression luaScript(u"^([a-z0-9_]+)\\.lua$"_s);
    static const QRegularExpression beardTexture(
        u"^characters/hair/textures/beardcap_([a-z0-9_]+?)(?:_(?:col|mat|nrm|dir|msk|ao|alpha)(?:_\\d+)?)?\\.pc\\.dds$"_s);

    QStringList found;
    const auto addPart = [&](const QString& folder, const QString& name) {
        found << u"characters/%1/models/%2/%2.modelcomponent"_s.arg(folder, name)
              << u"characters/%1/models/%2/%2.blendshapes"_s.arg(folder, name)
              << u"characters/%1/models/%2/%2.vbo"_s.arg(folder, name);   // RLL4 / RLL3.
    };

    int players = 0;
    const QStringList strings(m_strings.cbegin(), m_strings.cend());
    for (const QString& s : strings) {
        if (const auto m = helperJoints.match(s); m.hasMatch()) {
            // A player: "characters/helperjoints/<player>.hjoffsets" and the parts the character manager loads
            // ("<folder>/models/<part>/<part>.modelcomponent", with its .blendshapes).
            const QString player = m.captured(1);
            found << u"characters/helperjoints/%1.hjoffsets"_s.arg(player);
            addPart(u"heads"_s, player);
            addPart(u"hair"_s, player);
            addPart(u"hair"_s, player + u"_side"_s);
            addPart(u"haircap"_s, player + u"_haircap"_s);
            addPart(u"beards"_s, player + u"_moustache"_s);
            addPart(u"beards"_s, player + u"_sideburns"_s);
            addPart(u"helmets"_s, player + u"_headwear"_s);
            addPart(u"beards"_s, player + u"_chinhair"_s);
            addPart(u"hair"_s, player + u"_side2"_s);
            for (const QChar gear : u"abcdefgh"_s) {   // Hair with gear (headband, ...): "_geara", "_side_geara".
                addPart(u"hair"_s, player + u"_gear"_s + gear);
                addPart(u"hair"_s, player + u"_side_gear"_s + gear);
            }
            ++players;
        } else if (const auto c = crowdNodes.match(s); c.hasMatch()) {
            found << u"crowd/nodefiles/%1.nodefile"_s.arg(c.captured(1));
        } else if (const auto t = trophy.match(s); t.hasMatch()) {
            addPart(u"trophies"_s, t.captured(1));
        }
        if (const auto j = luaScript.match(s); j.hasMatch())
            found << u"characters/jigglyjoints/%1.jiggly"_s.arg(j.captured(1));
    }

    // The generic beards, named like their textures ("beardcap_chin_anchor01_col" -> chin_anchor01), plus the
    // numbered variants of each style and its words combined ("moustache_pencil_handlebar02", ...).
    QSet<QString> styles;
    for (const QString& path : allPaths) {
        const auto m = beardTexture.match(QString(path).toLower().replace(u'\\', u'/'));
        if (m.hasMatch())
            styles.insert(m.captured(1));
    }
    static const QRegularExpression digits(u"\\d+$"_s);
    QSet<QString> words;
    for (const QString& style : std::as_const(styles))
        for (QString word : style.split(u'_'))
            words.insert(word.remove(digits));
    QSet<QString> beards;
    for (const QString& kind : {u"chin"_s, u"moustache"_s, u"sideburns"_s}) {
        QStringList names{kind};
        for (const QString& a : std::as_const(words)) {
            if (a.isEmpty() || a == kind)
                continue;
            names << kind + u'_' + a;
            for (const QString& b : std::as_const(words))
                if (!b.isEmpty() && b != a && b != kind)
                    names << kind + u'_' + a + u'_' + b;
        }
        for (const QString& name : std::as_const(names)) {
            beards.insert(name);
            for (int n = 1; n <= 5; ++n)
                beards.insert(name + u"%1"_s.arg(n, 2, 10, QLatin1Char('0')));
        }
    }
    for (const QString& style : std::as_const(styles))
        beards.insert(style);
    for (const QString& beard : std::as_const(beards))
        addPart(u"beards"_s, beard);
    // The generic hair styles, helmets, uniforms, socks, boots, ... (name_resolver_skinned_names.h).
    for (const char* part : kSkinnedPartNames) {
        const QString text = QString::fromLatin1(part);
        addPart(text.section(u'/', 0, 0), text.section(u'/', 1));
    }
    // RLL4's character parts and their texture packs (name_resolver_rll4_names.h).
    for (const char* part : kRll4CharacterParts) {
        const QString text = QString::fromLatin1(part);
        const QString folder = text.section(u'/', 0, 0), name = text.section(u'/', 1);
        addPart(folder, name);
        found << u"characters/%1/models/%2/%2.texpack"_s.arg(folder, name);
    }

    // Collision (collision\N.col): each stadium's "collision/stadiums/<stadium>/collision.collision" (the stadiums
    // the mapping has a folder for), the stadium creator's fields' "<field model>_collision.collision" and the props.
    QSet<QString> stadiums, fieldModels;
    for (const QString& path : std::as_const(allPaths)) {
        const QString p = QString(path).toLower().replace(u'\\', u'/');
        if (p.startsWith(u"stadiums/"_s) && p.count(u'/') >= 2)
            stadiums.insert(p.section(u'/', 1, 1));
        // The field models are stadiums\stadiumcreator\fields\models\<field>.model.mesh (RLL4: oval01_small,
        // rectangle01_small, rectangle01_large).
        if (p.contains(u"stadiumcreator/fields/models/"_s) && p.contains(u".model."_s))
            fieldModels.insert(p.section(u'.', 0, 0));
    }
    for (const QString& stadium : std::as_const(stadiums)) {
        found << u"collision/stadiums/%1/collision.collision"_s.arg(stadium)
              << u"stadiums/%1/stadium_t1.model"_s.arg(stadium)       // The stadium (with its cutscene props) ...
              << u"stadiums/%1/stadium_t1.texpack"_s.arg(stadium)     // ... and its textures.
              << u"stadiums/%1/crowd_t1.model"_s.arg(stadium)         // RLL4: the crowd (mob\N.mob).
              << u"stadiums/%1/field_t1.texpack"_s.arg(stadium)
              // RLL4: stadiumconfig.lua's shared texture pack, basePath .. "common" .. time postfix .. ".texpack".
              << u"stadiums/%1/common_t1.texpack"_s.arg(stadium);
    }
    // RLL4's hair models: each style ("top_fringe12") in the permutations it comes in, assetmap.lua's
    // getModelFileNameByGuid: name .. "_" .. mirror .. "_" .. length .. "_" .. hairline .. "_" .. gear .. "_" .. sex
    // (empty parts left out) -> "characters/hair/models/<name>/<name>" (.modelcomponent, .blendshapes, .vbo).
    if (std::any_of(allPaths.cbegin(), allPaths.cend(), [](const QString& path) {
            return path.startsWith(u"characters\\beards\\models\\"_s, Qt::CaseInsensitive);
        })) {
        static const QHash<QChar, QString> letters[5] = {
            {{u'l', u"left"_s}, {u'm', QString()}, {u'r', u"right"_s}},                       // hairMirror
            {{u's', u"short"_s}, {u'm', u"medium"_s}, {u'l', u"long"_s}},                     // hairLength
            {{u'a', u"mhl0"_s}, {u'b', u"mhl1"_s}, {u'c', u"mhl2"_s}, {u'd', u"mhl3"_s},      // hairLine
             {u'e', u"fhl0"_s}, {u'f', u"fhl1"_s}},
            {{u'_', QString()}, {u'a', u"geara"_s}, {u'b', u"gearb"_s}, {u'c', u"gearc"_s},   // hairGear
             {u'd', u"geard"_s}, {u'e', u"geare"_s}, {u'f', u"gearf"_s}, {u'g', u"gearg"_s}, {u'h', u"gearh"_s}},
            {{u'm', u"male"_s}, {u'f', u"female"_s}},                                         // sex
        };
        QString all;
        for (const char* chunk : kRll4HairStyles)
            all += QLatin1String(chunk);
        int hairNames = 0;
        for (const QString& style : all.split(u';', Qt::SkipEmptyParts)) {
            const QStringList fields = style.split(u'|');
            if (fields.size() != 6)
                continue;
            QStringList names{fields[0]};
            for (int k = 0; k < 5; ++k) {
                QStringList parts;
                for (const QChar c : fields[k + 1])
                    if (const auto it = letters[k].constFind(c); it != letters[k].constEnd())
                        parts.append(it.value());
                if (parts.isEmpty())
                    parts.append(QString());
                QStringList next;
                for (const QString& name : std::as_const(names))
                    for (const QString& part : std::as_const(parts))
                        next.append(part.isEmpty() ? name : name + u'_' + part);
                names = next;
            }
            for (const QString& name : std::as_const(names)) {
                const QString base = u"characters/hair/models/%1/%1"_s.arg(name);
                found << base + u".modelcomponent"_s << base + u".blendshapes"_s << base + u".vbo"_s;
                ++hairNames;
            }
        }
        emit updateConsole(u"Characters: %1 RLL4 hair model name(s) from the hair styles' permutations."_s.arg(hairNames));
    }
    // RLL3's hair and beard parts (its hair styles' permutations already expanded): "<name>", "<name>_game",
    // "<name>_low_game" and "<name>_headgear".
    {
        QString all;
        for (const char* chunk : kRll3HairBeardParts)
            all += QLatin1String(chunk);
        for (const QString& part : all.split(u';', Qt::SkipEmptyParts)) {
            const QString folder = part.section(u'/', 0, 0), name = part.section(u'/', 1);
            // Each part has in game and low detail in game models in its own folder ("<name>/<name>_game",
            // "<name>/<name>_low_game"). The beards, sides and the shaved / unique tops also have a version to wear
            // under headgear, with the same three levels, in a folder of its own ("<name>_headgear/<name>_headgear",
            // "..._headgear_game", "..._headgear_low_game").
            const QString root = u"characters/%1/models/"_s.arg(folder);
            QStringList bases;
            for (const QString& folderName : {name, name + u"_headgear"_s})
                for (const QString& level : {u""_s, u"_game"_s, u"_low_game"_s})
                    bases << root + folderName + u'/' + folderName + level;
            for (const QString& base : std::as_const(bases)) {
                found << base + u".modelcomponent"_s << base + u".blendshapes"_s << base + u".vbo"_s
                      << base + u".texpack"_s;
            }
        }
    }
    // Other languages of the files named for English ("ui/pregame/legal_en.texpack" -> "ui/pregame/legal_fr.texpack").
    for (const QString& path : std::as_const(allPaths)) {
        QString p = QString(path).toLower().replace(u'\\', u'/');
        const qsizetype en = p.lastIndexOf(u"_en."_s);
        if (en < 0 || p.indexOf(u'/', en) >= 0)
            continue;
        // "a/b_en.texpack.txpk": the name and its type, without the mapping's extension.
        const QString stem = p.left(en), type = p.mid(en + 4).section(u'.', 0, 0);
        for (const char* language : {"fr", "de", "it", "es", "us", "gb", "au", "nz"})
            found << u"%1_%2.%3"_s.arg(stem, QLatin1String(language), type);
    }
    // A character part's texture pack names its textures "characters/<folder>/textures/<part>_col"; the pack is
    // "characters/<folder>/models/<part>/<part>.texpack" (RLL4's player heads, beards, ...).
    static const QRegularExpression partTexture(
        u"^characters/([a-z0-9_]+)/textures/([a-z0-9_]+?)_(?:col|nrm|mat|msk)(?:_\\d+)?$"_s);
    QSet<QString> textureParts;
    for (const QString& s : strings) {
        if (const auto t = partTexture.match(s); t.hasMatch())
            textureParts.insert(t.captured(1) + u'/' + t.captured(2));
    }
    for (const QString& part : std::as_const(textureParts)) {
        const QString folder = part.section(u'/', 0, 0), name = part.section(u'/', 1);
        found << u"characters/%1/models/%2/%2.texpack"_s.arg(folder, name);
    }
    // RLL4's stadium creator crowds: "%s_crowd_t0%d_v0%d_b%d.model" (size, type, variation, capacity).
    if (std::any_of(allPaths.cbegin(), allPaths.cend(), [](const QString& path) {
            return path.startsWith(u"stadiums\\stadiumcreator\\stadia\\"_s, Qt::CaseInsensitive);
        })) {
        for (const char* size : {"small", "medium", "large", "pavaus", "paveng", "pavind"})
            for (int type = 1; type <= 3; ++type)
                for (int variation = 1; variation <= 5; ++variation)
                    for (int capacity : {0, 100, 150, 225, 300, 450, 900}) {
                        const QString crowd = u"stadiums/stadiumcreator/stadia/models/%1_crowd_t0%2_v0%3_b%4"_s
                                                  .arg(QLatin1String(size)).arg(type).arg(variation).arg(capacity);
                        found << crowd + u".model"_s << crowd + u".material"_s;
                    }
    }
    // scene_config.lua: "collision/stadiums/stadiumcreator/fields/models/" .. model .. "_collision.collision".
    for (const QString& field : std::as_const(fieldModels))
        found << u"collision/"_s + field + u"_collision.collision"_s << field + u"_collision.collision"_s;
    for (const char* prop : {"base", "goalposts", "mannequin", "tackledummy"})
        found << u"collision/props/%1.collision"_s.arg(QLatin1String(prop));

    for (const QString& name : std::as_const(found))
        addString(name);
    if (players > 0)
        emit updateConsole(u"Characters: %1 player(s) from the helper joint offsets, %2 beard style name(s)."_s
                               .arg(players).arg(beards.size()));
}

// ---------------------------------------------------------------------------------------------------------------
// Resolving
// ---------------------------------------------------------------------------------------------------------------

void NameResolverWorker::run() {
    QElapsedTimer timer;
    timer.start();

    emit updateConsole(u"Resolving file names (blobset V%1, %2)..."_s.arg(m_version + 1).arg(BlobsetHash::platformName(m_platform)));
    reportProgress(0, u"Reading the file mapping..."_s);
    std::vector<FileMappingWorker::MappingEntry> entries = FileMappingWorker::readMappingFile(m_mappingFile);
    reportProgress(2);

    QHash<quint64, qsizetype> unnamed;          // Hash -> mapping entry index.
    QHash<quint64, QString> mappedPaths;        // Every hash -> its mapped path.
    QSet<QString> takenPaths;
    QStringList allPaths;
    for (qsizetype i = 0; i < qsizetype(entries.size()); ++i) {
        const auto& e = entries[size_t(i)];
        mappedPaths.insert(e.hash, e.path);
        takenPaths.insert(e.path.toLower());
        allPaths.append(e.path);
        if (isUnnamed(e.path))
            unnamed.insert(e.hash, i);
    }
    emit updateConsole(u"%1 of %2 mapped files have no real name yet."_s.arg(unnamed.size()).arg(entries.size()));
    if (unnamed.isEmpty()) {
        emit taskSucceeded();
        emit taskFinished(u"Every file already has a name."_s);
        return;
    }

    reportProgress(3, u"Collecting candidate names..."_s);
    emit updateConsole(u"Collecting candidate names from the executables, names.txt and the game's assets..."_s);
    harvestExecutables();
    harvestUserNames();
    reportProgress(10);
    harvestAssets(mappedPaths, allPaths);
    QHash<quint64, QString> direct;   // Hash -> mapped path, for names hashed without a type.
    harvestSound(mappedPaths, direct);
    // The text, one string table per language: "strings/strings_<language>", hashed by that whole name (V4) or as
    // "<name>.<platform>" (V1 / V2: RLL2 - RLL4, Don Bradman Cricket 17). Named "strings\strings_<language>.str".
    if (m_version != static_cast<int>(CoreDefs::BlobsetVersion::V3)) {
        const std::string platformSuffix = "." + BlobsetHash::platformName(m_platform).toStdString();
        for (const char* language : {"en", "fr", "de", "es", "it", "ja", "ko", "pl", "ru", "nl", "pt", "tr", "ar", "th",
                                     "zh", "pt-br", "es-mx", "en-us", "en-gb", "en-au", "zh-hans", "zh-hant"}) {
            const QString name = u"strings/strings_"_s + QLatin1String(language);
            const quint64 hash = m_version == static_cast<int>(CoreDefs::BlobsetVersion::V4)
                                     ? fnv1a64Raw(name)
                                     : quint64(BlobsetHash::murmur2(
                                           BlobsetHash::normalise(name.toStdString() + platformSuffix), 0xDEADBEEFu,
                                           m_isBigEndian));
            direct.insert(hash, QString(name).replace(u'/', u'\\') + u".str"_s);
        }
        // RLL2 (World Cup Edition)'s scripted plays library (the AI's set plays), named in the executable and hashed
        // like the string tables, "<name>.<platform>". The file mapping takes it for animation clips (.clips).
        if (m_version != static_cast<int>(CoreDefs::BlobsetVersion::V4)) {
            const quint32 plays = BlobsetHash::murmur2(
                BlobsetHash::normalise("scriptedplay/scriptedplaylibrary" + platformSuffix), 0xDEADBEEFu, m_isBigEndian);
            direct.insert(plays, u"scriptedplay\\scriptedplaylibrary.clips"_s);
        }
    }
    // Names other games already have for the same files: Big Ant reused assets between games (RLL3's front end
    // cameras are in RLL4 as "sequences/rll3_cameras.seq"), and the same name gives the same hash in every game of
    // the same hash scheme (V1 / V2: "<name>.<type>.<platform>" MurmurHash2; V4: FNV-1a 64; V3 hashes the game in,
    // so it's left out). Only the mappings of the same platform; the kind of file still has to match (below).
    if (m_version != static_cast<int>(CoreDefs::BlobsetVersion::V3)) {
        QDir platformDir = QFileInfo(m_mappingFile).absoluteDir();   // games\<game>\<platform>\data
        platformDir.cdUp();
        const QString platformFolder = platformDir.dirName();
        QDir gamesDir = platformDir;
        gamesDir.cdUp();
        const QString thisGame = gamesDir.dirName();
        gamesDir.cdUp();
        const auto scheme = [](int version) { return version <= 1 ? 0 : version; };   // V1 and V2 hash alike.
        int borrowed = 0;
        QStringList fromGames;
        for (const QString& game : gamesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (game.compare(thisGame, Qt::CaseInsensitive) == 0)
                continue;
            const QDir otherPlatform(gamesDir.filePath(game + u'/' + platformFolder));
            const QString ini = otherPlatform.filePath(u"GameInfo.ini"_s);
            if (!QFileInfo::exists(ini))
                continue;
            const int otherVersion = QSettings(ini, QSettings::IniFormat).value(u"GameInfo/BlobsetVersion"_s, -1).toInt();
            if (otherVersion < 0 || scheme(otherVersion) != scheme(m_version))
                continue;
            int fromThis = 0;
            for (const QString& file : QDir(otherPlatform.filePath(u"data"_s)).entryList({u"*.mapping"_s}, QDir::Files)) {
                try {
                    for (const auto& e : FileMappingWorker::readMappingFile(otherPlatform.filePath(u"data/"_s + file))) {
                        if (unnamed.contains(e.hash) && !isUnnamed(e.path) && !direct.contains(e.hash)) {
                            direct.insert(e.hash, e.path);
                            ++fromThis;
                        }
                    }
                } catch (const std::exception&) {
                    // An unreadable mapping of another game: skipped.
                }
            }
            if (fromThis > 0) {
                borrowed += fromThis;
                fromGames << u"%1 (%2)"_s.arg(game).arg(fromThis);
            }
        }
        if (borrowed > 0)
            emit updateConsole(u"%1 name(s) other games' mappings have for the same files: %2."_s
                                   .arg(borrowed).arg(fromGames.join(u", "_s)));
    }
    deriveCharacterNames(allPaths);
    deriveAnimationSetNames();
    emit updateConsole(u"Collected %1 candidate strings."_s.arg(m_strings.size()));

    // Candidate bases (path without type / extension) plus every folder above them; explicit "<base>.<type>" pairs.
    QSet<QString> bases;
    QSet<QPair<QString, QString>> explicitPairs;
    emit updateStatus(u"Building candidate names from %1 strings..."_s.arg(m_strings.size()));
    qsizetype stringsDone = 0;
    for (const QString& s : std::as_const(m_strings)) {
        reportRange(60, 70, ++stringsDone, m_strings.size());
        const qsizetype slash = s.lastIndexOf(u'/');
        const QString last = s.mid(slash + 1);
        const qsizetype dot = last.indexOf(u'.');
        const QString base = dot >= 0 ? s.left(slash + 1 + dot) : s;
        if (base.isEmpty())
            continue;

        if (dot >= 0) {
            const QStringList suffixes = last.mid(dot + 1).split(u'.');
            if (!suffixes.isEmpty() && isTypeWord(suffixes.first()) && suffixes.first() != u"pc"_s
                && suffixes.first() != u"ps3"_s)
                explicitPairs.insert({base, suffixes.first()});
        }

        bases.insert(base);
        for (qsizetype p = base.indexOf(u'/'); p > 0; p = base.indexOf(u'/', p + 1))
            bases.insert(base.left(p));
    }

    const QStringList guessedTypes = compatibleExtensions().keys();
    const QString platform = BlobsetHash::platformName(m_platform);

    struct Hit {
        QString base;
        QString type;
    };
    QHash<quint64, Hit> hits;
    int rejected = 0;

    const auto tryName = [&](const QString& base, const QString& type, bool spelledOut) {
        // A guessed type on a name without a folder: with 32 bit hashes and thousands of files, junk strings match by
        // chance ("lamp.wav", "wm9k.wav" in RLL2), and no game keeps its files outside a folder.
        if (!spelledOut && !base.contains(u'/') && m_version != static_cast<int>(CoreDefs::BlobsetVersion::V4))
            return;
        const quint64 hash = BlobsetHash::hashForAsset(m_version, {base, type}, m_platform, m_gameKey);
        const auto target = unnamed.constFind(hash);
        if (target == unnamed.constEnd() || hits.contains(hash))
            return;

        const QString extension = extensionOf(entries[size_t(target.value())].path);
        const auto compatible = compatibleExtensions().constFind(type);
        const bool fits = compatible != compatibleExtensions().constEnd() ? compatible.value().contains(extension)
                                                                          : spelledOut;
        if (!fits) {
            ++rejected;   // Most likely two different names sharing a 32 bit hash.
            return;
        }
        hits.insert(hash, {base, type});
    };

    reportProgress(70, u"Hashing %1 candidate names..."_s.arg(bases.size()));
    for (const auto& pair : std::as_const(explicitPairs))
        tryName(pair.first, pair.second, true);
    // Names whose base has a dot in it, so they can't come from a "<base>.<type>" string (RLL4).
    for (const auto& [base, type] : std::initializer_list<std::pair<const char*, const char*>>{
             {"sound/rll4/master.strings", "bank"}, {"sound/rll4/master", "bank"},
             {"sound/rll4/loaded_menus_frontend", "bank"}, {"fonts/numbers_blk.ttf_sdf", "fnt"},
             {"mempacks/lua.mempack", "retail"},       // The compiled Lua scripts (every version).
             {"mempacks/luainit.mempack", "retail"},   // The scripts' load order ("intermediate\\common\\lua.mempack.retail.order").
             // Rugby 25's fast play (quick tap penalty) action set, objects\3.obj: the only action set the scripts
             // don't load by name (universe.lua loads "actionset/<name>set.dat" for the others).
             {"actionset/fastplayset", "dat"},
             // Rugby 25's menu videos (unknown\mainuncompressed_vramuncompressed\N.dat), in noesis\video.mempack.
             {"noesis/workspace/images/videos/academy_1080p", "mp4"},
             {"noesis/workspace/images/videos/career_1080p", "mp4"},
             {"noesis/workspace/images/videos/playnow_1080p", "mp4"},
             // Cricket 24's Ashes videos (unknown\mainuncompressed_vramuncompressed\N.dat), named in its scripts.
             {"noesis/workspace/images/video/ashes_hype_reel_aus", "mp4"},
             {"noesis/workspace/images/video/ashes_hype_reel_eng", "mp4"},
             {"noesis/workspace/images/video/particles", "mp4"},
             // Rugby 25's empty front end music bank (bank id 0x8B9576B6) that soundmap_banks no longer lists.
             {"sound/frontendmusic_untitled", "bnk"},
             // Tiebreak's skeleton for the non-player characters (model\\skinned\\N.mdl), in no script.
             {"rigs/npc01", "rig"}})
        tryName(QString::fromLatin1(base), QString::fromLatin1(type), true);
    // Mem packs named after the top folder of what they hold, under "mempacks/": AFL Live's shader pack
    // "mempacks/shaders" holds "shaders/<name>.shader.pc.pssg" (like "mempacks/lua", "mempacks/heatmaps").
    for (const QString& base : std::as_const(bases))
        if (!base.contains(u'/'))
            tryName(u"mempacks/"_s + base, u"mempack"_s, true);
    // FMOD wave banks, named in the FMOD project (.fev) and loaded from "sound/<bank>.fsb" (AFL Live: crowd, music,
    // sfx, tackles, vo_ump, commentary). "sfx" is shorter than the shortest string collected, so it's tried as it is.
    for (const QString& base : std::as_const(bases))
        if (!base.contains(u'/'))
            tryName(u"sound/"_s + base, u"fsb"_s, true);
    tryName(u"sound/sfx"_s, u"fsb"_s, true);
    // AFL Live's animation clip sets, "animations/<set>.gz": the players' (animationset), the crowd's (crowdset), the
    // crowd props' (crowdpropset, the floggers) and the banner's. Only "banner" is in afl.exe.
    for (const QString& base : std::as_const(bases))
        if (!base.contains(u'/'))
            tryName(u"animations/"_s + base, u"gz"_s, false);
    for (const QString& set : {u"animationset"_s, u"crowdset"_s, u"crowdpropset"_s, u"banner"_s})
        tryName(u"animations/"_s + set, u"gz"_s, false);
    // Mem packs named after the one file they hold, its extension included (AFL Live: "sound/commentarydatadc.dj"
    // holds "sound\commentarydatadc.dj": "sound/commentarydatadc.dj.mempack.pc").
    for (QString s : std::as_const(m_strings)) {
        if (s.endsWith(u".pc"_s))
            s.chop(3);
        if (s.mid(s.lastIndexOf(u'/') + 1).contains(u'.'))
            tryName(s, u"mempack"_s, true);
    }
    // Stadium parts: "stadiums/<stadium>/<part>" (RLL2: "stadiums/wc_generic/common_t2.texpack", the
    // e00_common event props "stadiums/e00_common/fieldprops_nrl", ...). The stadium folders come from the textures'
    // paths, the part names from the scripts, so every folder is tried with every name.
    {
        QSet<QString> stadiums, parts;
        for (const QString& base : std::as_const(bases)) {
            if (base.startsWith(u"stadiums/"_s) && base.count(u'/') == 1)
                stadiums.insert(base);
            // Only word-like names ("common_t2", "fieldprops_nrl_2013", "skydome"): every folder times every
            // string is a lot of tries, and 32 bit hashes then match junk ("il87") by chance.
            static const QRegularExpression partName(u"^[a-z]+(_[a-z0-9]+)*$"_s);
            const QString part = base.mid(base.lastIndexOf(u'/') + 1);
            if (part.size() >= 5 && partName.match(part).hasMatch())
                parts.insert(part);
        }
        for (const QString& stadium : std::as_const(stadiums))
            for (const QString& part : std::as_const(parts))
                tryName(stadium + u'/' + part, u"texpack"_s, false);
    }
    // RLL2's head and hair morph packs: "heads/<name>.morphpack" and the full detail "heads_lod0/<name>.morphpack"
    // (scripts: "heads_lod0/tier2.morphpack"), for every player head (the scripts' heads\<player>.lua) and hair style
    // (heads\<style>hair_base_mesh.model: "short_16", ...).
    {
        QSet<QString> heads;
        for (const QString& base : std::as_const(bases)) {
            if (!base.startsWith(u"heads/"_s) || base.count(u'/') != 1)
                continue;
            QString name = base.mid(6);
            if (name.endsWith(u"hair_base_mesh"_s))
                name.chop(14);
            if (!name.isEmpty())
                heads.insert(name);
        }
        for (const QString& name : {u"tier1"_s, u"tier2"_s, u"tier3"_s, u"bald"_s})
            heads.insert(name);
        for (const QString& name : std::as_const(heads))
            for (const QString& folder : {u"heads/"_s, u"heads_lod0/"_s})
                tryName(folder + name, u"morphpack"_s, true);
    }
    // RLL2 (World Cup Edition)'s animation sets, "animations/<name>.animset" (mapped anim\clip_packs\N.clippack): the
    // game's sets (scripts' animationsets.lua) and one per cutscene actor ("<sequence>_cutscene_player" /
    // "_cheerleader", from each sequences\<name>_cutscene.lua's animset(...); some keep the ".lua" of their sequence).
    // Only in the compressed Lua mem pack, so listed here.
    static const char* const kRll2AnimationSets[] = {
        "animationset", "animtest_cutscene_player", "ballset", "championstrophy_01_cutscene_player",
        "cheerleaders_5_cutscene_cheerleader", "conversion_success02_cutscene_player",
        "conversion_success_l_cutscene_player", "conversion_success_r_cutscene_player", "crowdpropset", "crowdset",
        "demo_gf_anthem_cu_01_cutscene.lua_player", "demo_gf_anthem_cu_02_cutscene.lua_player",
        "demo_gf_anthem_team_a_cutscene_player", "demo_gf_anthem_team_b_cutscene_player",
        "demo_gf_anthemwide_cutscene.lua_player", "demo_gf_preanthema_cutscene_player",
        "demo_gf_shot_00_cutscene_player", "demo_gf_shot_00a_cutscene_player",
        "demo_gf_shot_01_cutscene_cheerleader", "demo_gf_shot_01_cutscene_player", "demo_gf_shot_02_cutscene_player",
        "demo_gf_shot_03_cutscene_player", "demo_gf_shot_04_cutscene_cheerleader", "demo_gf_shot_04_cutscene_player",
        "demo_gf_shot_05_cutscene_player", "demo_gf_shot_06_cutscene_player",
        "demo_gf_shot_07_cutscene.lua_cheerleader", "demo_gf_shot_07_cutscene.lua_player",
        "demo_gf_wide_cutscene_cheerleader", "demo_gf_wide_cutscene_player",
        "disappoint_handgestures_01_cutscene.lua_player", "disappoint_handonhips_cutscene.lua_player",
        "disappoint_handslap_cutscene.lua_player", "disappoint_standing_01_cutscene.lua_player",
        "disappoint_standing_02_cutscene.lua_player", "disappoint_walk_01_cutscene.lua_player",
        "disappoint_walk_02_cutscene.lua_player", "fulltime_cutscene_player", "fulltime_tie_cu_1_cutscene_player",
        "fulltime_tie_cu_2_cutscene_player", "fulltime_tie_cu_3_cutscene_player",
        "fulltime_tie_mid_1_cutscene_player", "fulltime_tie_steady_cutscene_player", "fulltime_wide_cutscene_player",
        "game_intro_star_player_teama1_cutscene_cheerleader", "game_intro_star_player_teama1_cutscene_player",
        "game_intro_star_player_teamb1_cutscene_player", "game_intro_team_a_sequence_cheerleader",
        "game_intro_team_a_sequence_player", "game_intro_team_b_sequence_player",
        "grandfinal_preanthema_cutscene_player", "grandfinalintro_anthem_cu_01_cutscene.lua_player",
        "grandfinalintro_anthem_cu_02_cutscene.lua_player", "grandfinalintro_anthembreak_cutscene.lua_cheerleader",
        "grandfinalintro_anthembreak_cutscene.lua_player", "grandfinalintro_wide_cutscene.lua_player",
        "halftime_cutscene_player", "injury_lowerbody_knee_cutscene_player",
        "injury_upperbody_shoulder_cutscene_player", "nationalanthem_team_a_cutscene_player",
        "nationalanthem_team_b_cutscene_player", "prematchwaiting_cutscene_player",
        "referee_heldup_01_cutscene_player", "referee_heldup_02_cutscene_player",
        "referee_knockon_01_cutscene_player", "referee_try_01_cutscene_player", "referee_try_02_cutscene_player",
        "referee_try_03_cutscene_player", "referee_video_01_cutscene_player", "referee_video_02_cutscene_player",
        "sendoff_cutscene_player", "sinbin_cutscene_player", "test_eyetest_sequence.lua_player",
        "trycelebrate_excited_3hug_01_cutscene_player", "trycelebrate_excited_hugrunoff_cutscene_player",
        "trycelebrate_excited_lowfiveplushug_cutscene_player", "trycelebrate_excited_scruffhair2_cutscene_player",
        "trycelebrate_excited_scruffhair3_cutscene_player", "trycelebrate_excited_tapshoulder_01_cutscene_player",
        "trycelebrate_excited_throwball2handed_3_cutscene_player",
        "trycelebrate_excited_throwballhighintense_1_cutscene_player", "uiplayer"};
    for (const char* name : kRll2AnimationSets)
        tryName(u"animations/"_s + QLatin1String(name), u"animset"_s, true);
    // The FMOD strings banks ("master.strings": a dot in the name) of both sound projects, RLL3 and RLL4.
    for (const QString& project : {u"rll3"_s, u"rll4"_s, u"rll3_academy"_s, u"rll4_academy"_s})
        tryName(u"sound/%1/master.strings"_s.arg(project), u"bank"_s, true);
    // Character parts' facial expression shapes (smile, squint, brow up, mouth open, exert, blink, ...): the character
    // manager loads "%s.modelcomponent", "%s.vbo", "%s.texpack", "%s.blendshapes" and "%s.anim.blendshapes" for each
    // part (RLL4). From the parts already named and the ones named this run (the hair permutations, ...).
    {
        QSet<QString> parts;
        for (const QString& path : std::as_const(allPaths)) {
            const QString p = QString(path).toLower().replace(u'\\', u'/');
            const qsizetype at = p.indexOf(u".blendshapes."_s);
            if (p.startsWith(u"characters/"_s) && at > 0 && !p.left(at).endsWith(u".anim"_s))
                parts.insert(p.left(at));
        }
        for (const QString& s : std::as_const(m_strings))
            if (s.startsWith(u"characters/"_s) && s.endsWith(u".blendshapes"_s) && !s.endsWith(u".anim.blendshapes"_s))
                parts.insert(s.chopped(12));
        for (const QString& part : std::as_const(parts))
            tryName(part + u".anim"_s, u"blendshapes"_s, true);
    }
    // Fonts: "fonts/<font>.ttf_sdf" is a font's texture (.dds); its glyph table is the same name with the type "fnt".
    // The base has a dot in it ("daniel-regular.otf_sdf"), so it's tried as it is.
    for (const QString& path : std::as_const(allPaths)) {
        const QString p = QString(path).toLower().replace(u'\\', u'/');
        const qsizetype sdf = p.indexOf(u"_sdf."_s);
        if (p.startsWith(u"fonts/"_s) && sdf > 0)
            tryName(p.left(sdf + 4), u"fnt"_s, true);
    }
    // V4 engine files the executable loads by name with the graphics API as the type (".d11", DirectX 11): the
    // shader pack and the vertex stream formats. Their names have a dot in them, so they're tried as they are.
    if (m_version == 3) {
        for (const QString& name : {u"shaders.blob"_s, u"game.streamformat"_s, u"crowd.streamformat"_s})
            for (const QString& api : {u"d11"_s, u"d12"_s, u"d10"_s})
                tryName(name, api, true);
    }
    // RLL4's vertex stream format pack (StreamFormatPackLoader: "streamformat.blob" + ".d11"; every vertex format and
    // stream: "V:basicVF|S:basicSF", ...). Mapped as shader\vertex_formats\N.vfmt.
    for (const QString& api : {u"d11"_s, u"d10"_s, u"d9"_s}) {
        tryName(u"streamformat.blob"_s, api, true);
        tryName(u"shaders.blob"_s, api, true);   // RLL3 / RLL4's shader pack (DXBC shaders in its vram section).
    }
    // The scripts' flat Lua data (CFlatLuaTable): the tables, the start up expressions and their debug info. V4 (RL26)
    // and RLL4 (scripts\N.luadata) alike.
    for (const QString& name : {u"flatluadata"_s, u"flatluadata.init"_s, u"flatluadata.init.debug"_s})
        tryName(name, u"blob"_s, true);
    // The animation sets the cutscenes load by name: cutscene_listener.lua's "animations/" .. animsetName ..
    // ".animset" (Tiebreak, Rugby 25, ...): every script constant is tried (64 bit hashes: no chance matches).
    if (m_version == static_cast<int>(CoreDefs::BlobsetVersion::V4)) {
        for (const QString& word : std::as_const(m_luaWords))
            tryName(u"animations/"_s + word, u"animset"_s, true);
        // The Wwise banks the scripts / script config load by name ("cutscenesfrontend", "envmelbourne", "uisfx":
        // Tiebreak's soundmap): "sound/<bank>.bnk".
        for (const QString& word : std::as_const(m_luaWords))
            tryName(u"sound/"_s + word, u"bnk"_s, true);
        // crowd_tennis.lua: "crowd/nodefiles/" .. venue .. "_crowdnodes.nodefile" (Tiebreak: "stadium"), and the
        // light animations light_animations_list.lua names ("light_animations/lighting_tb10_test.lightanim").
        for (const QString& word : std::as_const(m_luaWords)) {
            tryName(u"crowd/nodefiles/"_s + word + u"_crowdnodes"_s, u"nodefile"_s, true);
            tryName(u"light_animations/"_s + word, u"lightanim"_s, true);
            tryName(u"rigs/"_s + word, u"rig"_s, true);   // Skeletons: "rigs/player_male.rig".
        }
        // Cricket 24's player voice banks: soundmap's "Voice%02d_%s_bat" (voice number, "Male" / "Female"),
        // "sound/voice20_male_keep.bnk", and the "_indoor" banks no script string names (the kinds are listed too, in case
        // the format strings aren't among the strings collected). 64 bit hashes: other games can't match by chance.
        {
            static const QRegularExpression voiceFormat(u"^voice%02d_%s_([a-z]+)$"_s);
            QSet<QString> kinds{u"bat"_s, u"bowl"_s, u"field"_s, u"keep"_s, u"indoor"_s};
            for (const QString& s : std::as_const(m_strings))
                if (const auto m = voiceFormat.match(s.toLower()); m.hasMatch())
                    kinds.insert(m.captured(1));
            for (int n = 0; n < 100; ++n)
                for (const QString& sex : {u"male"_s, u"female"_s})
                    for (const QString& kind : std::as_const(kinds))
                        for (const QString& gap : {u"_"_s, u"__"_s})   // "voice16_female__keep": a double underscore.
                            tryName(u"sound/voice%1_%2%3%4"_s.arg(n, 2, 10, QLatin1Char('0')).arg(sex, gap, kind),
                                    u"bnk"_s, true);
        }
        // The props list (Cricket 24's propslist.lua: "scenes/models/tourBus_international_static") names the static
        // props under "props/" without the "_static": "props/scenes/models/tourbus_international.model.mesh" and its
        // ".texpack.txpl".
        for (const QString& s : std::as_const(m_strings)) {
            if (!s.endsWith(u"_static"_s, Qt::CaseInsensitive) || !s.contains(u'/'))
                continue;
            const QString base = u"props/"_s + s.chopped(7).toLower();
            tryName(base, u"model"_s, true);
            tryName(base, u"texpack"_s, true);
        }
        for (const QString& base : std::as_const(bases))
            if (!base.contains(u'/') && !base.contains(u'.'))
                tryName(u"sound/"_s + base, u"bnk"_s, true);
    }
    // Tiebreak's players' stroke sets: universe.lua loads "actionset/strokeset_" .. groundStrokeAnimSet .. ".dat" and
    // "animations/profile" .. groundStrokeAnimSet .. ".animset" for each player (objects\N.obj, anim\packs\N.anims).
    // The set names are the players' surnames ("augeraliassime"): the prefix of the animations in their stroke set
    // ("augeraliassime_swing_fh_f_ch_..."), or (a set without animations) the end of a helper joint script's player
    // name ("hj_base_felixaugeraliassime_2026").
    if (m_version == static_cast<int>(CoreDefs::BlobsetVersion::V4)) {
        static const QRegularExpression year(u"_\\d{4}$"_s);
        QSet<QString> strokeSets;
        for (const QString& s : std::as_const(m_strings)) {
            const QString name = s.mid(s.lastIndexOf(u'/') + 1);
            if (const qsizetype swing = name.indexOf(u"_swing_"_s); swing > 0)
                strokeSets.insert(name.left(swing));
            if (const qsizetype hj = name.indexOf(u"hj_base_"_s); hj >= 0) {
                QString player = name.mid(hj + 8);
                player.remove(year);
                for (qsizetype k = 0; k + 3 <= player.size(); ++k)
                    strokeSets.insert(player.mid(k));
            }
        }
        for (const QString& set : std::as_const(strokeSets)) {
            tryName(u"actionset/strokeset_"_s + set, u"dat"_s, true);
            tryName(u"animations/profile"_s + set, u"animset"_s, true);
        }
    }
    // Rugby 25's and Tiebreak's character models (model\skinned\N.mdl / .mdat): design/characters/assetmap.lua's
    // getModelFileNameByItem, basePath .. "/models/" .. name .. "/" .. name plus the item's permutations ("_gearf",
    // "_male", "_short_female", ...), for the asset map items' names; assetmapitems.lua adds each tier 1 player's
    // "<player>_side", "_side2", "_haircap" / "_bothcap" items at load time.
    if (!m_characterItems.isEmpty()) {
        const auto tryPart = [&](const QString& folder, const QString& name, const QStringList& suffixes) {
            for (const QString& suffix : suffixes) {
                const QString part = name + suffix;
                const QString base = u"characters/%1/models/%2/%2"_s.arg(folder, part);
                tryName(base, u"modelcomponent"_s, true);
                tryName(base, u"blendshapes"_s, true);
            }
        };
        // The permutations' names the games append (Rugby 25, Tiebreak): sex, hair gear (headband, cap, ...: geara -
        // gearh), the clothes' "dyn" (cloth simulated) / short / long versions, the player bodies' sleeves and legs.
        const QStringList none{QString()};
        const QStringList sexes{u"_male"_s, u"_female"_s};
        QStringList hair = none + sexes, clothes = none + sexes, player;
        for (const QChar gear : u"abcdefgh"_s) {
            hair << u"_gear"_s + gear;
            for (const QString& sex : sexes)
                hair << u"_gear"_s + gear + sex;
        }
        for (const QString& sex : sexes) {
            clothes << u"_dyn"_s + sex << u"_short"_s + sex << u"_long"_s + sex;
            for (const QString& sleeve : {QString(), u"_shortsleeve"_s, u"_longsleeve"_s, u"_singlet"_s})
                for (const QString& legs : {QString(), u"_pants"_s, u"_shorts"_s})
                    player << sex + sleeve + legs;
        }
        const QStringList sexed = none + sexes;
        for (const QString& name : std::as_const(m_characterItems)) {
            for (const QString& folder : {u"heads"_s, u"beards"_s, u"helmets"_s, u"haircap"_s, u"trophies"_s, u"hats"_s,
                                          u"bag"_s, u"towel"_s})
                tryPart(folder, name, none);
            tryPart(u"haircap"_s, name, {u"_haircap"_s, u"_bothcap"_s});
            for (const QString& side : {QString(), u"_side"_s, u"_side2"_s})
                tryPart(u"hair"_s, name + side, hair);
            tryPart(u"tops"_s, name, clothes);
            tryPart(u"bottoms"_s, name, clothes);
            tryPart(u"player"_s, name, player);
            for (const QString& folder : {u"socks"_s, u"shoes"_s, u"mouthwear"_s, u"eyewear"_s, u"armwear"_s, u"flag"_s,
                                          u"whistle"_s, u"accessories"_s, u"necklace"_s})
                tryPart(folder, name, sexed);
            // The uniform tops' / bottoms' wrinkle maps (uniformTop_wrinkleConfig: "f_clothes_top_soft01_short_female").
            tryName(u"animations/wrinklemaps/"_s + name, u"wrinklemap"_s, true);
        }
        // Cricket 24 makes each hat per player: "characters/hats/models/<player>_<hat>/<player>_<hat>"
        // ("aaliyahalleyne_baggygreen"), for the players with a head or a hair cap and every asset map item name.
        static const QRegularExpression playerPart(
            u"^characters/(?:heads|haircap)/models/([a-z0-9]+?)(?:_haircap)?/"_s);
        QSet<QString> players;
        for (const QString& path : std::as_const(allPaths))
            if (const auto m = playerPart.match(QString(path).toLower().replace(u'\\', u'/')); m.hasMatch())
                players.insert(m.captured(1));
        for (const QString& player : std::as_const(players))
            for (const QString& item : std::as_const(m_characterItems))
                tryPart(u"hats"_s, player + u'_' + item, none);
        emit updateConsole(u"Characters: %1 asset map item name(s) tried as character models."_s
                               .arg(m_characterItems.size()));
    }
    qsizetype done = 0;
    for (const QString& base : std::as_const(bases)) {
        for (const QString& type : std::as_const(guessedTypes))
            tryName(base, type, false);
        ++done;
        const int percent = 70 + int(25 * done / std::max<qsizetype>(1, bases.size()));
        if (percent != m_lastPercent)   // Only build the status text when the bar moves.
            reportProgress(percent, u"Hashing candidate names (%1 of %2, %3 match(es) so far)..."_s
                                .arg(done).arg(bases.size()).arg(hits.size()));
    }

    // Whole names (no type): only for files the mapping already calls the same kind.
    QHash<quint64, QString> directHits;
    for (auto it = direct.cbegin(); it != direct.cend(); ++it) {
        const auto target = unnamed.constFind(it.key());
        if (target == unnamed.constEnd() || hits.contains(it.key()))
            continue;
        // The string tables are lists of length prefixed strings the file mapping can't tell from other data, so it
        // may have guessed another kind for them (RLL4's strings_en was "model\index\19632.idx").
        const bool stringTable = it.value().startsWith(u"strings\\strings_"_s) && it.value().endsWith(u".str"_s);
        if (!stringTable && extensionOf(entries[size_t(target.value())].path) != extensionOf(it.value())) {
            ++rejected;
            continue;
        }
        directHits.insert(it.key(), it.value());
    }

    reportProgress(95, u"Renaming %1 matched file(s)..."_s.arg(hits.size() + directHits.size()));

    // Rename.
    QStringList report;
    int renamed = 0;
    for (auto it = directHits.cbegin(); it != directHits.cend(); ++it) {
        auto& entry = entries[size_t(unnamed.value(it.key()))];
        const QString& path = it.value();
        if (!FileMappingWorker::validatePath(path).isEmpty() || takenPaths.contains(path.toLower()))
            continue;
        report.append(u"%1  %2  ->  %3"_s.arg(BlobsetHash::toHex(it.key(), 16), entry.path, path));
        takenPaths.insert(path.toLower());
        entry.path = path;
        ++renamed;
    }
    for (auto it = hits.cbegin(); it != hits.cend(); ++it) {
        auto& entry = entries[size_t(unnamed.value(it.key()))];
        const QString extension = extensionOf(entry.path);
        QString path = it.value().base;
        path.replace(u'/', u'\\');
        if (it.value().type == u"texture"_s && extension == u"dds"_s)
            path += u'.' + platform + u".dds"_s;   // Like the textures the mapping already names.
        else
            path += u'.' + it.value().type + (extension.isEmpty() || extension == it.value().type ? QString()
                                                                                                    : u'.' + extension);

        if (!FileMappingWorker::validatePath(path).isEmpty() || takenPaths.contains(path.toLower()))
            continue;

        report.append(u"%1  %2  ->  %3"_s.arg(BlobsetHash::toHex(it.key(), m_version == 3 ? 16 : 8), entry.path, path));
        takenPaths.insert(path.toLower());
        entry.path = path;
        ++renamed;
    }

    // V4 files whose real name isn't known yet but whose kind is: moved out of unknown\ into their folder, keeping
    // their number (so they still count as unnamed and a later run can name them).
    // Files whose hashed name couldn't be worked out but whose name is known: given that name.
    static const QHash<quint64, QString> kKnownNames = {
    };
    // Files older mappings gave a made up name: AFL Live's FMOD project was "sound\\fsb\\afll.fev".
    static const QHash<quint64, std::pair<QString, QString>> kRenamed = {
        {0xC5458D3Cull, {u"sound\\fsb\\afll.fev"_s, u"sound\\afl.fev"_s}},
        // The Lua load order's mem pack is "mempacks/luainit.mempack.retail" (RLL4 0x78CE33E3, V4 0x73B87B2E6AD0F9BE),
        // named after the file it holds by earlier versions.
        {0x78CE33E3ull, {u"mempacks\\lua.mempack.retail.order.m3mp"_s, u"mempacks\\luainit.mempack.retail.m3mp"_s}},
        {0x73B87B2E6AD0F9BEull, {u"mempacks\\lua.mempack.retail.order.m3mp"_s, u"mempacks\\luainit.mempack.retail.m3mp"_s}},
        // RLL2 WCE: a chance match of an earlier resolver; Old Trafford's (its lighting texture) common_t2 pack.
        {0x422C6988ull, {u"stadiums\\nrl_dairyfarmers\\il87.texpack.txpk"_s,
                         u"stadiums\\sl_oldtrafford\\common_t2.texpack.txpk"_s}},
    };
    for (auto& entry : entries) {
        // RLL2's FMOD project, named "sound\\rl4.fev.wav" while the mapping took it for a wav.
        if (entry.path.compare(u"sound\\rl4.fev.wav"_s, Qt::CaseInsensitive) == 0
            && !takenPaths.contains(u"sound\\rl4.fev"_s)) {
            report.append(u"%1  %2  ->  %3"_s.arg(BlobsetHash::toHex(entry.hash, 8), entry.path, u"sound\\rl4.fev"_s));
            takenPaths.insert(u"sound\\rl4.fev"_s);
            entry.path = u"sound\\rl4.fev"_s;
            ++renamed;
            continue;
        }
        const auto old = kRenamed.constFind(entry.hash);
        if (old == kRenamed.constEnd() || entry.path.compare(old->first, Qt::CaseInsensitive) != 0
            || takenPaths.contains(old->second.toLower()))
            continue;
        report.append(u"%1  %2  ->  %3"_s.arg(BlobsetHash::toHex(entry.hash, 8), entry.path, old->second));
        takenPaths.insert(old->second.toLower());
        entry.path = old->second;
        ++renamed;
    }
    for (auto& entry : entries) {
        const auto known = kKnownNames.constFind(entry.hash);
        if (known == kKnownNames.constEnd() || !isUnnamed(entry.path) || takenPaths.contains(known->toLower()))
            continue;
        report.append(u"%1  %2  ->  %3"_s.arg(BlobsetHash::toHex(entry.hash, 16), entry.path, known.value()));
        takenPaths.insert(known->toLower());
        entry.path = known.value();
        ++renamed;
    }
    if (m_version == 3) {
        static const QHash<quint64, QString> kKnownFolders = {
            {0xA1305FAF1998A7F8ull, u"sound\\"_s},                      // 38 raw PCM sound effects (UI / career).
            {0xB8EBD4D5590BD38Cull, u"stadiums\\bokarinastadium\\"_s}, // A texture pack (glasstransparent) after
            {0x68BBD693F5EC0B88ull, u"stadiums\\endeavourfield\\"_s},  // ... / the stadium's stadium_t1.texpack.
        };
        for (auto& entry : entries) {
            const auto folder = kKnownFolders.constFind(entry.hash);
            if (folder == kKnownFolders.constEnd()
                || !(entry.path.startsWith(u"unknown\\"_s, Qt::CaseInsensitive)
                     || entry.path.startsWith(u"dds_txpk\\"_s, Qt::CaseInsensitive)))
                continue;
            const QString path = folder.value() + entry.path.section(u'\\', -1);
            if (takenPaths.contains(path.toLower()))
                continue;
            report.append(u"%1  %2  ->  %3"_s.arg(BlobsetHash::toHex(entry.hash, 16), entry.path, path));
            takenPaths.insert(path.toLower());
            entry.path = path;
            ++renamed;
        }
    }

    if (renamed == 0) {
        reportProgress(100, u"Done."_s);
        emit taskSucceeded();
        emit taskFinished(u"No new names found (%1 candidate names tried). Add names to names.txt and try again."_s
                              .arg(bases.size()));
        return;
    }

    reportProgress(97, u"Writing the file mapping..."_s);
    FileMappingWorker::sortEntries(entries);
    FileMappingWorker::writeMappingFile(m_mappingFile, entries);
    reportProgress(99, u"Writing resolved_names.txt..."_s);

    // What was renamed, newest run first.
    const QString reportPath = QFileInfo(m_mappingFile).absoluteDir().filePath(u"resolved_names.txt"_s);
    QByteArray previous;
    {
        QFile old(reportPath);
        if (old.open(QIODevice::ReadOnly))
            previous = old.readAll();
    }
    report.sort(Qt::CaseInsensitive);
    QSaveFile out(reportPath);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(u"# %1: %2 file(s) named\r\n"_s.arg(QDateTime::currentDateTime().toString(Qt::ISODate)).arg(renamed).toUtf8());
        out.write(report.join(u"\r\n"_s).toUtf8() + "\r\n\r\n");
        out.write(previous);
        out.commit();
    }

    for (const auto& lineList = report.mid(0, 50); const QString& line : lineList)
        emit updateConsole(line);
    if (report.size() > 50)
        emit updateConsole(u"... and %1 more (see resolved_names.txt)."_s.arg(report.size() - 50));
    if (rejected > 0)
        emit updateConsole(u"%1 hash match(es) were skipped because the type didn't fit the file (chance collisions)."_s
                               .arg(rejected));

    reportProgress(100, u"Done."_s);
    emit taskSucceeded();
    emit taskFinished(u"Named %1 of %2 unknown files in %3 s (%4 still unknown). The list is in %5."_s
                          .arg(renamed).arg(unnamed.size()).arg(timer.elapsed() / 1000)
                          .arg(unnamed.size() - renamed).arg(QDir::toNativeSeparators(reportPath)));
}
