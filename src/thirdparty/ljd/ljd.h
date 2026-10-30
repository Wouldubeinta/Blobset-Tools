// Ported from luajit-decompiler-v2 by marsinator358 (https://github.com/marsinator358/luajit-decompiler-v2) for
// Blobset Tools: decompiles from memory to a string, without the Windows API. See ljd.h.

#ifndef LJD_H
#define LJD_H

#include <cstdint>
#include <string>

// LuaJIT bytecode ("\x1BLJ", versions 1 and 2, little endian) to Lua source. Runs deeply recursive code: call it
// on a thread with a big stack (the original links with a 256 MB stack).
namespace ljd {

struct Options {
	bool ignoreDebugInfo = false;
	bool minimizeDiffs = false;
	bool unrestrictedAscii = false;
};

struct Result {
	bool ok = false;
	std::string source;    // UTF-8 Lua source, CRLF line ends.
	std::string error;
	uint8_t version = 0;   // The bytecode's version (1 = LuaJIT 2.0, 2 = LuaJIT 2.1) and flags.
	uint8_t flags = 0;
};

bool isBytecode(const std::string& data);
Result decompile(const std::string& bytecode, const std::string& name, const Options& options = {});

} // namespace ljd

#endif // LJD_H
