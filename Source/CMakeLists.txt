 #CMake File for DaedalusX64

#Build instructions
# Create a directory in the DaedalusX64 root directory and cd into it 'eg: mkdir tmp ~/DaedalusX64'
# ensure you have cmake installed
# For PSP:
# cmake -DCMAKE_TOOLCHAIN_FILE=../Tools/psptoolchain.cmake ../Source/ -DPSP_OPTION

#Options
# PSP_RELEASE - Builds PSP Release

cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 14)
project (${PROJECT_NAME})

option(DAEDALUS_ENABLE_FUNCTION_PROFILING "Enable CTR function profiling" OFF)
option(DAEDALUS_ENABLE_SDMC_DIAGNOSTICS "Enable SD-card logging and profiler snapshots" OFF)
option(DAEDALUS_ENABLE_DEBUG_CONSOLE "Enable the CTR debug console" OFF)
option(DAEDALUS_ENABLE_BATCH_TESTS "Compile emulator batch-test support" OFF)
if(DAEDALUS_ENABLE_SDMC_DIAGNOSTICS)
	set(DAEDALUS_ENABLE_FUNCTION_PROFILING ON)
endif()

enable_language(ASM)

#Default Files for build
set (BASE_FILES StdAfx.cpp)
set (CONFIG_FILES Config/ConfigOptions.cpp)
set (CORE_FILES Core/RE2Task.cpp Core/RDRam.cpp Core/Cheats.cpp Core/CPU.cpp Core/DMA.cpp Core/Dynamo.cpp Core/FlashMem.cpp Core/Interpret.cpp Core/Interrupts.cpp Core/JpegTask.cpp Core/Memory.cpp Core/PIF.cpp Core/R4300.cpp Core/ROM.cpp Core/ROMBuffer.cpp Core/ROMImage.cpp Core/RomSettings.cpp Core/RSP_HLE.cpp Core/Save.cpp Core/SaveState.cpp Core/TLB.cpp)
# Diagnostic implementations are injected only when their feature is enabled.
set (DEBUG_FILES Debug/Dump.cpp)
if(DAEDALUS_ENABLE_SDMC_DIAGNOSTICS)
	list(APPEND DEBUG_FILES Debug/DebugLog.cpp)
endif()
if(DAEDALUS_ENABLE_DEBUG_CONSOLE)
	list(APPEND DEBUG_FILES Debug/DebugConsoleImpl.cpp)
endif()
set (DYNAREC_FILES DynaRec/BranchType.cpp DynaRec/DynaRecProfile.cpp DynaRec/Fragment.cpp DynaRec/FragmentCache.cpp DynaRec/IndirectExitMap.cpp DynaRec/StaticAnalysis.cpp DynaRec/TraceRecorder.cpp)
set (GRAPHICS_FILES Graphics/ColourValue.cpp Graphics/PngUtil.cpp Graphics/TextureTransform.cpp)
set (HLEAUDIO_FILES HLEAudio/AudioHLEProcessor.cpp HLEAudio/ABI1.cpp HLEAudio/ABI2.cpp HLEAudio/ABI3.cpp HLEAudio/ABI3mp3.cpp HLEAudio/AudioBuffer.cpp HLEAudio/HLEMain.cpp HLEAudio/ABI_ADPCM.cpp HLEAudio/ABI_Buffers.cpp HLEAudio/ABI_Filters.cpp HLEAudio/ABI_MixerInterleave.cpp HLEAudio/ENV_Mixer.cpp HLEAudio/ABI_Resample.cpp)
set (HLEGRAPHICS_FILES HLEGraphics/BaseRenderer.cpp HLEGraphics/BaseRenderer.h HLEGraphics/CachedTexture.cpp HLEGraphics/ConvertImage.cpp HLEGraphics/ConvertTile.cpp HLEGraphics/DLDebug.cpp HLEGraphics/DLParser.cpp HLEGraphics/Microcode.cpp HLEGraphics/RDPStateManager.cpp HLEGraphics/TextureCache.cpp HLEGraphics/TextureInfo.cpp HLEGraphics/uCodes/Ucode.cpp)
set (INTERFACE_FILES Interface/RomDB.cpp)
set (MATH_FILES Math/Matrix4x4.cpp)
set (OSHLE_FILES OSHLE/OS.cpp OSHLE/patch.cpp)
set (PLUGIN_FILES Plugins/GraphicsPlugin.cpp)
set (SYSTEM_FILES System/Paths.cpp System/System.cpp)
set (TEST_FILES)
if(DAEDALUS_ENABLE_BATCH_TESTS)
	list(APPEND TEST_FILES Test/BatchTest.cpp)
