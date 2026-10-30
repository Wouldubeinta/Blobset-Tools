luajit-decompiler-v2 by marsinator358 (MIT, see LICENSE): https://github.com/marsinator358/luajit-decompiler-v2

Ported for Blobset Tools by tools/ljd_port.py: it reads the bytecode from memory and returns the Lua source as a
string (no Windows API, files or console), builds with GCC / Clang / MSVC (char must be unsigned:
-funsigned-char / /J, set in CMakeLists.txt), and lives in namespace ljd. Entry point: ljd.h.
The decompiler logic itself is unchanged. To update: run tools/ljd_port.py on a new copy of the original source.
