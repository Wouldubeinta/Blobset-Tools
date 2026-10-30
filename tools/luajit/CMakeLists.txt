# LuaJIT 2.1 as a static library (libluajit.a) for Blobset Tools, built with Qt's MinGW 64-bit kit.
#
# Open this file in Qt Creator (Qt 6.11.2 MinGW 64-bit kit) and build Release. CMake only: LuaJIT's Makefile
# and mingw32-make aren't used. The steps are the same as src/Makefile / src/msvcbuild.bat:
#
#   1. minilua      host Lua, runs DynASM and genversion.lua
#   2. DynASM       src/vm_x64.dasc -> buildvm_arch.h
#   3. luajit.h     src/luajit_rolling.h + .relver (or git) -> luajit.h
#   4. buildvm      host tool -> lj_vm.obj (the interpreter, peobj) + lj_bcdef/ffdef/libdef/recdef/folddef.h
#   5. libluajit.a  the LuaJIT core and standard libraries
#
# The output goes to <build>/dist (lib/libluajit.a, include/luajit/*.h) and, when BLOBSET_TOOLS_DIR is set (it
# defaults to the Blobset Tools project folder when that exists), into its lib and include/luajit folders.
#
# Defaults are kept: on x64 that's GC64, so the bytecode carries the FR2 flag like the BigAnt games' scripts.
# LUAJIT_BUILD_DLL / LUAJIT_BUILD_EXE also build lua51.dll and luajit.exe (with jit/*.lua for luajit -b).

cmake_minimum_required(VERSION 3.21)
project(LuaJIT LANGUAGES C)

option(LUAJIT_BUILD_DLL "Also build lua51.dll" OFF)
option(LUAJIT_BUILD_EXE "Also build luajit.exe (links lua51.dll)" OFF)
set(BLOBSET_TOOLS_DEFAULT "C:/Users/would/OneDrive/Documents/Qt/Project/Blobset-Tools")
if(EXISTS "${BLOBSET_TOOLS_DEFAULT}/CMakeLists.txt")
    set(BLOBSET_TOOLS_DIR "${BLOBSET_TOOLS_DEFAULT}" CACHE PATH "Blobset Tools project folder: libluajit.a and the headers are copied into its lib / include/luajit")
else()
    set(BLOBSET_TOOLS_DIR "" CACHE PATH "Blobset Tools project folder: libluajit.a and the headers are copied into its lib / include/luajit")
