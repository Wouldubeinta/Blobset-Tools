# Usage: python ljd_port.py <luajit-decompiler-v2 folder> <src/thirdparty/ljd> (see src/thirdparty/ljd/README.txt)
# Ports luajit-decompiler-v2 (Visual Studio / Windows) to portable C++20 that decompiles from memory to a string.
import re, os, sys
SRC = sys.argv[1]; DST = sys.argv[2]
os.makedirs(DST, exist_ok=True)
def rd(p): return open(os.path.join(SRC, p), encoding='utf-8-sig').read().replace('\r\n', '\n')
def wr(p, s):
    os.makedirs(os.path.dirname(os.path.join(DST, p)) or DST, exist_ok=True)
    open(os.path.join(DST, p), 'w', newline='\n').write(s)
def sub1(s, old, new):
    assert s.count(old) == 1, (old[:60], s.count(old)); return s.replace(old, new)

HEADER_NOTE = "// Ported from luajit-decompiler-v2 by marsinator358 (https://github.com/marsinator358/luajit-decompiler-v2) for\n// Blobset Tools: decompiles from memory to a string, without the Windows API. See ljd.h.\n"

# The shared header (was main.h).
internal = HEADER_NOTE + '''
#ifndef LJD_INTERNAL_H
#define LJD_INTERNAL_H

#if !defined(__CHAR_UNSIGNED__) && !defined(_CHAR_UNSIGNED)
#error The decompiler needs char to be unsigned (-funsigned-char / /J).
#endif

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#define DEBUG_INFO __FUNCTION__, __FILE__, __LINE__

namespace ljd {

constexpr char PROGRAM_NAME[] = "LuaJIT Decompiler v2";
constexpr uint64_t DOUBLE_SIGN = 0x8000000000000000;
constexpr uint64_t DOUBLE_EXPONENT = 0x7FF0000000000000;
constexpr uint64_t DOUBLE_FRACTION = 0x000FFFFFFFFFFFFF;
constexpr uint64_t DOUBLE_SPECIAL = DOUBLE_EXPONENT;
constexpr uint64_t DOUBLE_NEGATIVE_ZERO = DOUBLE_SIGN;

struct Error {
	const std::string message;
	const std::string filePath;
	const std::string function;
	const std::string source;
	const std::string line;
};

inline void print_progress_bar(const double& = 0, const double& = 100) {}
inline void erase_progress_bar() {}
void ljd_assert(const bool& assertion, const std::string& message, const std::string& filePath, const std::string& function, const std::string& source, const uint32_t& line);
std::string byte_to_string(const uint8_t& byte);

class Bytecode;
class Ast;
class Lua;

#include "bytecode/bytecode.h"
#include "ast/ast.h"
#include "lua/lua.h"

} // namespace ljd

#endif // LJD_INTERNAL_H
'''
wr('ljd_internal.h', internal)

def fix_common(s):
    s = re.sub(r'(?<![\w.])assert\(', 'ljd_assert(', s)
    return s

# Headers: the backslash includes, the file handles.
for h in ['ast/ast.h', 'ast/building_blocks.h', 'ast/conditionBuilder.h', 'ast/function.h', 'bytecode/constants.h',
          'bytecode/instructions.h', 'bytecode/prototype.h', 'lua/lua.h', 'bytecode/bytecode.h']:
    s = fix_common(rd(h)).replace('\\\\', '/')
    s = re.sub(r'#include "([^"]*)\\\\([^"]*)"', r'#include "\1/\2"', s)
    if h in ('bytecode/prototype.h', 'bytecode/constants.h', 'bytecode/instructions.h', 'ast/building_blocks.h',
             'ast/function.h', 'ast/conditionBuilder.h'):
        # Included inside the class: MSVC allows "struct Ast::Local {" there, GCC / Clang want "struct Local {".
        s = re.sub(r'^(\s*)(struct|class) (?:Ast|Bytecode)::(\w+)', r'\1\2 \3', s, flags=re.M)
    if h == 'ast/ast.h':
        s = sub1(s, '#include "conditionBuilder.h";', '#include "conditionBuilder.h"')
        # Declared private, defined in the public part (MSVC accepts that).
        s = sub1(s, '\tstruct Local;\n\tstruct SlotScope;\n\tstruct ConditionBuilder;\n\npublic:\n',
                 '\tstruct ConditionBuilder;\n\npublic:\n\tstruct Local;\n\tstruct SlotScope;\n')
    if h == 'bytecode/bytecode.h':
        s = sub1(s, 'Bytecode(const std::string& filePath);', 'Bytecode(const std::string& filePath, const std::string& data);')
        s = sub1(s, '\tHANDLE file = INVALID_HANDLE_VALUE;\n', '\tconst std::string& data;\n\tuint64_t readPosition = 0;\n')
    if h == 'lua/lua.h':
        s = sub1(s, '\tHANDLE file = INVALID_HANDLE_VALUE;\n', '')
        s = sub1(s, '\tconst std::string filePath;\n', '\tconst std::string filePath;\n\tstd::string output;   // The Lua source (UTF-8, no BOM).\n')
    wr(h, HEADER_NOTE + s if not h.endswith(('instructions.h','constants.h','prototype.h','building_blocks.h','conditionBuilder.h','function.h')) else s)

