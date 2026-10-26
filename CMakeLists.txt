cmake_minimum_required(VERSION 3.21)
project(Blobset-Tools VERSION 2.1.10.55 LANGUAGES CXX)

if(WIN32)
    enable_language(RC) #
    set(WIN32_EXE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/windows_assets.rc")
endif()

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Auto-wire Qt Meta-Object Compiler, Resource Compiler, and UI Compiler
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt 6.11.2 and required target modules
find_package(Qt6 6.11.2 COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    QuickDialogs2
    Widgets
    Concurrent
    Xml
    Multimedia
    Quick3D
    REQUIRED)

find_package(OpenMP REQUIRED)

qt_standard_project_setup(REQUIRES 6.11.2)

# Application Executable Mapping - Bundling all C++ Backing Logics & Workers
qt_add_executable(Blobset-Tools
    src/main.cpp
	${WIN32_EXE_ICON}
    
    # Core Structures & Definitions
    src/core/core_defs.h
    src/core/asset_signatures.h
    src/core/package_stream.h
    src/core/utilities.h
    src/core/utilities.cpp
    src/core/blobset_asset_reader.h
    src/core/blobset_asset_reader.cpp
    src/core/blobset_entry_registry.h
    src/core/blobset_entry_registry.cpp
    src/core/creator_support.h
    src/core/creator_support.cpp
    src/core/bsb_file.h
    src/core/bsb_file.cpp
    src/core/bnk_file.h
    src/core/bnk_file.cpp
    src/core/blobset_file.h
    src/core/blobset_file.cpp
    src/core/gcm_texture.h
    src/core/gcm_texture.cpp
    src/core/fsb_file.h
    src/core/fsb_file.cpp
    src/core/mini_txpk_file.h
    src/core/mini_txpk_file.cpp
    src/core/m3mp_file.h
    src/core/m3mp_file.cpp
    src/core/mesh_file.h
    src/core/mesh_file.cpp
    src/core/txpk_file.h
    src/core/txpk_file.cpp
    src/core/x360_texture.h
    src/core/x360_texture.cpp
    src/core/mod_library.h
    src/core/mod_library.cpp
    src/core/command_line.h
    src/core/command_line.cpp
    src/core/app_paths.h
    src/core/app_paths.cpp
	
    # Application Orchestrator
    src/core/app_orchestrator.h
    src/core/app_orchestrator.cpp

    # Binary Data Streams & Compression
    src/compression/compression_io.h
    src/compression/compression_io.cpp
    
    # Audio Conversion Logics
    src/audio/audio_decoder.h
    src/audio/audio_decoder.cpp
    src/audio/audio_player.h
    src/audio/audio_player.cpp
    
    # Graphics Processing Layer
    src/graphics/dds_image_provider.h
    src/graphics/dds_image_provider.cpp
    src/graphics/dds_processor.h
    src/graphics/dds_processor.cpp
    
    # Decoupled Multi-Threaded Workers
    src/workers/create_worker.h
    src/workers/create_worker.cpp
    src/workers/extract_worker.h
    src/workers/extract_worker.cpp
    src/workers/file_mapping_worker.h
    src/workers/file_mapping_worker.cpp
    src/workers/name_resolver_worker.h
    src/workers/name_resolver_worker.cpp
    src/workers/restore_worker.h
    src/workers/restore_worker.cpp
    src/core/hash_utility.h
    src/workers/modify_worker.h
    src/workers/modify_worker.cpp
    
    # Context Logic Bridges & Tool Managers
    src/managers/bnk_tool_manager.h
    src/managers/bnk_tool_manager.cpp
    src/managers/bsb_tool_manager.h
    src/managers/bsb_tool_manager.cpp
    src/managers/dds_viewer_bridge.h
    src/managers/dds_viewer_bridge.cpp
    src/managers/fmod_tool_manager.h
    src/managers/fmod_tool_manager.cpp
    src/managers/m3mp_tool_manager.h
    src/managers/m3mp_tool_manager.cpp
    src/managers/mesh_tool_manager.h
    src/managers/mesh_tool_manager.cpp
    src/managers/mod_manager.h
    src/managers/mod_manager.cpp
    src/managers/txpk_tool_manager.h
    src/managers/txpk_tool_manager.cpp
    
    # High-Performance UI Models
    src/models/xml_models.h
    src/models/xml_models.cpp
    src/models/tree_model.h
    src/models/tree_model.cpp
    src/models/hex_viewer_model.h
    src/models/hex_viewer_model.cpp
    src/models/file_mapping_editor_model.h
    src/models/file_mapping_editor_model.cpp
    src/models/string_table_model.h
    src/models/string_table_model.cpp
)

set_source_files_properties(qml/BtDarkStyle.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# QML Module Mapping for Qt6 - Bundling all Blobset Dark-Themed Views
qt_add_qml_module(Blobset-Tools
    URI "com.wouldymods.blobset"
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    RESOURCES
        assets/blobset_tools.ico
        assets/blobset_tools.png
        assets/blackorchid.png
        assets/ext/alpha_32.png
        assets/ext/alpha_checked_32.png
        assets/ext/bank_32.png
        assets/ext/bmf_32.png
        assets/ext/bnk_32.png
        assets/ext/bsb_32.png
        assets/ext/close_32.png
        assets/ext/col_32.png
        assets/ext/compression_32.png
        assets/ext/create_32.png
        assets/ext/dat_32.png
        assets/ext/dds_32.png
        assets/ext/extract_32.png
        assets/ext/fev1_32.png
        assets/ext/file_32.png
        assets/ext/file_mapping_32.png
        assets/ext/flip_image_32.png
        assets/ext/folder_32.png
        assets/ext/fsb_32.png
        assets/ext/grid_dark.bmp
        assets/ext/grid_light.bmp
        assets/ext/hjo_32.png
        assets/ext/info_32.png
        assets/ext/loading_32.png
        assets/ext/lua_32.png
        assets/ext/m3mp_32.png
        assets/ext/mapping_32.png
        assets/ext/menu_32.png
        assets/ext/mesh_32.png
        assets/ext/modify_32.png
        assets/ext/obj_32.png
        assets/ext/open_32.png
        assets/ext/png_32.png
        assets/ext/reset_32.png
        assets/ext/restore_backup_32.png
        assets/ext/save_32.png
        assets/ext/skip_32.png
        assets/ext/steam_32.png
        assets/ext/txpk_32.png
        assets/ext/txpl_32.png
        assets/ext/str_32.png
        assets/ext/txt_32.png
        assets/ext/wav_32.png
        assets/ext/wem_32.png
        assets/ext/wireframe_32.png
        assets/wouldy_mods.png
        assets/ext/wwise_32.png
    QML_FILES
        qml/main.qml
        qml/BtDarkStyle.qml
        qml/BtToolTip.qml
        qml/views/GameSelection.qml
        qml/views/MainForm.qml
        qml/subview_viewers/BNKViewer.qml
        qml/subview_viewers/BSBViewer.qml
        qml/subview_viewers/BSBCreator.qml
        qml/subview_viewers/DDSViewer.qml
        qml/subview_viewers/FileMappingEditor.qml
        qml/subview_viewers/ModManager.qml
        qml/subview_viewers/FMODViewer.qml
        qml/subview_viewers/HexViewer.qml
        qml/subview_viewers/StringEditor.qml
        qml/subview_viewers/M3MPViewer.qml
        qml/subview_viewers/M3MPCreator.qml
        qml/subview_viewers/MeshViewer.qml
        qml/subview_viewers/TXPKCreator.qml
        qml/subview_viewers/TXPKViewer.qml
        qml/subview_viewers/About.qml
        RESOURCES readme/1.jpg readme/10.jpg readme/11.jpg readme/12.jpg readme/2.jpg readme/3.jpg readme/4.jpg readme/5.jpg readme/6.jpg readme/7.jpg readme/8.jpg readme/9.jpg
        RESOURCES GameSelectionInfo.xml ReadMe.md
        RESOURCES docs/MOD_MANAGER.md
RESOURCES vortex/game-afl23/gameart.png vortex/game-afl26/gameart.png vortex/game-afllive/gameart.png vortex/game-aointernationaltennis/gameart.png vortex/game-aotennis2/gameart.png vortex/game-ashescricket/gameart.png vortex/game-caseypowelllacrosse16/gameart.png vortex/game-caseypowelllacrosse18/gameart.png vortex/game-cricket19/gameart.png vortex/game-cricket22/gameart.png vortex/game-cricket24/gameart.png vortex/game-cricket26/gameart.png vortex/game-donbradmancricket14/gameart.png vortex/game-donbradmancricket17/gameart.png vortex/game-masqueradethebaublesofdoom/gameart.png vortex/game-rugby25/gameart.png vortex/game-rugbyleague26/gameart.png vortex/game-rugbyleaguelive3/gameart.png vortex/game-rugbyleaguelive4/gameart.png vortex/game-tabletopcricket/gameart.png vortex/game-tennisworldtour2/gameart.png vortex/game-tiebreak/gameart.png
QML_FILES vortex/game-afl23/blobset_game.json vortex/game-afl23/index.js vortex/game-afl23/info.json vortex/game-afl26/blobset_game.json vortex/game-afl26/index.js vortex/game-afl26/info.json vortex/game-afllive/blobset_game.json vortex/game-afllive/index.js vortex/game-afllive/info.json vortex/game-aointernationaltennis/blobset_game.json vortex/game-aointernationaltennis/index.js vortex/game-aointernationaltennis/info.json vortex/game-aotennis2/blobset_game.json vortex/game-aotennis2/index.js vortex/game-aotennis2/info.json vortex/game-ashescricket/blobset_game.json vortex/game-ashescricket/index.js vortex/game-ashescricket/info.json vortex/game-caseypowelllacrosse16/blobset_game.json vortex/game-caseypowelllacrosse16/index.js vortex/game-caseypowelllacrosse16/info.json vortex/game-caseypowelllacrosse18/blobset_game.json vortex/game-caseypowelllacrosse18/index.js vortex/game-caseypowelllacrosse18/info.json vortex/game-cricket19/blobset_game.json vortex/game-cricket19/index.js vortex/game-cricket19/info.json vortex/game-cricket22/blobset_game.json vortex/game-cricket22/index.js vortex/game-cricket22/info.json vortex/game-cricket24/blobset_game.json vortex/game-cricket24/index.js vortex/game-cricket24/info.json vortex/game-cricket26/blobset_game.json vortex/game-cricket26/index.js vortex/game-cricket26/info.json vortex/game-donbradmancricket14/blobset_game.json vortex/game-donbradmancricket14/index.js vortex/game-donbradmancricket14/info.json vortex/game-donbradmancricket17/blobset_game.json vortex/game-donbradmancricket17/index.js vortex/game-donbradmancricket17/info.json vortex/game-masqueradethebaublesofdoom/blobset_game.json vortex/game-masqueradethebaublesofdoom/index.js vortex/game-masqueradethebaublesofdoom/info.json vortex/game-rugby25/blobset_game.json vortex/game-rugby25/index.js vortex/game-rugby25/info.json vortex/game-rugbyleague26/blobset_game.json vortex/game-rugbyleague26/index.js vortex/game-rugbyleague26/info.json vortex/game-rugbyleaguelive3/blobset_game.json vortex/game-rugbyleaguelive3/index.js vortex/game-rugbyleaguelive3/info.json vortex/game-rugbyleaguelive4/blobset_game.json vortex/game-rugbyleaguelive4/index.js vortex/game-rugbyleaguelive4/info.json vortex/game-tabletopcricket/blobset_game.json vortex/game-tabletopcricket/index.js vortex/game-tabletopcricket/info.json vortex/game-tennisworldtour2/blobset_game.json vortex/game-tennisworldtour2/index.js vortex/game-tennisworldtour2/info.json vortex/game-tiebreak/blobset_game.json vortex/game-tiebreak/index.js vortex/game-tiebreak/info.json
RESOURCES readme/image.jpg
)

# Target third-party library header includes
target_include_directories(Blobset-Tools PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/audio"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compression"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/managers"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/models"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workers"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/directxtex"
)

# Target third-party library bin directories
set(BT_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
target_link_directories(Blobset-Tools PRIVATE
    "${BT_LIB_DIR}"
)

target_compile_definitions(Blobset-Tools PRIVATE
    HAVE_DIRECTXMATH
)

target_link_libraries(Blobset-Tools PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Xml
    Qt6::Multimedia
    Qt6::Quick3D
    OpenMP::OpenMP_CXX
    liblzma
    libzstd
    vgmstream
    # Codecs vgmstream (a static library) is built with: ATRAC9, FSB CELT (two versions) and MPEG.
    # Full paths: their import libraries use MSVC style names (libxxx.lib) that -l doesn't always find.
    "${BT_LIB_DIR}/libatrac9.lib"
    "${BT_LIB_DIR}/libcelt-0061.lib"
    "${BT_LIB_DIR}/libcelt-0110.lib"
    "${BT_LIB_DIR}/libmpg123-0.lib"
    vorbis
    vorbisfile
    avcodec
    avformat
    avutil
    swresample
    DirectXTex
)

# The Vortex game extensions (vortex/<game>): next to the exe, where --install-vortex-extensions copies them from.
add_custom_command(TARGET Blobset-Tools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/vortex" "$<TARGET_FILE_DIR:Blobset-Tools>/vortex"
    COMMENT "Copying the Vortex extensions next to Blobset-Tools.exe"
    VERBATIM)

# The read-only files the exe reads from its own folder (the writable ones live in Documents\Blobset Tools, see
# src/core/app_paths.h): the game list, the game pictures and defaults (the data folder's starting files).
add_custom_command(TARGET Blobset-Tools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/GameSelectionInfo.xml" "$<TARGET_FILE_DIR:Blobset-Tools>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/games" "$<TARGET_FILE_DIR:Blobset-Tools>/assets/games"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/defaults" "$<TARGET_FILE_DIR:Blobset-Tools>/defaults"
    COMMENT "Copying GameSelectionInfo.xml, assets/games and defaults next to Blobset-Tools.exe"
    VERBATIM)

# Copy the third-party DLLs next to the exe after each build (the exe won't start or play audio without them).
file(GLOB BT_RUNTIME_DLLS "${BT_LIB_DIR}/*.dll")
if(BT_RUNTIME_DLLS)
    add_custom_command(TARGET Blobset-Tools POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BT_RUNTIME_DLLS} "$<TARGET_FILE_DIR:Blobset-Tools>"
        COMMENT "Copying the third-party DLLs from lib next to Blobset-Tools.exe"
        VERBATIM)
endif()

if(WIN32)
    set_target_properties(Blobset-Tools PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Keep the Qt runtime next to the exe complete: once Qt's DLLs sit beside Blobset-Tools.exe, Qt only looks for QML
# modules in the local "qml" folder, so every module the QML imports (QtQuick3D for the Mesh Viewer, ...) has to be
# deployed there too. windeployqt scans our qml folder for the imports and copies what's missing after each build.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Blobset-Tools POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    --no-translations
                    "$<TARGET_FILE:Blobset-Tools>"
            COMMENT "Deploying the Qt runtime and QML modules next to Blobset-Tools.exe"
            VERBATIM)
    else()
        message(WARNING "windeployqt not found: the QML modules next to Blobset-Tools.exe won't be updated.")
    endif()
endif()
