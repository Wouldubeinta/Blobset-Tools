#include "xml_models.h"

// ==========================================
// XmlGameSelectionInfo Implementation
// ==========================================
bool XmlGameSelectionInfo::deserialize(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QXmlStreamReader xml(&file);
    games.clear();

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == u"Game") {
                GameEntry entry;

                // Track child elements inside the individual Game tag block
                while (!(xml.readNext() == QXmlStreamReader::EndElement && xml.name() == u"Game") && !xml.atEnd()) {
                    if (xml.tokenType() == QXmlStreamReader::StartElement) {
                        QString tagName = xml.name().toString();
                        if (tagName == u"Name") {
                            entry.name = xml.readElementText();
                        } else if (tagName == u"FolderName") {
                            entry.folderName = xml.readElementText();
                        } else if (tagName == u"ModFolder") {
                            entry.modFolder = xml.readElementText();
                        } else if (tagName == u"PC") {
                            entry.pc = (xml.readElementText() == u"true");
                        } else if (tagName == u"PS3") {
                            entry.ps3 = (xml.readElementText() == u"true");
                        } else if (tagName == u"Xbox360") {
                            entry.xbox360 = (xml.readElementText() == u"true");
                        } else if (tagName == u"Image") {
                            entry.image = (xml.readElementText());
                        } else {
                            xml.skipCurrentElement(); // Safely skip unknown elements
                        }
                    }
                }
                games.append(entry);
            }
        }
    }
    return !xml.hasError();
}

// ==========================================
// XmlExtractFileInfo
// ==========================================
bool XmlExtractFileInfo::deserialize(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QXmlStreamReader xml(&file);
    entries.clear();

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == u"ExtractFileInfo") {
                index = xml.attributes().value("Index").toUInt();
                folderHash = xml.attributes().value("FolderHash").toString();
                fileNameHash = xml.attributes().value("FileNameHash").toString();
                isCompressed = xml.attributes().value("IsCompressed") == u"true";
                const QXmlStreamAttributes a = xml.attributes();
                type = a.value("Type").toString();
                platform = a.value("Platform").toString();
                version = a.hasAttribute("Version") ? a.value("Version").toInt() : -1;
                outputName = a.value("OutputName").toString();
                recordAlignment = a.value("RecordAlignment").toUInt();
                dataAlignment = a.value("DataAlignment").toUInt();
                movie = a.value("Movie").toString();
            } else if (xml.name() == u"Entry") {
                ExtractFileEntry entry;
                entry.filePath = xml.attributes().value("FilePath").toString();
                entry.file = xml.attributes().value("File").toString();
                if (xml.attributes().hasAttribute("ImageType"))
                    entry.imageType = xml.attributes().value("ImageType").toUInt();
                entries.append(entry);
            }
        }
    }
    return !xml.hasError();
}

bool XmlExtractFileInfo::serialize(const QString& filePath) const {
    // Written to a temporary file and swapped in only when it's complete.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();

    xml.writeStartElement("ExtractFileInfo");
    xml.writeAttribute("Index", QString::number(index));
    xml.writeAttribute("FolderHash", folderHash);
    xml.writeAttribute("FileNameHash", fileNameHash);
    xml.writeAttribute("IsCompressed", isCompressed ? "true" : "false");
    if (!type.isEmpty())
        xml.writeAttribute("Type", type);
    if (!platform.isEmpty())
        xml.writeAttribute("Platform", platform);
    if (version >= 0)
        xml.writeAttribute("Version", QString::number(version));
    if (!outputName.isEmpty())
        xml.writeAttribute("OutputName", outputName);
    if (recordAlignment > 0)
        xml.writeAttribute("RecordAlignment", QString::number(recordAlignment));
    if (dataAlignment > 0)
        xml.writeAttribute("DataAlignment", QString::number(dataAlignment));
    if (!movie.isEmpty())
        xml.writeAttribute("Movie", movie);

    for (const auto& entry : entries) {
        xml.writeStartElement("Entry");
        xml.writeAttribute("FilePath", entry.filePath);
        if (!entry.file.isEmpty())
            xml.writeAttribute("File", entry.file);
        if (type == u"TXPK" || type == u"BSB")
            xml.writeAttribute("ImageType", QString::number(entry.imageType));
        xml.writeEndElement();
    }

    xml.writeEndElement(); // ExtractFileInfo
    xml.writeEndDocument();
    return !xml.hasError() && file.commit();
}