endif()
set (UTILITY_FILES Utility/CRC.cpp Utility/DataSink.cpp Utility/FastMemcpy.cpp  Utility/FramerateLimiter.cpp Utility/Hash.cpp Utility/IniFile.cpp Utility/MemoryHeap.cpp Utility/Preferences.cpp Utility/PrintOpCode.cpp Utility/ROMFile.cpp Utility/ROMFileCache.cpp Utility/ROMFileCompressed.cpp Utility/ROMFileLZ4.cpp Utility/ROMFileMemory.cpp Utility/ROMFileUncompressed.cpp Utility/LZ4Block.c Utility/Stream.cpp Utility/StringUtil.cpp Utility/Synchroniser.cpp Utility/Timer.cpp  Utility/Translate.cpp Utility/ZLibWrapper.cpp)
if(DAEDALUS_ENABLE_FUNCTION_PROFILING)
	list(APPEND UTILITY_FILES Utility/Profiler.cpp)
endif()
set (UNKNOWN_FILES Utility/FastMemcpy_test.cpp)
set (DEBUG_ONLY Core/Registers.cpp)
set (BUILD ${BASE_FILES} ${CONFIG_FILES} ${CORE_FILES} ${DEBUG_FILES} ${DYNAREC_FILES} ${GRAPHICS_FILES} ${HLEAUDIO_FILES} ${HLEGRAPHICS_FILES} ${INTERFACE_FILES} ${MATH_FILES} ${OSHLE_FILES} ${PLUGIN_FILES} ${SYSTEM_FILES} ${TEST_FILES} ${UTILITY_FILES})


# N3DS(CTR)
set (CTR_MAIN_FILES SysCTR/main.cpp)
set (CTR_DEBUG_FILES SysCTR/Debug/DaedalusAssertCTR.cpp)
if(DAEDALUS_ENABLE_DEBUG_CONSOLE)
	list(APPEND CTR_DEBUG_FILES SysCTR/Debug/DBGConsoleCTR.cpp)
endif()
set (CTR_DYNAREC_FILES SysCTR/DynaRec/arm/AssemblyUtilsARM.cpp SysCTR/DynaRec/arm/AssemblyWriterARM.cpp SysCTR/DynaRec/arm/CodeBufferManagerARM.cpp SysCTR/DynaRec/arm/DynarecStubARM.S SysCTR/DynaRec/arm/CodeGeneratorARM.cpp SysCTR/DynaRec/arm/N64RegisterCacheARM.cpp)
set (CTR_GRAPHICS_FILES SysCTR/Graphics/GraphicsContextCTR.cpp SysCTR/Graphics/NativeTextureCTR.cpp)
set (CTR_HLEGRAPHICS_FILES SysCTR/HLEGraphics/GraphicsPluginCTR.cpp SysCTR/HLEGraphics/RendererCTR.cpp SysCTR/HLEGraphics/ColourAdjuster.cpp SysCTR/HLEGraphics/BlendModes.cpp SysCTR/HLEGraphics/Combiner/BlendConstant.cpp SysCTR/HLEGraphics/Combiner/CombinerExpression.cpp SysCTR/HLEGraphics/Combiner/CombinerTree.cpp SysCTR/HLEGraphics/Combiner/RenderSettings.cpp)
set (CTR_HLEAUDIO_FILES SysCTR/HLEAudio/AudioPluginCTR.cpp SysCTR/HLEAudio/AudioOutput.cpp)
set (CTR_INPUTMANAGER_FILES SysCTR/Input/InputManagerCTR.cpp SysCTR/Input/CTRMultiplayer.cpp)
set (CTR_UI_FILES SysCTR/UI/UserInterface.cpp SysCTR/UI/RomSelector.cpp SysCTR/UI/InGameMenu.cpp)
set (CTR_UTILITY_FILES SysCTR/Utility/CTRPerfLearning.cpp SysCTR/Utility/CTRStorage.cpp SysCTR/Utility/ThreadCTR.cpp SysCTR/Utility/IOCTR.cpp SysCTR/Utility/TimingCTR.cpp SysCTR/Utility/MemoryCTR.cpp SysCTR/Utility/CacheCTR.S SysCTR/Utility/FastMemcpyCTR.S)
set (CTR_BUILD ${CTR_DEBUG_FILES} ${CTR_DYNAREC_FILES} ${CTR_GRAPHICS_FILES} ${CTR_HLEAUDIO_FILES} ${CTR_HLEGRAPHICS_FILES} ${CTR_INPUTMANAGER_FILES} ${CTR_UI_FILES} ${CTR_UTILITY_FILES})