def wrap_cpp(s):
    s = fix_common(s)
    s = sub1(s, '#include "..\\main.h"\n', '#include "../ljd_internal.h"\n\nnamespace ljd {\n')
    return HEADER_NOTE + s.rstrip('\n') + '\n\n} // namespace ljd\n'

# bytecode.cpp: read from memory.
s = rd('bytecode/bytecode.cpp')
s = sub1(s, 'Bytecode::Bytecode(const std::string& filePath) : filePath(filePath) {}',
         'Bytecode::Bytecode(const std::string& filePath, const std::string& data) : filePath(filePath), data(data) {}')
s = re.sub(r'void Bytecode::open_file\(\) \{.*?\n\}\n', '''void Bytecode::open_file() {
	readPosition = 0;
	fileSize = data.size();
	assert(fileSize >= MIN_FILE_SIZE, "File is too small or empty", filePath, DEBUG_INFO);
	bytesUnread = fileSize;
}
''', s, count=1, flags=re.S)
s = re.sub(r'void Bytecode::close_file\(\) \{.*?\n\}\n', 'void Bytecode::close_file() {}\n', s, count=1, flags=re.S)
s = sub1(s, '''	DWORD bytesRead = 0;
	assert(ReadFile(file, fileBuffer.data(), byteCount, &bytesRead, NULL) && !(byteCount - bytesRead), "Failed to read file", filePath, DEBUG_INFO);
''', '''	std::memcpy(fileBuffer.data(), data.data() + readPosition, byteCount);
	readPosition += byteCount;
''')
wr('bytecode/bytecode.cpp', wrap_cpp(s))

wr('bytecode/prototype.cpp', wrap_cpp(rd('bytecode/prototype.cpp')))
wr('ast/ast.cpp', wrap_cpp(rd('ast/ast.cpp')))

# lua.cpp: write to a string.
s = rd('lua/lua.cpp')
s = sub1(s, 'Lua::~Lua() {\n\tclose_file();\n}', 'Lua::~Lua() {}')
s = re.sub(r'void Lua::create_file\(\) \{.*?\n\}\n', 'void Lua::create_file() {}\n', s, count=1, flags=re.S)
s = re.sub(r'void Lua::close_file\(\) \{.*?\n\}\n', 'void Lua::close_file() {}\n', s, count=1, flags=re.S)
s = sub1(s, '''	DWORD charsWritten = 0;
	assert(WriteFile(file, writeBuffer.data(), writeBuffer.size(), &charsWritten, NULL) && !(writeBuffer.size() - charsWritten), "Failed writing to file", filePath, DEBUG_INFO);
	writeBuffer.clear();
''', '''	output = std::move(writeBuffer);
	if (output.compare(0, sizeof(UTF8_BOM) - 1, UTF8_BOM) == 0) output.erase(0, sizeof(UTF8_BOM) - 1);
	writeBuffer.clear();
''')
wr('lua/lua.cpp', wrap_cpp(s))

# The entry point (was main.cpp).
wr('ljd.cpp', HEADER_NOTE + '''
#include "ljd.h"
#include "ljd_internal.h"

namespace ljd {

void ljd_assert(const bool& assertion, const std::string& message, const std::string& filePath, const std::string& function, const std::string& source, const uint32_t& line) {
	if (!assertion) throw Error{
		.message = message,
		.filePath = filePath,
		.function = function,
		.source = source,
		.line = std::to_string(line)
	};
}

std::string byte_to_string(const uint8_t& byte) {
	char string[] = "0x00";
	uint8_t digit;

	for (uint8_t i = 2; i--;) {
		digit = (byte >> i * 4) & 0xF;
		string[3 - i] = digit >= 0xA ? 'A' + digit - 0xA : '0' + digit;
	}

	return string;
}

bool isBytecode(const std::string& data) {
	return data.size() >= 5 && data.compare(0, 3, "\\x1BLJ") == 0;
}

Result decompile(const std::string& bytecodeData, const std::string& name, const Options& options) {
	Result result;
	try {
		Bytecode bytecode(name, bytecodeData);
		Ast ast(bytecode, options.ignoreDebugInfo, options.minimizeDiffs);
		Lua lua(bytecode, ast, name, true, options.minimizeDiffs, options.unrestrictedAscii);
		bytecode();
		ast();
		lua();
		result.version = bytecode.header.version;
		result.flags = bytecode.header.flags;
		result.source = std::move(lua.output);
		result.ok = true;
	} catch (const Error& error) {
		result.error = error.message + " (" + error.function + ", " + error.source + ":" + error.line + ")";
	} catch (const std::exception& error) {
		result.error = std::string("Unexpected error: ") + error.what();
	} catch (...) {
		result.error = "Unexpected error";
	}
	return result;
}

} // namespace ljd
''')
wr('ljd.h', HEADER_NOTE + '''
#ifndef LJD_H
#define LJD_H

#include <cstdint>
#include <string>

// LuaJIT bytecode ("\\x1BLJ", versions 1 and 2, little endian) to Lua source. Runs deeply recursive code: call it
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
''')
print("ported")
