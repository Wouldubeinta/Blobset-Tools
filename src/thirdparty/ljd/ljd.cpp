// Ported from luajit-decompiler-v2 by marsinator358 (https://github.com/marsinator358/luajit-decompiler-v2) for
// Blobset Tools: decompiles from memory to a string, without the Windows API. See ljd.h.

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
	return data.size() >= 5 && data.compare(0, 3, "\x1BLJ") == 0;
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
