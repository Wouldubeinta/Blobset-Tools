#ifndef XML_MODELS_H
#define XML_MODELS_H

#include <QString>
#include <QVector>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QSaveFile>
#include <QFile>

struct GameEntry {
    QString name;
    QString folderName;
    QString modFolder;
    bool pc = false;
    bool ps3 = false;
    bool xbox360 = false;
    QString image;
};

class XmlGameSelectionInfo {
public:
    QVector<GameEntry> games;

    bool deserialize(const QString& filePath);
};

struct ExtractFileEntry {
    QString filePath;           // The path stored inside the TXPK / M3MP, exactly as the game has it.
    QString file;               // Creator lists: the extracted file, relative to the list's folder.
    quint32 imageType = 1;      // TXPK only: the record's image type.
};

struct ModifyFileEntry {
    QString filePath;
    quint32 bufferSize = 0;
    quint8 ddsType = 0;
    quint8 ddsMipMaps = 0;
    quint32 unknown1 = 0;
    quint16 unknown2 = 0;
    quint16 ddsWidth = 0;
    quint16 ddsHeight = 0;
    quint16 ddsImageType = 0;
};

// Also the creator list the TXPK / M3MP viewers write on Extract All (TXPK_List.xml / M3MP_List.xml), which the
// TXPK / M3MP Creators read back: the optional attributes below are only written when they are set.
class XmlExtractFileInfo {
public:
    quint32 index = 0;
    QString folderHash;
    QString fileNameHash;
    bool isCompressed = false;
    QString type;               // "TXPK" / "M3MP"
    QString platform;           // "PC" / "PS3" / "Xbox360"
    int version = -1;           // CoreDefs::BlobsetVersion of the game it came from, -1 = unknown.
    QString outputName;         // File name the creator suggests (e.g. "12815.txpk").
    quint32 recordAlignment = 0;    // TXPK: alignment of the info records (1 = packed), 0 = unknown.
    quint32 dataAlignment = 0;      // TXPK: alignment of the texture data in the vram section, 0 = unknown.
    QString movie;                  // BSB: the extracted Flash movie (.swf), relative to the list's folder.
    QVector<ExtractFileEntry> entries;

    bool deserialize(const QString& filePath);
    bool serialize(const QString& filePath) const;
};

class XmlModifyFileInfo {
public:
    quint32 index = 0;
    QString folderHash;
    QString fileNameHash;
    bool isCompressed = false;
    quint32 mainCompressedSize = 0;
    quint32 mainUnCompressedSize = 0;
    quint32 vramCompressedSize = 0;
    quint32 vramUnCompressedSize = 0;
    QVector<ModifyFileEntry> entries;

    bool deserialize(const QString& filePath);
    bool serialize(const QString& filePath) const;
};

#endif // XML_MODELS_H