// ==========================================
// XmlModifyFileInfo
// ==========================================
bool XmlModifyFileInfo::deserialize(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QXmlStreamReader xml(&file);
    entries.clear();

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == u"ModifyFileInfo") {
                index = xml.attributes().value("Index").toUInt();
                folderHash = xml.attributes().value("FolderHash").toString();
                fileNameHash = xml.attributes().value("FileNameHash").toString();
                isCompressed = xml.attributes().value("IsCompressed") == u"true";
                mainCompressedSize = xml.attributes().value("MainCompressedSize").toUInt();
                mainUnCompressedSize = xml.attributes().value("MainUnCompressedSize").toUInt();
                vramCompressedSize = xml.attributes().value("VramCompressedSize").toUInt();
                vramUnCompressedSize = xml.attributes().value("VramUnCompressedSize").toUInt();
            } else if (xml.name() == u"Entry") {
                // FIX: Gather all data attributes into a single entry object instead of broken multi-appends
                ModifyFileEntry entry;
                entry.filePath = xml.attributes().value("FilePath").toString();
                entry.bufferSize = xml.attributes().value("BufferSize").toUInt();
                entry.ddsType = xml.attributes().value("DDSType").toUShort(); // fits quint8
                entry.ddsMipMaps = xml.attributes().value("DDSMipMaps").toUShort();
                entry.unknown1 = xml.attributes().value("Unknown1").toUInt();
                entry.unknown2 = xml.attributes().value("Unknown2").toUShort();
                entry.ddsWidth = xml.attributes().value("DDSWidth").toUShort();
                entry.ddsHeight = xml.attributes().value("DDSHeight").toUShort();
                entry.ddsImageType = xml.attributes().value("DDSImageType").toUShort();
                entries.append(entry);
            }
        }
    }
    return !xml.hasError();
}

bool XmlModifyFileInfo::serialize(const QString& filePath) const {
    // Written to a temporary file and swapped in only when it's complete.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();

    xml.writeStartElement("ModifyFileInfo");
    xml.writeAttribute("Index", QString::number(index));
    xml.writeAttribute("FolderHash", folderHash);
    xml.writeAttribute("FileNameHash", fileNameHash);
    xml.writeAttribute("IsCompressed", isCompressed ? "true" : "false");
    xml.writeAttribute("MainCompressedSize", QString::number(mainCompressedSize));
    xml.writeAttribute("MainUnCompressedSize", QString::number(mainUnCompressedSize));
    xml.writeAttribute("VramCompressedSize", QString::number(vramCompressedSize));
    xml.writeAttribute("VramUnCompressedSize", QString::number(vramUnCompressedSize));

    for (const auto& entry : entries) {
        xml.writeStartElement("Entry");
        // FIX: Missing XML attributes added to serialization loop
        xml.writeAttribute("FilePath", entry.filePath);
        xml.writeAttribute("BufferSize", QString::number(entry.bufferSize));
        xml.writeAttribute("DDSType", QString::number(entry.ddsType));
        xml.writeAttribute("DDSMipMaps", QString::number(entry.ddsMipMaps));
        xml.writeAttribute("Unknown1", QString::number(entry.unknown1));
        xml.writeAttribute("Unknown2", QString::number(entry.unknown2));
        xml.writeAttribute("DDSWidth", QString::number(entry.ddsWidth));
        xml.writeAttribute("DDSHeight", QString::number(entry.ddsHeight));
        xml.writeAttribute("DDSImageType", QString::number(entry.ddsImageType));
        xml.writeEndElement();
    }

    xml.writeEndElement(); // ModifyFileInfo
    xml.writeEndDocument();
    return !xml.hasError() && file.commit();
}
