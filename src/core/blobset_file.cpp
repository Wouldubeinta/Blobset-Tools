#include "blobset_file.h"
#include "utilities.h"

#include <QIODevice>
#include <QtEndian>

bool BlobsetFile::read(QDataStream& in, CoreDefs::BlobsetVersion version, bool isBigEndian, int gameId) {
    in.setByteOrder(isBigEndian ? QDataStream::BigEndian : QDataStream::LittleEndian);

    Q_UNUSED(gameId)
    const auto isMagic = [](quint32 value) { return value == 1112493122 || value == 1112297282; }; // 'BLOB' / 'BOLB'

    // Some games put a 20 byte SHA1 in front of the magic (RLL2/RLL3/DBC14/..., and RLL4 too), others don't
    // (AFLL, TTC). Look for the magic instead of keying it off the game id.
    in >> magic;
    if (!isMagic(magic)) {
        // Those 4 bytes were the start of the SHA1: put them back in file order, then read the rest of it.
        sha1Hash.resize(20);
        if (in.byteOrder() == QDataStream::BigEndian)
            qToBigEndian(magic, sha1Hash.data());
        else
            qToLittleEndian(magic, sha1Hash.data());
        in.readRawData(sha1Hash.data() + 4, 16);
        in >> magic;
        if (!isMagic(magic))
            return false;
    }

    if (version != CoreDefs::BlobsetVersion::V3 && version != CoreDefs::BlobsetVersion::V4) {
        in >> blobsetCount;
    }

    in >> filesCount;
    if (in.status() != QDataStream::Ok)
        return false;

    // A corrupt count must not turn into a huge allocation: every entry takes at least 8 header bytes.
    if (const QIODevice* device = in.device(); device && !device->isSequential()
        && qint64(filesCount) * 8 > device->size() - device->pos())
        return false;
    entries.resize(filesCount);

    for (quint32 i = 0; i < filesCount; ++i) {
        BlobsetEntry& e = entries[i];

        if (version == CoreDefs::BlobsetVersion::V4) {

            in >> e.folderFileHash64;
            auto [folderHashName, fileHashName] = Utilities::getHashString64(e.folderFileHash64);
            e.folderHashName = folderHashName;
            e.fileHashName = fileHashName;
        } 
        else if (version == CoreDefs::BlobsetVersion::V1 || version == CoreDefs::BlobsetVersion::V2) {
            in >> e.mainFinalOffset >> e.mainCompressedSize >> e.mainUnCompressedSize;
            in >> e.vramFinalOffset >> e.vramCompressedSize >> e.vramUnCompressedSize;
            
            in >> e.folderFileHash32;
            auto [folderHashName, fileHashName] = Utilities::getHashString32(e.folderFileHash32);
            e.folderHashName = folderHashName;
            e.fileHashName = fileHashName;
            
            in >> e.blobSetNumber;
        } 
        else {
            in >> e.mainCompressedSize >> e.mainUnCompressedSize;
            in >> e.vramCompressedSize >> e.vramUnCompressedSize;
            
            in >> e.folderFileHash32;
            auto [folderHashName, fileHashName] = Utilities::getHashString32(e.folderFileHash32);
            e.folderHashName = folderHashName;
            e.fileHashName = fileHashName;
            
            quint32 dummy;
            in >> dummy;
        }
    }

    if (version == CoreDefs::BlobsetVersion::V4) {
        for (quint32 i = 0; i < filesCount; ++i) {
            in >> entries[i].mainCompressedSize >> entries[i].mainUnCompressedSize;
            in >> entries[i].vramCompressedSize >> entries[i].vramUnCompressedSize;
        }
    }
    return in.status() == QDataStream::Ok;   // False when the header is truncated.
}
