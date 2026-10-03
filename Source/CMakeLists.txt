cmake_minimum_required(VERSION 3.16)

# Source/ is also a supported standalone configure directory. When the
# package root is used, its top-level CMakeLists.txt owns the project() call.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(DaedalusX64 LANGUAGES C CXX ASM)
    option(CTR_RELEASE "Build the Nintendo 3DS (CTR) version" ON)
    if(NOT CTR_RELEASE)
        message(FATAL_ERROR "This source package supports only the CTR build; configure with -DCTR_RELEASE=ON.")
    endif()
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DAED_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(DAED_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(BASE_FILES StdAfx.cpp)
set(CONFIG_FILES Config/ConfigOptions.cpp)
set(CORE_FILES
    Core/RE2Task.cpp Core/RDRam.cpp Core/Cheats.cpp Core/CPU.cpp Core/DMA.cpp
    Core/Dynamo.cpp Core/FlashMem.cpp Core/Interpret.cpp Core/Interrupts.cpp
    Core/JpegTask.cpp Core/Memory.cpp Core/PIF.cpp Core/R4300.cpp Core/ROM.cpp
    Core/ROMBuffer.cpp Core/ROMImage.cpp Core/RomSettings.cpp Core/RSP_HLE.cpp
    Core/Save.cpp Core/SaveState.cpp Core/TLB.cpp)
set(DEBUG_FILES Debug/DebugConsoleImpl.cpp Debug/DebugLog.cpp Debug/Dump.cpp)
set(DYNAREC_FILES
    DynaRec/BranchType.cpp DynaRec/DynaRecProfile.cpp DynaRec/Fragment.cpp
    DynaRec/FragmentCache.cpp DynaRec/IndirectExitMap.cpp DynaRec/StaticAnalysis.cpp
    DynaRec/TraceRecorder.cpp)
set(GRAPHICS_FILES Graphics/ColourValue.cpp Graphics/PngUtil.cpp Graphics/TextureTransform.cpp)
set(HLEAUDIO_FILES
    HLEAudio/AudioHLEProcessor.cpp HLEAudio/ABI1.cpp HLEAudio/ABI2.cpp HLEAudio/ABI3.cpp
    HLEAudio/ABI3mp3.cpp HLEAudio/AudioBuffer.cpp HLEAudio/HLEMain.cpp
    HLEAudio/ABI_ADPCM.cpp HLEAudio/ABI_Buffers.cpp HLEAudio/ABI_Filters.cpp
    HLEAudio/ABI_MixerInterleave.cpp HLEAudio/ENV_Mixer.cpp HLEAudio/ABI_Resample.cpp)
set(HLEGRAPHICS_FILES
    HLEGraphics/BaseRenderer.cpp HLEGraphics/BaseRenderer.h HLEGraphics/CachedTexture.cpp
    HLEGraphics/ConvertImage.cpp HLEGraphics/ConvertTile.cpp HLEGraphics/DLDebug.cpp
    HLEGraphics/DLParser.cpp HLEGraphics/Microcode.cpp HLEGraphics/RDPStateManager.cpp
    HLEGraphics/TextureCache.cpp HLEGraphics/TextureInfo.cpp HLEGraphics/uCodes/Ucode.cpp)
set(INTERFACE_FILES Interface/RomDB.cpp)
set(MATH_FILES Math/Matrix4x4.cpp)
set(OSHLE_FILES OSHLE/OS.cpp OSHLE/patch.cpp)
set(PLUGIN_FILES Plugins/GraphicsPlugin.cpp)
set(SYSTEM_FILES System/Paths.cpp System/System.cpp)
set(TEST_FILES Test/BatchTest.cpp)
set(UTILITY_FILES
    Utility/CRC.cpp Utility/DataSink.cpp Utility/FastMemcpy.cpp
    Utility/FramerateLimiter.cpp Utility/Hash.cpp Utility/IniFile.cpp
    Utility/MemoryHeap.cpp Utility/Preferences.cpp Utility/PrintOpCode.cpp
    Utility/Profiler.cpp Utility/ROMFile.cpp Utility/ROMFileCache.cpp
    Utility/ROMFileCompressed.cpp Utility/ROMFileMemory.cpp
    Utility/ROMFileUncompressed.cpp Utility/Stream.cpp Utility/StringUtil.cpp
    Utility/Synchroniser.cpp Utility/Timer.cpp Utility/Translate.cpp Utility/ZLibWrapper.cpp)
set(DEBUG_ONLY Core/Registers.cpp)
set(COMMON_BUILD
    ${BASE_FILES} ${CONFIG_FILES} ${CORE_FILES} ${DEBUG_FILES} ${DYNAREC_FILES}
    ${GRAPHICS_FILES} ${HLEAUDIO_FILES} ${HLEGRAPHICS_FILES} ${INTERFACE_FILES}
    ${MATH_FILES} ${OSHLE_FILES} ${PLUGIN_FILES} ${SYSTEM_FILES} ${TEST_FILES}
    ${UTILITY_FILES})

set(CTR_MAIN_FILES SysCTR/main.cpp)
set(CTR_DEBUG_FILES SysCTR/Debug/DaedalusAssertCTR.cpp SysCTR/Debug/DBGConsoleCTR.cpp)
set(CTR_DYNAREC_FILES
    SysCTR/DynaRec/arm/AssemblyUtilsARM.cpp SysCTR/DynaRec/arm/AssemblyWriterARM.cpp
    SysCTR/DynaRec/arm/CodeBufferManagerARM.cpp
    SysCTR/DynaRec/arm/CodeGeneratorARM.cpp SysCTR/DynaRec/arm/N64RegisterCacheARM.cpp)
set(CTR_GRAPHICS_FILES SysCTR/Graphics/GraphicsContextCTR.cpp SysCTR/Graphics/NativeTextureCTR.cpp)
set(CTR_HLEGRAPHICS_FILES
    SysCTR/HLEGraphics/GraphicsPluginCTR.cpp SysCTR/HLEGraphics/RendererCTR.cpp
    SysCTR/HLEGraphics/ColourAdjuster.cpp SysCTR/HLEGraphics/BlendModes.cpp
    SysCTR/HLEGraphics/Combiner/BlendConstant.cpp
    SysCTR/HLEGraphics/Combiner/CombinerExpression.cpp
    SysCTR/HLEGraphics/Combiner/CombinerTree.cpp
    SysCTR/HLEGraphics/Combiner/RenderSettings.cpp)
set(CTR_HLEAUDIO_FILES SysCTR/HLEAudio/AudioPluginCTR.cpp SysCTR/HLEAudio/AudioOutput.cpp)
set(CTR_INPUTMANAGER_FILES SysCTR/Input/InputManagerCTR.cpp SysCTR/Input/CTRMultiplayer.cpp)
set(CTR_UI_FILES SysCTR/UI/UserInterface.cpp SysCTR/UI/RomSelector.cpp SysCTR/UI/InGameMenu.cpp)
set(CTR_UTILITY_FILES
    SysCTR/Utility/CTRStorage.cpp SysCTR/Utility/ThreadCTR.cpp
    SysCTR/Utility/IOCTR.cpp SysCTR/Utility/TimingCTR.cpp
    SysCTR/Utility/MemoryCTR.c SysCTR/Utility/MemoryCTR.cpp)
set(CTR_ASM_FILES
    SysCTR/DynaRec/arm/DynarecStubARM.S
    SysCTR/Utility/CacheCTR.S)
set(CTR_BUILD
    ${CTR_DEBUG_FILES} ${CTR_DYNAREC_FILES} ${CTR_GRAPHICS_FILES}
    ${CTR_HLEAUDIO_FILES} ${CTR_HLEGRAPHICS_FILES} ${CTR_INPUTMANAGER_FILES}
    ${CTR_UI_FILES} ${CTR_UTILITY_FILES})

set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -L${DEVKITPRO}/libctru/lib -L${DEVKITPRO}/picaGL/lib -L${DEVKITPRO}/portlibs/3ds/lib -specs=3dsx.specs -Wl,--gc-sections -Wl,-z,noexecstack -Wl,--no-warn-execstack")

add_definitions(-DDAEDALUS_CTR -D__3DS__)
include_directories(BEFORE "${DAED_PROJECT_ROOT}/../picaGL/include")
include_directories(
    "${DAED_SOURCE_DIR}"
    "${DAED_SOURCE_DIR}/SysCTR/Include"
    "${DAED_SOURCE_DIR}/Config/Dev"
    "${DEVKITPRO}/libctru/include"
    "${DEVKITPRO}/picaGL/include"
    "${DEVKITPRO}/portlibs/3ds/include")

set(DAEDALUS_GAME_ID_VALUE 0)
set(DAEDALUS_GAME_ID_HEX "")
if(DEFINED DAEDALUS_GAME_ID AND NOT "${DAEDALUS_GAME_ID}" STREQUAL "")
    string(TOUPPER "${DAEDALUS_GAME_ID}" DAEDALUS_GAME_ID_HEX)
    string(REGEX REPLACE "^0X" "" DAEDALUS_GAME_ID_HEX "${DAEDALUS_GAME_ID_HEX}")
    string(LENGTH "${DAEDALUS_GAME_ID_HEX}" DAEDALUS_GAME_ID_LENGTH)
    if(NOT DAEDALUS_GAME_ID_HEX MATCHES "^[0-9A-F]+$" OR DAEDALUS_GAME_ID_LENGTH GREATER 4)
        message(FATAL_ERROR "DAEDALUS_GAME_ID must be a 16-bit hexadecimal CartID, e.g. 514D")
    endif()
    math(EXPR DAEDALUS_GAME_ID_VALUE "0x${DAEDALUS_GAME_ID_HEX}")
    while(DAEDALUS_GAME_ID_LENGTH LESS 4)
        set(DAEDALUS_GAME_ID_HEX "0${DAEDALUS_GAME_ID_HEX}")
        math(EXPR DAEDALUS_GAME_ID_LENGTH "${DAEDALUS_GAME_ID_LENGTH} + 1")
    endwhile()
    message(STATUS "Building CTR game profile for CartID 0x${DAEDALUS_GAME_ID_HEX}")
endif()

set(DAEDALUS_GAME_PROFILE "${DAED_SOURCE_DIR}/Core/GameProfiles/${DAEDALUS_GAME_ID_HEX}.h")
add_library(daedalus STATIC ${COMMON_BUILD} ${DEBUG_ONLY} ${CTR_BUILD})
target_compile_definitions(daedalus PRIVATE DAEDALUS_TARGET_GAME_ID=${DAEDALUS_GAME_ID_VALUE})
if(DAEDALUS_GAME_ID_VALUE AND EXISTS "${DAEDALUS_GAME_PROFILE}")
    target_compile_definitions(daedalus PRIVATE
        "DAEDALUS_GAME_PROFILE_HEADER=\"Core/GameProfiles/${DAEDALUS_GAME_ID_HEX}.h\"")
endif()
target_link_libraries(daedalus PRIVATE z png)

# Keep architecture assembly stubs as direct executable objects, not archive
# members. This guarantees the ARM dynarec/cache entry points reach the final link.
add_executable(${PROJECT_NAME}.elf
    ${CTR_MAIN_FILES} ${CTR_INPUTMANAGER_FILES} ${CTR_ASM_FILES})
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DAEDALUS_TARGET_GAME_ID=${DAEDALUS_GAME_ID_VALUE})
if(DAEDALUS_GAME_ID_VALUE AND EXISTS "${DAEDALUS_GAME_PROFILE}")
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
        "DAEDALUS_GAME_PROFILE_HEADER=\"Core/GameProfiles/${DAEDALUS_GAME_ID_HEX}.h\"")