endif()
if(LUAJIT_BUILD_EXE)
    set(LUAJIT_BUILD_DLL ON)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Blobset Tools is built with Qt's MinGW (GCC): a library from another compiler won't link with it (MSVC's objects
# want its runtime: __security_cookie, MSVCRT). In Qt Creator pick the "Desktop Qt 6.11.2 MinGW 64-bit" kit.
if(NOT (MINGW AND CMAKE_C_COMPILER_ID STREQUAL "GNU") AND WIN32)
    message(FATAL_ERROR "LuaJIT for Blobset Tools has to be built with Qt's MinGW 64-bit kit (GCC), "
                        "not ${CMAKE_C_COMPILER_ID} (${CMAKE_C_COMPILER}). In Qt Creator: Projects > add the "
                        "\"Desktop Qt 6.11.2 MinGW 64-bit\" kit, remove the other kit, and build again.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64|x64)$")
    message(FATAL_ERROR "This project builds LuaJIT for x64 (the Qt MinGW 64-bit kit). Compiler: ${CMAKE_C_COMPILER}")
endif()

set(LJ_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(LJ_DASM "${CMAKE_CURRENT_SOURCE_DIR}/dynasm")
set(LJ_GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${LJ_GEN}" "${LJ_GEN}/jit")

# Compiler flags (src/Makefile: CCOPT, CCWARN, TARGET_XCFLAGS).
set(LJ_CFLAGS -O2 -fomit-frame-pointer -Wall -U_FORTIFY_SOURCE)
set(LJ_DEFINES)
if(NOT WIN32)
    # GCC / Clang on ELF write .eh_frame unwind tables (the Makefile's TARGET_TESTUNWIND); Windows x64 uses SEH.
    list(APPEND LJ_DEFINES LUAJIT_UNWIND_EXTERNAL)
endif()

# ---- 1. minilua ------------------------------------------------------------------------------------------------
add_executable(minilua "${LJ_SRC}/host/minilua.c")
target_compile_options(minilua PRIVATE -O2 -w)
if(NOT WIN32)
    target_link_libraries(minilua PRIVATE m)
endif()

# ---- 2. DynASM: buildvm_arch.h -----------------------------------------------------------------------------------
# x64 with GC64 (FR2) uses vm_x64.dasc. Flags as msvcbuild.bat / the Makefile give them for x64.
set(LJ_DASM_FLAGS -D JIT -D FFI -D ENDIAN_LE -D FPU -D P64)
if(WIN32)
    list(PREPEND LJ_DASM_FLAGS -D WIN)
else()
    list(APPEND LJ_DASM_FLAGS -D HFABI)
endif()
add_custom_command(OUTPUT "${LJ_GEN}/buildvm_arch.h"
    COMMAND minilua "${LJ_DASM}/dynasm.lua" ${LJ_DASM_FLAGS} -o "${LJ_GEN}/buildvm_arch.h" vm_x64.dasc
    DEPENDS minilua "${LJ_SRC}/vm_x64.dasc" "${LJ_SRC}/lj_arch.h" "${LJ_SRC}/lua.h" "${LJ_SRC}/luaconf.h"
    WORKING_DIRECTORY "${LJ_SRC}"
    COMMENT "DynASM vm_x64.dasc"
    VERBATIM)

# ---- 3. luajit.h (the release version: git's commit time, else .relver from the release archive) ----------------
set(LJ_RELVER "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" show -s --format=%ct
                        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE LJ_RELVER
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
endif()
if(NOT LJ_RELVER MATCHES "^[0-9]+$" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.relver")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.relver" LJ_RELVER)
    string(STRIP "${LJ_RELVER}" LJ_RELVER)
endif()
if(NOT LJ_RELVER MATCHES "^[0-9]+$")
    message(WARNING "LuaJIT's release version is unknown (no git, and .relver isn't a number): luajit.h says ROLLING.")
    set(LJ_RELVER "")
endif()
file(WRITE "${LJ_GEN}/luajit_relver.txt.in" "${LJ_RELVER}\n")
configure_file("${LJ_GEN}/luajit_relver.txt.in" "${LJ_GEN}/luajit_relver.txt" COPYONLY)
add_custom_command(OUTPUT "${LJ_GEN}/luajit.h"
    COMMAND minilua host/genversion.lua luajit_rolling.h "${LJ_GEN}/luajit_relver.txt" "${LJ_GEN}/luajit.h"
    DEPENDS minilua "${LJ_SRC}/luajit_rolling.h" "${LJ_SRC}/host/genversion.lua" "${LJ_GEN}/luajit_relver.txt"
    WORKING_DIRECTORY "${LJ_SRC}"
    COMMENT "Generating luajit.h (2.1.${LJ_RELVER})"
    VERBATIM)

# ---- 4. buildvm and what it generates ------------------------------------------------------------------------------
add_executable(buildvm
    "${LJ_SRC}/host/buildvm.c" "${LJ_SRC}/host/buildvm_asm.c" "${LJ_SRC}/host/buildvm_peobj.c"
    "${LJ_SRC}/host/buildvm_lib.c" "${LJ_SRC}/host/buildvm_fold.c"
    "${LJ_GEN}/buildvm_arch.h" "${LJ_GEN}/luajit.h")
target_include_directories(buildvm PRIVATE "${LJ_GEN}" "${LJ_SRC}" "${LJ_DASM}")
target_compile_options(buildvm PRIVATE -O2 -w)
target_compile_definitions(buildvm PRIVATE ${LJ_DEFINES})

set(LJ_LIB_C lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c
             lib_jit.c lib_ffi.c lib_buffer.c)
set(LJ_LIB_C_FULL)
foreach(f IN LISTS LJ_LIB_C)
    list(APPEND LJ_LIB_C_FULL "${LJ_SRC}/${f}")
endforeach()

# The interpreter: a COFF object on Windows (buildvm writes it directly), assembler source elsewhere.
if(WIN32)
    set(LJ_VM "${LJ_GEN}/lj_vm.obj")
    set(LJ_VM_MODE peobj)
else()
    enable_language(ASM)
    set(LJ_VM "${LJ_GEN}/lj_vm.S")
    set(LJ_VM_MODE elfasm)
endif()
add_custom_command(OUTPUT "${LJ_VM}"
    COMMAND buildvm -m ${LJ_VM_MODE} -o "${LJ_VM}"
    DEPENDS buildvm WORKING_DIRECTORY "${LJ_SRC}" COMMENT "buildvm ${LJ_VM_MODE}" VERBATIM)

set(LJ_GEN_HEADERS)
foreach(mode bcdef ffdef libdef recdef)
    add_custom_command(OUTPUT "${LJ_GEN}/lj_${mode}.h"
        COMMAND buildvm -m ${mode} -o "${LJ_GEN}/lj_${mode}.h" ${LJ_LIB_C}
        DEPENDS buildvm ${LJ_LIB_C_FULL} WORKING_DIRECTORY "${LJ_SRC}" COMMENT "buildvm ${mode}" VERBATIM)
    list(APPEND LJ_GEN_HEADERS "${LJ_GEN}/lj_${mode}.h")
endforeach()
add_custom_command(OUTPUT "${LJ_GEN}/lj_folddef.h"
    COMMAND buildvm -m folddef -o "${LJ_GEN}/lj_folddef.h" lj_opt_fold.c
    DEPENDS buildvm "${LJ_SRC}/lj_opt_fold.c" WORKING_DIRECTORY "${LJ_SRC}" COMMENT "buildvm folddef" VERBATIM)
add_custom_command(OUTPUT "${LJ_GEN}/jit/vmdef.lua"
    COMMAND buildvm -m vmdef -o "${LJ_GEN}/jit/vmdef.lua" ${LJ_LIB_C}
    DEPENDS buildvm ${LJ_LIB_C_FULL} WORKING_DIRECTORY "${LJ_SRC}" COMMENT "buildvm vmdef" VERBATIM)
list(APPEND LJ_GEN_HEADERS "${LJ_GEN}/lj_folddef.h" "${LJ_GEN}/luajit.h")

# ---- 5. The library ----------------------------------------------------------------------------------------------
set(LJ_CORE_C lj_assert.c lj_gc.c lj_err.c lj_char.c lj_bc.c lj_obj.c lj_buf.c lj_str.c lj_tab.c lj_func.c
    lj_udata.c lj_meta.c lj_debug.c lj_prng.c lj_state.c lj_dispatch.c lj_vmevent.c lj_vmmath.c lj_strscan.c
    lj_strfmt.c lj_strfmt_num.c lj_serialize.c lj_api.c lj_profile.c lj_lex.c lj_parse.c lj_bcread.c lj_bcwrite.c
    lj_load.c lj_ir.c lj_opt_mem.c lj_opt_fold.c lj_opt_narrow.c lj_opt_dce.c lj_opt_loop.c lj_opt_split.c
    lj_opt_sink.c lj_mcode.c lj_snap.c lj_record.c lj_crecord.c lj_ffrecord.c lj_asm.c lj_trace.c lj_gdbjit.c
    lj_ctype.c lj_cdata.c lj_cconv.c lj_ccall.c lj_ccallback.c lj_carith.c lj_clib.c lj_cparse.c lj_lib.c
    lj_alloc.c lib_aux.c ${LJ_LIB_C} lib_init.c)
set(LJ_CORE_SOURCES)
foreach(f IN LISTS LJ_CORE_C)
    list(APPEND LJ_CORE_SOURCES "${LJ_SRC}/${f}")
endforeach()
if(WIN32)
    set_source_files_properties("${LJ_VM}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
endif()

function(luajit_core target type)
    add_library(${target} ${type} ${LJ_CORE_SOURCES} "${LJ_VM}" ${LJ_GEN_HEADERS})
    target_include_directories(${target} PRIVATE "${LJ_GEN}" "${LJ_SRC}")
    target_compile_options(${target} PRIVATE ${LJ_CFLAGS})
    target_compile_definitions(${target} PRIVATE ${LJ_DEFINES})
    if(NOT WIN32)
        target_link_libraries(${target} PUBLIC m ${CMAKE_DL_LIBS})
    endif()
endfunction()

luajit_core(libluajit STATIC)
set_target_properties(libluajit PROPERTIES OUTPUT_NAME luajit PREFIX "lib" SUFFIX ".a")

if(LUAJIT_BUILD_DLL)
    luajit_core(lua51 SHARED)
    if(WIN32)
        target_compile_definitions(lua51 PRIVATE LUA_BUILD_AS_DLL)   # __declspec(dllexport)
    endif()
    set_target_properties(lua51 PROPERTIES PREFIX "" OUTPUT_NAME lua51)
endif()
if(LUAJIT_BUILD_EXE)
    add_executable(luajit "${LJ_SRC}/luajit.c" "${LJ_GEN}/luajit.h" "${LJ_GEN}/jit/vmdef.lua")
    target_include_directories(luajit PRIVATE "${LJ_GEN}" "${LJ_SRC}")
    target_compile_options(luajit PRIVATE ${LJ_CFLAGS})
    if(WIN32)
        target_compile_definitions(luajit PRIVATE LUA_BUILD_AS_DLL)   # __declspec(dllimport)
    endif()
    target_link_libraries(luajit PRIVATE lua51)
    # luajit -b / -jdump need the jit modules next to the exe.
    add_custom_command(TARGET luajit POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${LJ_SRC}/jit" "$<TARGET_FILE_DIR:luajit>/jit"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LJ_GEN}/jit/vmdef.lua" "$<TARGET_FILE_DIR:luajit>/jit"
        VERBATIM)
endif()

# ---- Output: dist/ and the Blobset Tools project ------------------------------------------------------------------
set(LJ_PUBLIC_HEADERS "${LJ_SRC}/lua.h" "${LJ_SRC}/luaconf.h" "${LJ_SRC}/lualib.h" "${LJ_SRC}/lauxlib.h"
    "${LJ_SRC}/lua.hpp" "${LJ_GEN}/luajit.h")
set(LJ_DIST "${CMAKE_CURRENT_BINARY_DIR}/dist")
set(LJ_COPY_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LJ_DIST}/lib" "${LJ_DIST}/include/luajit"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libluajit>" "${LJ_DIST}/lib"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LJ_PUBLIC_HEADERS} "${CMAKE_CURRENT_SOURCE_DIR}/COPYRIGHT" "${LJ_DIST}/include/luajit")
if(BLOBSET_TOOLS_DIR)
    list(APPEND LJ_COPY_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BLOBSET_TOOLS_DIR}/lib" "${BLOBSET_TOOLS_DIR}/include/luajit"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libluajit>" "${BLOBSET_TOOLS_DIR}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LJ_PUBLIC_HEADERS} "${CMAKE_CURRENT_SOURCE_DIR}/COPYRIGHT" "${BLOBSET_TOOLS_DIR}/include/luajit")
    message(STATUS "libluajit.a and the headers will be copied into ${BLOBSET_TOOLS_DIR}/lib and include/luajit")
endif()
add_custom_command(TARGET libluajit POST_BUILD ${LJ_COPY_COMMANDS}
    COMMENT "Copying libluajit.a and the LuaJIT headers to dist (and Blobset Tools)" VERBATIM)

install(TARGETS libluajit ARCHIVE DESTINATION lib)
install(FILES ${LJ_PUBLIC_HEADERS} DESTINATION include/luajit)
if(LUAJIT_BUILD_DLL)
    install(TARGETS lua51 RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
endif()
if(LUAJIT_BUILD_EXE)
    install(TARGETS luajit RUNTIME DESTINATION bin)
endif()
