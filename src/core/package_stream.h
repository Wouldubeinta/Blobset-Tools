#ifndef PACKAGE_STREAM_H
#define PACKAGE_STREAM_H

#include <QFile>
#include <QBuffer>
#include <QDataStream>
#include <QString>
#include <QByteArray>
#include <stdexcept>
#include <memory>

class PackageStream {
public:
    enum Endianness { Little, Big };

    // Opens a file (throws std::runtime_error when it can't be opened).
    explicit PackageStream(const QString& filePath, Endianness endian = Little,
                           QIODevice::OpenMode mode = QIODevice::ReadWrite) {
        auto file = std::make_unique<QFile>(filePath);
        if (!file->open(mode)) {
            throw std::runtime_error("Failed to open file: " + filePath.toStdString());
        }
        m_device = std::move(file);
        m_stream.setDevice(m_device.get());
        setEndianness(endian);
    }

    // Memory-buffered constructor (takes an existing open QBuffer)
    explicit PackageStream(QBuffer* buffer, Endianness endian = Little) {
        if (!buffer || !buffer->isOpen()) {
            throw std::runtime_error("Buffer must be valid and open");
        }
        m_stream.setDevice(buffer);
        setEndianness(endian);
    }

    void setEndianness(Endianness endian) {
        m_stream.setByteOrder(endian == Big ? QDataStream::BigEndian : QDataStream::LittleEndian);
    }

    // ==========================================
    // READING FUNCTIONS (throw std::runtime_error at the end of the data)
    // ==========================================

    quint8 readByte() { return read<quint8>(); }
    quint16 readUInt16() { return read<quint16>(); }
    quint32 readUInt32() { return read<quint32>(); }
    quint64 readUInt64() { return read<quint64>(); }

    QString readNullTerminatedString() {
        QByteArray rawData;
        char ch;
        QIODevice* dev = m_stream.device();

        while (dev && dev->getChar(&ch)) {
            if (ch == '\0') {
                break;
            }
            rawData.append(ch);
        }

        return QString::fromUtf8(rawData);
    }

    QByteArray readBytes(qint64 length) {
        // Checked before allocating: a corrupt size must not turn into a multi-gigabyte allocation.
        QIODevice* dev = m_stream.device();
        if (length < 0 || !dev || length > dev->size() - dev->pos())
            throw std::runtime_error("Unexpected End of File");

        QByteArray data(length, Qt::Uninitialized);
        if (m_stream.readRawData(data.data(), length) != length)
            throw std::runtime_error("Unexpected End of File");
        return data;
    }

    // ==========================================
    // WRITING FUNCTIONS
    // ==========================================

    void writeByte(quint8 value) {
        m_stream << value;
    }

    void writeUInt16(quint16 value) {
        m_stream << value;
    }

    void writeUInt32(quint32 value) {
        m_stream << value;
    }

    void writeUInt64(quint64 value) {
        m_stream << value;
    }

    void writeBytes(const QByteArray& data) {
        int bytesWritten = m_stream.writeRawData(data.constData(), data.size());
        if (bytesWritten != data.size()) {
            throw std::runtime_error("Failed to write all bytes to stream");
        }
    }

    void writeNullTerminatedString(const QString& str) {
        QByteArray rawData = str.toLatin1();
        writeBytes(rawData);
        writeByte(0);
    }

    // ==========================================
    // UTILITY FUNCTIONS
    // ==========================================

    void seek(qint64 position) {
        if (m_stream.device()) {
            m_stream.device()->seek(position);
            m_stream.resetStatus();   // A read past the end before doesn't block reads at the new position.
        }
    }

    qint64 position() const {
        return m_stream.device() ? m_stream.device()->pos() : 0;
    }

    qint64 length() const {
        return m_stream.device() ? m_stream.device()->size() : 0;
    }

    bool atEnd() const {
        return m_stream.device() ? m_stream.device()->atEnd() : true;
    }

private:
    template <typename T>
    T read() {
        T value{};
        m_stream >> value;
        if (m_stream.status() != QDataStream::Ok)
            throw std::runtime_error("Unexpected End of File");
        return value;
    }

    std::unique_ptr<QIODevice> m_device;
    QDataStream m_stream;
};

#endif // PACKAGE_STREAM_H