endif()
target_link_libraries(${PROJECT_NAME}.elf PRIVATE daedalus -limgui -lpicaGL -lctru -lm)

set(APP_SMDH "${DAED_SOURCE_DIR}/SysCTR/Resources/daedalus.smdh")
set(APP_BANNER "${DAED_SOURCE_DIR}/SysCTR/Resources/banner.bnr")
set(APP_ROMFS_DIR "${DAED_SOURCE_DIR}/SysCTR/Resources/romfs")
set(APP_ROMFS_BIN "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin")
set(APP_RSF "${DAED_SOURCE_DIR}/SysCTR/Resources/template2.rsf")
set(CTR_TOOL_DIR "${DAED_PROJECT_ROOT}/Tools")

add_custom_command(
    OUTPUT "${APP_ROMFS_BIN}"
    COMMAND "${CTR_TOOL_DIR}/3dstool.exe" -cvtf romfs "${APP_ROMFS_BIN}"
            --romfs-dir "${APP_ROMFS_DIR}"
    DEPENDS ${APP_ROMFS_DIR}
    VERBATIM)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.3dsx"
    COMMAND "${CTR_TOOL_DIR}/3dsxtool.exe" "$<TARGET_FILE:${PROJECT_NAME}.elf>"
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.3dsx"
            "--romfs=${APP_ROMFS_DIR}" "--smdh=${APP_SMDH}"
    DEPENDS ${PROJECT_NAME}.elf
    VERBATIM)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.cia"
    COMMAND "${CTR_TOOL_DIR}/makerom.exe" -f cia -target t -exefslogo
            -o "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.cia"
            -elf "$<TARGET_FILE:${PROJECT_NAME}.elf>"
            -rsf "${APP_RSF}" -banner "${APP_BANNER}"
            -icon "${APP_SMDH}" -romfs "${APP_ROMFS_BIN}"
    DEPENDS ${PROJECT_NAME}.elf "${APP_ROMFS_BIN}"
    VERBATIM)
add_custom_target(N3DS ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.3dsx"
            "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.cia")
