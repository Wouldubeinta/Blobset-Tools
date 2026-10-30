// Ported from luajit-decompiler-v2 by marsinator358 (https://github.com/marsinator358/luajit-decompiler-v2) for
// Blobset Tools: decompiles from memory to a string, without the Windows API. See ljd.h.
class Bytecode {
public:

	class Prototype;
	struct Constant;
	struct NumberConstant;
	struct TableConstant;
	struct TableNode;
	struct VariableInfo;
	struct Instruction;
	#include "prototype.h"
	#include "constants.h"
	#include "instructions.h"

	Bytecode(const std::string& filePath, const std::string& data);
	~Bytecode();

	void operator()();

	const std::string filePath;

	struct {
		uint8_t version = 0;
		uint8_t flags = 0;
		std::string chunkname;
	} header;

	const Prototype* main = nullptr;
	uint64_t prototypesTotalSize = 0;

private:

	static constexpr uint8_t MIN_PROTO_SIZE = 11;
	static constexpr uint8_t MIN_FILE_SIZE = MIN_PROTO_SIZE + 7;

	void read_header();
	void read_prototypes();
	void open_file();
	void close_file();
	void read_file(const uint32_t& byteCount);
	uint32_t read_uleb128();
	bool buffer_next_block();

	const std::string& data;
	uint64_t readPosition = 0;
	uint64_t fileSize = 0;
	uint64_t bytesUnread = 0;
	std::vector<uint8_t> fileBuffer;
	std::vector<Prototype*> prototypes;
};