if (CTR_RELEASE)
	message("N3DS Release Build..")

	set(CMAKE_EXE_LINKER_FLAGS "-L${DEVKITPRO}/libctru/lib -L${DEVKITPRO}/picaGL/lib -L${DEVKITPRO}/portlibs/3ds/lib -specs=3dsx.specs -Wl,--gc-sections -Wl,-z,noexecstack -Wl,--no-warn-execstack")

	# Prefer this checkout's picaGL API header so Daedalus and the linked
	# picaGL library agree on the stereo extensions used by Preferences.cpp.
	if(EXISTS "${PROJECT_SOURCE_DIR}/../deps/picaGL/include/GL/picaGL.h")
		include_directories(BEFORE ${PROJECT_SOURCE_DIR}/../deps/picaGL/include)
	elseif(EXISTS "${PROJECT_SOURCE_DIR}/../../picaGL/include/GL/picaGL.h")
		include_directories(BEFORE ${PROJECT_SOURCE_DIR}/../../picaGL/include)
	endif()
	include_directories(${DEVKITPRO}/libctru/include ${DEVKITPRO}/picaGL/include ${DEVKITPRO}/portlibs/3ds/include)

	add_definitions("-DDAEDALUS_CTR" "-D__3DS__")

	# Build Daedalus Lib
	add_library(daedalus STATIC
		${BUILD}
		${DEBUG_ONLY}
		${CTR_BUILD}
	)
	# Keep optional instrumentation completely out of normal release builds.
	target_compile_options(daedalus PRIVATE -O2 -ffunction-sections -fdata-sections)
	if(DAEDALUS_ENABLE_FUNCTION_PROFILING)
		target_compile_definitions(daedalus PRIVATE DAEDALUS_ENABLE_PROFILING)
	endif()
	if(DAEDALUS_ENABLE_SDMC_DIAGNOSTICS)
		target_compile_definitions(daedalus PRIVATE DAEDALUS_LOG DAEDALUS_ENABLE_SDMC_DIAGNOSTICS)
	endif()
	if(DAEDALUS_ENABLE_DEBUG_CONSOLE)
		target_compile_definitions(daedalus PRIVATE DAEDALUS_DEBUG_CONSOLE)
	endif()
	if(DAEDALUS_ENABLE_BATCH_TESTS)
		target_compile_definitions(daedalus PRIVATE DAEDALUS_BATCH_TEST_ENABLED)
	endif()

	# A CartID is optional: zero keeps the standard multi-ROM build. For a
	# dedicated profile, the build script can detect the ID from an N64 ROM.
	set(DAEDALUS_GAME_ID_VALUE 0)
	if(DEFINED DAEDALUS_GAME_ID AND NOT "${DAEDALUS_GAME_ID}" STREQUAL "")
		string(TOUPPER "${DAEDALUS_GAME_ID}" DAEDALUS_GAME_ID_HEX)
		string(REGEX REPLACE "^0X" "" DAEDALUS_GAME_ID_HEX "${DAEDALUS_GAME_ID_HEX}")
		string(LENGTH "${DAEDALUS_GAME_ID_HEX}" DAEDALUS_GAME_ID_LENGTH)
		if(NOT DAEDALUS_GAME_ID_HEX MATCHES "^[0-9A-F]+$" OR DAEDALUS_GAME_ID_LENGTH GREATER 4)
			message(FATAL_ERROR "DAEDALUS_GAME_ID must be a 16-bit hexadecimal CartID (for example 514D)")
		endif()
		math(EXPR DAEDALUS_GAME_ID_VALUE "0x${DAEDALUS_GAME_ID_HEX}")
		while(DAEDALUS_GAME_ID_LENGTH LESS 4)
			set(DAEDALUS_GAME_ID_HEX "0${DAEDALUS_GAME_ID_HEX}")
			math(EXPR DAEDALUS_GAME_ID_LENGTH "${DAEDALUS_GAME_ID_LENGTH} + 1")
		endwhile()
		message(STATUS "Building compile-time game profile for CartID 0x${DAEDALUS_GAME_ID_HEX}")
	endif()

	target_compile_definitions(daedalus PRIVATE DAEDALUS_TARGET_GAME_ID=${DAEDALUS_GAME_ID_VALUE})
	set(DAEDALUS_GAME_PROFILE "${PROJECT_SOURCE_DIR}/Core/GameProfiles/${DAEDALUS_GAME_ID_HEX}.h")
	if(DAEDALUS_GAME_ID_VALUE AND EXISTS "${DAEDALUS_GAME_PROFILE}")
		target_compile_definitions(daedalus PRIVATE
			"DAEDALUS_GAME_PROFILE_HEADER=\"Core/GameProfiles/${DAEDALUS_GAME_ID_HEX}.h\"")
		message(STATUS "Using game profile header ${DAEDALUS_GAME_PROFILE}")
	endif()

	target_include_directories(daedalus PRIVATE
		${PROJECT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/SysCTR/Include
		${PROJECT_SOURCE_DIR}/Config/Release
	)

	target_link_libraries(daedalus
		z
		png
	)

	# Build and Link Executable
	add_executable(${PROJECT_NAME}.elf
		${CTR_MAIN_FILES}
		${CTR_INPUTMANAGER_FILES}
	)
	target_compile_options(${PROJECT_NAME}.elf PRIVATE -O2 -ffunction-sections -fdata-sections)
	if(DAEDALUS_ENABLE_FUNCTION_PROFILING)
		target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DAEDALUS_ENABLE_PROFILING)
	endif()
	if(DAEDALUS_ENABLE_SDMC_DIAGNOSTICS)
		target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DAEDALUS_LOG DAEDALUS_ENABLE_SDMC_DIAGNOSTICS)
	endif()
	if(DAEDALUS_ENABLE_DEBUG_CONSOLE)
		target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DAEDALUS_DEBUG_CONSOLE)
	endif()
	if(DAEDALUS_ENABLE_BATCH_TESTS)
		target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DAEDALUS_BATCH_TEST_ENABLED)
	endif()
	target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DAEDALUS_TARGET_GAME_ID=${DAEDALUS_GAME_ID_VALUE})
	if(DAEDALUS_GAME_ID_VALUE AND EXISTS "${DAEDALUS_GAME_PROFILE}")
		target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
			"DAEDALUS_GAME_PROFILE_HEADER=\"Core/GameProfiles/${DAEDALUS_GAME_ID_HEX}.h\"")
	endif()

	target_include_directories(${PROJECT_NAME}.elf PRIVATE
		${PROJECT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/SysCTR/Include
		${PROJECT_SOURCE_DIR}/Config/Release
	)

	target_link_libraries(${PROJECT_NAME}.elf
		daedalus
		-limgui
		-lpicaGL
		-lctru
		-lm
	)

	set(APP_TITLE "${PROJECT_NAME}")
	set(APP_DESCRIPTION "${PROJECT_NAME} port for 3DS")
	set(APP_AUTHOR "MasterFeizz")

	if(EXISTS "${PROJECT_NAME}.smdh")
		set(APP_SMDH "${PROJECT_NAME}.smdh")
	else()
		set(APP_SMDH "${PROJECT_SOURCE_DIR}/SysCTR/Resources/daedalus.smdh")
	endif()

	if(EXISTS "${PROJECT_NAME}.bnr")
		set(APP_BANNER "${PROJECT_NAME}.bnr")
	else()
		set(APP_BANNER "${PROJECT_SOURCE_DIR}/SysCTR/Resources/banner.bnr")
	endif()
	set(APP_AUDIO "${PROJECT_NAME}.wav")
	set(APP_RSF "${PROJECT_NAME}.rsf")
	set(APP_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
	set(APP_ROMFS_BIN "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin")
	set(APP_ROMFS_SOURCE_DIR "${PROJECT_SOURCE_DIR}/SysCTR/Resources/romfs")
	set(APP_ROMFS_STAGE_STAMP "${CMAKE_CURRENT_BINARY_DIR}/romfs.stage.stamp")
	set(APP_ROMFS_PACKER "${PROJECT_SOURCE_DIR}/../Tools/3dstool.exe")
	set(APP_3DSX_PACKER "${PROJECT_SOURCE_DIR}/../Tools/3dsxtool.exe")
	set(APP_MAKEROM "${PROJECT_SOURCE_DIR}/../Tools/makerom.exe")

	# Keep the staged ROMFS up to date, while preserving per-game files that
	# build scripts/users may already have placed in the build's romfs folder.
	file(GLOB_RECURSE APP_ROMFS_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false
		"${APP_ROMFS_SOURCE_DIR}/*")
	set(APP_ROMFS_DATA_INI "${PROJECT_SOURCE_DIR}/../Data/roms.ini")
	if(EXISTS "${APP_ROMFS_DATA_INI}")
		list(APPEND APP_ROMFS_FILES "${APP_ROMFS_DATA_INI}")
	endif()
	set(APP_ROMFS_STAGE_COMMANDS
		COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_ROMFS_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_ROMFS_SOURCE_DIR}" "${APP_ROMFS_DIR}"
	)
	if(EXISTS "${APP_ROMFS_DATA_INI}")
		list(APPEND APP_ROMFS_STAGE_COMMANDS
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${APP_ROMFS_DATA_INI}" "${APP_ROMFS_DIR}/roms.ini")
	endif()
	add_custom_command(
		OUTPUT "${APP_ROMFS_STAGE_STAMP}"
		${APP_ROMFS_STAGE_COMMANDS}
		COMMAND ${CMAKE_COMMAND} -E touch "${APP_ROMFS_STAGE_STAMP}"
		DEPENDS ${APP_ROMFS_FILES}
		VERBATIM
	)

	# makerom's -romfs input is a packed RomFS image, not the source directory.
	# Generate it as a build output and make the CIA depend on it explicitly.
	add_custom_command(
		OUTPUT "${APP_ROMFS_BIN}"
		COMMAND "${APP_ROMFS_PACKER}" -c --type romfs --romfs-dir "${APP_ROMFS_DIR}" --file "${APP_ROMFS_BIN}"
		DEPENDS "${APP_ROMFS_STAGE_STAMP}" ${APP_ROMFS_FILES}
		WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
		VERBATIM
	)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.3dsx"
		COMMAND "${APP_3DSX_PACKER}" "${PROJECT_NAME}.elf" "${PROJECT_NAME}.3dsx" "--romfs=${APP_ROMFS_DIR}" "--smdh=${APP_SMDH}"
		DEPENDS ${PROJECT_NAME}.elf "${APP_ROMFS_STAGE_STAMP}" ${APP_ROMFS_FILES}
		WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
		VERBATIM
	)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.cia"
		COMMAND "${APP_MAKEROM}" -f cia
				-target t
				-exefslogo
				-o "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.cia"
				-elf "${PROJECT_NAME}.elf"
				-rsf "${APP_RSF}"
				-banner "${APP_BANNER}"
				-icon "${APP_SMDH}"
				-romfs "${APP_ROMFS_BIN}"
			DEPENDS ${PROJECT_NAME}.elf "${APP_ROMFS_BIN}"
			WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
			VERBATIM
    )

	add_custom_target( N3DS ALL
		DEPENDS ${PROJECT_NAME}.3dsx
				${PROJECT_NAME}.cia
	)

endif (CTR_RELEASE)
