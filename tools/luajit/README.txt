LuaJIT 2.1 for Blobset Tools (Qt 6.11.2 MinGW 64-bit)

Blobset Tools links LuaJIT as a static library: lib/libluajit.a with its headers in include/luajit. It compiles the
Lua scripts edited in the M3MP viewer back into the LuaJIT bytecode the games use.

To build it:
  1. Get LuaJIT 2.1 (https://github.com/LuaJIT/LuaJIT, branch v2.1: a git clone, or the release archive).
  2. Copy this CMakeLists.txt into the LuaJIT folder (next to its Makefile and src folder).
  3. Open that CMakeLists.txt in Qt Creator with the Desktop Qt 6.11.2 MinGW 64-bit kit and build Release.
  4. libluajit.a and the headers are copied into the Blobset Tools folder (BLOBSET_TOOLS_DIR, set in the CMake
     configuration - it defaults to C:/Users/would/OneDrive/Documents/Qt/Project/Blobset-Tools when that exists),
     and into <build>/dist.

LUAJIT_BUILD_DLL / LUAJIT_BUILD_EXE (CMake options) also build lua51.dll and luajit.exe.
The defaults are kept (x64 GC64): the bytecode has the FR2 flag (header 1B 4C 4A 02 0A), like the games' scripts.
LuaJIT is MIT licensed (COPYRIGHT is copied into include/luajit).
