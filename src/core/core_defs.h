#pragma once
#include <QString>
#include <cstdint>

namespace CoreDefs {

    enum class FileType : uint32_t {
        Unknown = 0,
        BMF     = 543575362,
        BSB     = 1112752672,
        BSB_BE  = 541217602,   // BSB byte swapped (console)
        BANK    = 1179011410,
        FEV1    = 827737414,
        FEV1_BE = 1178949169,  // FEV1 byte swapped (console)
        FSB     = 1179861557,  // "FSB5" read big endian
        FSB5_LE = 893539142,   // "FSB5" read little endian (PC)
        FSB4    = 876761926,   // "FSB4" read little endian (AFL Live PC music banks)
        FSB4_BE = 1179861556,  // "FSB4" read big endian
        PM3M    = 1295207760,
        M3MP    = 1347236685,
        MINI    = 1229867341,
        MOB     = 557993805,
        PNG     = 2303741511,
        KPXT    = 1263556692,
        TXPK    = 1415073867,
        WiseBNK = 1145588546,
        WiseWEM = 1179011410,
        WAV     = 1380533830
    };

    enum class Game {
        AFLL, RLL2, RLL2WCE, DBC14, RLL3, TTC, CPL16, DBC17, MTBOD,
        AC, RLL4, AOIT, CPL18, C19, AOT2, TWT2, C22, AFL23, C24, TB,
        R25, AFL26, RL26, C26
    };

    enum class BlobsetVersion {
        V1, V2, V3, V4
    };

    enum class Platforms {
        Windows, PS3, Xbox360
    };

    struct FileMappingEntry {
        QString filePath;
        quint64 pathHash;
    };
}
