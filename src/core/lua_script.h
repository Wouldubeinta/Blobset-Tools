#ifndef LUA_SCRIPT_H
#define LUA_SCRIPT_H

#include <QByteArray>
#include <QString>

// LuaJIT scripts in the games' M3MPs: bytecode to readable Lua (luajit-decompiler-v2, src/thirdparty/ljd) and Lua
// back to bytecode (LuaJIT 2.1: lib/libluajit.a + include/luajit, built with tools/luajit/CMakeLists.txt).
//
// The games' scripts are LuaJIT 2.1 bytecode: "\x1BLJ", version 2, flags 0x0A (stripped, FR2 = 64 bit frames).
// compile() writes the same format; checkWritable() says when a file's format can't be written back (LuaJIT 2.0
// bytecode, 32 bit frames, big endian).
namespace LuaScript {

constexpr quint8 kFlagBigEndian = 0x01;
constexpr quint8 kFlagStrip = 0x02;
constexpr quint8 kFlagFfi = 0x04;
constexpr quint8 kFlagFr2 = 0x08;

bool isBytecode(const QByteArray& data);

struct Decompiled {
    bool ok = false;
    bool bytecode = false;   // False: the file was Lua source already (shown and saved as it is).
    QString source;          // '\n' line ends.
    QString error;
    quint8 version = 0;      // Bytecode header (when bytecode).
    quint8 flags = 0;
};

// Blocking (seconds for the biggest scripts): call it from a worker thread. It runs the decompiler on a thread of
// its own with a big stack, as the decompiler recurses deeply.
Decompiled decompile(const QByteArray& data, const QString& name);

struct Compiled {
    bool ok = false;
    QByteArray bytecode;
    QString error;
    int line = 0;            // The error's line (1 based), 0 when unknown.
};

// Lua source to bytecode with the given header flags' strip setting. Thread safe (a Lua state of its own).
Compiled compile(const QString& source, const QString& name, quint8 flags);

// Empty when compile() writes a file the game can load in place of one with this header, else why not.
QString checkWritable(quint8 version, quint8 flags);

// The bytecode version and FR2 flag this build of LuaJIT writes.
quint8 builtVersion();
quint8 builtFlags();

} // namespace LuaScript

#endif // LUA_SCRIPT_H
