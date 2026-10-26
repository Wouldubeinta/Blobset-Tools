#ifndef FSB_FILE_H
#define FSB_FILE_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/// Reader for FMOD sample banks:
///   - FMOD Studio banks (.bank): RIFF "FEV " container; the samples (if any) are an FSB5 inside the "SND " chunk.
///     Master / strings banks have no "SND " chunk: they parse fine and simply have no samples.
///   - FSB5 (FMOD Studio), FSB4 (FMOD Ex) and FSB3 (FMOD 3) sound banks (.fsb).
///
/// Only the headers are parsed (names, sizes, rates, loops); decoding is left to vgmstream, which is handed
/// the FSB itself (fsbOffset / fsbSize) - one vgmstream subsong per sample, in the same order.
class FsbFile {
public:
    enum class Container { None, Fsb3, Fsb4, Fsb5, StudioBank };

    struct Sample {
        QString name;
        qint64 dataOffset = -1;   // Relative to the start of the FSB; -1 when the format doesn't tell.
        quint32 dataSize = 0;
        quint32 sampleCount = 0;
        quint32 sampleRate = 0;
        quint32 channels = 0;
        bool hasLoop = false;
        quint32 loopStart = 0;
        quint32 loopEnd = 0;
    };

    bool parse(const QByteArray& data, QString* error = nullptr);

    bool hasFsb() const { return fsbOffset >= 0; }

    Container container = Container::None;
    QString formatName;         // e.g. "FMOD Studio bank (FSB5 version 1)"
    QString note;               // Why a bank has no (readable) samples; empty otherwise.
    QStringList chunks;         // Studio banks: chunk tags found in the bank, e.g. "FMT", "LIST(PROJ)", "SND".
    bool truncated = false;     // Studio banks: a chunk runs past its parent / the end of the file.
    quint32 fsbVersion = 0;
    quint32 bankVersion = 0;    // Studio banks: first value of the FMT chunk.
    quint32 headerFlags = 0;    // FSB3 / FSB4 header mode flags.
    QString codecName;          // FSB5: one codec for the whole bank.
    qint64 fsbOffset = -1;      // Where the FSB starts inside the parsed file; -1 = there is none.
    qint64 fsbSize = 0;
    quint32 sampleDataSize = 0;
    QList<Sample> samples;

private:
    struct BankScan {
        qint64 sndBody = -1;
        quint32 sndSize = 0;
    };

    bool parseStudioBank(const QByteArray& data);
    void walkBankChunks(const QByteArray& data, qint64 pos, qint64 end, int depth, BankScan& scan);
    bool parseFsb5(const QByteArray& data, qint64 base, qint64 limit, QString* error);
    bool parseFsb34(const QByteArray& data, qint64 base, int fsbMajor, QString* error);
};

#endif // FSB_FILE_H
