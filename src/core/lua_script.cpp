#include "lua_script.h"

#include "thirdparty/ljd/ljd.h"

// LuaJIT's headers (include/luajit, from the LuaJIT-2.1 build: tools/luajit).
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <QRegularExpression>
#include <QThread>

#include <memory>

using namespace Qt::StringLiterals;

namespace LuaScript {
namespace {

// luajit-decompiler-v2 links with a 256 MB stack: its AST passes recurse once per nested block / expression.
constexpr uint kDecompilerStack = 256u * 1024u * 1024u;

// Runs fn on a thread with a big stack and waits for it.
template <typename Fn>
void runWithBigStack(Fn&& fn) {
    std::unique_ptr<QThread> thread(QThread::create(std::forward<Fn>(fn)));
    thread->setObjectName(u"LuaJIT decompiler"_s);
    thread->setStackSize(kDecompilerStack);
    thread->start();
    thread->wait();
}

struct BuiltHeader {
    quint8 version = 0;
    quint8 flags = 0;
};

// What this LuaJIT writes, from a dump of an empty function.
const BuiltHeader& builtHeader() {
    static const BuiltHeader header = [] {
        const Compiled empty = compile(QString(), u"header"_s, kFlagStrip);
        BuiltHeader h;
        if (empty.ok && empty.bytecode.size() >= 5) {
            h.version = static_cast<quint8>(empty.bytecode.at(3));
            h.flags = static_cast<quint8>(empty.bytecode.at(4));
        }
        return h;
    }();
    return header;
}

struct LuaStateDeleter {
    void operator()(lua_State* state) const { lua_close(state); }
};

} // namespace

bool isBytecode(const QByteArray& data) {
    return data.size() >= 5 && data.startsWith("\x1BLJ");
}

quint8 builtVersion() {
    return builtHeader().version;
}

quint8 builtFlags() {
    return builtHeader().flags;
}

Decompiled decompile(const QByteArray& data, const QString& name) {
    Decompiled out;
    if (!isBytecode(data)) {
        // Lua source already (a few games keep their scripts as text).
        out.ok = true;
        out.source = QString::fromUtf8(data);
        if (out.source.startsWith(QChar(0xFEFF)))
            out.source.remove(0, 1);
        out.source.replace(u"\r\n"_s, u"\n"_s);
        return out;
    }

    out.bytecode = true;
    out.version = static_cast<quint8>(data.at(3));
    out.flags = static_cast<quint8>(data.at(4));
    if (out.flags & kFlagBigEndian) {
        out.error = u"This script is big endian LuaJIT bytecode (a console build), which the decompiler can't read."_s;
        return out;
    }

    ljd::Result result;
    const std::string bytes(data.constData(), static_cast<size_t>(data.size()));
    const std::string label = name.toStdString();
    runWithBigStack([&result, &bytes, &label]() { result = ljd::decompile(bytes, label); });

    if (!result.ok) {
        out.error = u"The decompiler couldn't turn this script back into Lua: %1"_s.arg(QString::fromStdString(result.error));
        return out;
    }
    out.ok = true;
    out.source = QString::fromUtf8(result.source.data(), static_cast<qsizetype>(result.source.size()));
    out.source.replace(u"\r\n"_s, u"\n"_s);
    return out;
}

Compiled compile(const QString& source, const QString& name, quint8 flags) {
    Compiled out;
    std::unique_ptr<lua_State, LuaStateDeleter> state(luaL_newstate());
    if (!state) {
        out.error = u"LuaJIT couldn't start (out of memory)."_s;
        return out;
    }
    lua_State* L = state.get();
    luaL_openlibs(L);

    const QByteArray text = source.toUtf8();
    const QByteArray chunkName = "=" + (name.isEmpty() ? QByteArray("script") : name.toUtf8());
    if (luaL_loadbuffer(L, text.constData(), static_cast<size_t>(text.size()), chunkName.constData()) != 0) {
        // "<name>:<line>: <message>"
        const QString message = QString::fromUtf8(lua_tostring(L, -1));
        static const QRegularExpression lineAt(u"^.*?:(\\d+): (.*)$"_s, QRegularExpression::DotMatchesEverythingOption);
        const QRegularExpressionMatch match = lineAt.match(message);
        if (match.hasMatch()) {
            out.line = match.captured(1).toInt();
            out.error = u"Line %1: %2"_s.arg(out.line).arg(match.captured(2));
        } else {
            out.error = message;
        }
        return out;
    }

    // string.dump(chunk, mode): the bytecode file the game loads. "d" (deterministic) sorts the table constants,
    // which are otherwise in hash order and differ from run to run: the same script always gives the same file.
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "dump");
    lua_pushvalue(L, -3);
    lua_pushstring(L, (flags & kFlagStrip) ? "sd" : "d");
    if (lua_pcall(L, 2, 1, 0) != 0) {
        out.error = u"LuaJIT couldn't write the bytecode: %1"_s.arg(QString::fromUtf8(lua_tostring(L, -1)));
        return out;
    }
    size_t size = 0;
    const char* bytes = lua_tolstring(L, -1, &size);
    out.bytecode = QByteArray(bytes, static_cast<qsizetype>(size));
    out.ok = isBytecode(out.bytecode);
    if (!out.ok)
        out.error = u"LuaJIT wrote no bytecode."_s;
    return out;
}

QString checkWritable(quint8 version, quint8 flags) {
    const BuiltHeader& built = builtHeader();
    if (built.version == 0)
        return u"LuaJIT isn't working in this build."_s;
    if (flags & kFlagBigEndian)
        return u"It's big endian bytecode (a console build): only little endian (PC) scripts can be saved."_s;
    if (version != built.version)
        return u"It's LuaJIT %1 bytecode (version %2): Blobset Tools writes LuaJIT 2.1 bytecode (version %3)."_s
            .arg(version == 1 ? u"2.0"_s : u"?"_s).arg(version).arg(built.version);
    if ((flags & kFlagFr2) != (built.flags & kFlagFr2))
        return (flags & kFlagFr2) ? u"It's 64 bit (FR2) bytecode, and this LuaJIT writes 32 bit bytecode."_s
                                  : u"It's 32 bit bytecode (no FR2 flag): Blobset Tools writes 64 bit (FR2) bytecode."_s;
    return {};
}

} // namespace LuaScript
