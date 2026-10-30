// Ported from luajit-decompiler-v2 by marsinator358 (https://github.com/marsinator358/luajit-decompiler-v2) for
// Blobset Tools: decompiles from memory to a string, without the Windows API. See ljd.h.

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
