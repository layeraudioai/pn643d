 #CMake File for DaedalusX64

#Build instructions
# Create a directory in the DaedalusX64 root directory and cd into it 'eg: mkdir tmp ~/DaedalusX64'
# ensure you have cmake installed
# CTR_RELEASE - Builds for Nintendo 3DS

cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 14)
project (${PROJECT_NAME})

enable_language(ASM)

#Default Files for build
set (BASE_FILES StdAfx.cpp)
set (CONFIG_FILES Config/ConfigOptions.cpp)
set (CORE_FILES Core/RE2Task.cpp Core/RDRam.cpp Core/Cheats.cpp Core/CPU.cpp Core/DMA.cpp Core/Dynamo.cpp Core/FlashMem.cpp Core/Interpret.cpp Core/Interrupts.cpp Core/JpegTask.cpp Core/Memory.cpp Core/PIF.cpp Core/R4300.cpp Core/ROM.cpp Core/ROMBuffer.cpp Core/ROMImage.cpp Core/RomSettings.cpp Core/RSP_HLE.cpp Core/Save.cpp Core/SaveState.cpp Core/TLB.cpp)
set (DEBUG_FILES Debug/DebugConsoleImpl.cpp Debug/DebugLog.cpp Debug/Dump.cpp)
set (DYNAREC_FILES DynaRec/BranchType.cpp DynaRec/DynaRecProfile.cpp DynaRec/Fragment.cpp DynaRec/FragmentCache.cpp DynaRec/IndirectExitMap.cpp DynaRec/StaticAnalysis.cpp DynaRec/TraceRecorder.cpp)
set (GRAPHICS_FILES Graphics/ColourValue.cpp Graphics/PngUtil.cpp Graphics/TextureTransform.cpp)
set (HLEAUDIO_FILES HLEAudio/AudioHLEProcessor.cpp HLEAudio/ABI1.cpp HLEAudio/ABI2.cpp HLEAudio/ABI3.cpp HLEAudio/ABI3mp3.cpp HLEAudio/AudioBuffer.cpp HLEAudio/HLEMain.cpp HLEAudio/ABI_ADPCM.cpp HLEAudio/ABI_Buffers.cpp HLEAudio/ABI_Filters.cpp HLEAudio/ABI_MixerInterleave.cpp HLEAudio/ENV_Mixer.cpp HLEAudio/ABI_Resample.cpp)
set (HLEGRAPHICS_FILES HLEGraphics/BaseRenderer.cpp HLEGraphics/BaseRenderer.h HLEGraphics/CachedTexture.cpp HLEGraphics/ConvertImage.cpp HLEGraphics/ConvertTile.cpp HLEGraphics/DLDebug.cpp HLEGraphics/DLParser.cpp HLEGraphics/Microcode.cpp HLEGraphics/RDPStateManager.cpp HLEGraphics/TextureCache.cpp HLEGraphics/TextureInfo.cpp HLEGraphics/uCodes/Ucode.cpp)
set (INTERFACE_FILES Interface/RomDB.cpp)
set (MATH_FILES Math/Matrix4x4.cpp)
set (OSHLE_FILES OSHLE/OS.cpp OSHLE/patch.cpp)
set (PLUGIN_FILES Plugins/GraphicsPlugin.cpp)
set (SYSTEM_FILES System/Paths.cpp System/System.cpp)
set (TEST_FILES Test/BatchTest.cpp)
set (UTILITY_FILES Utility/CRC.cpp Utility/DataSink.cpp Utility/FastMemcpy.cpp  Utility/FramerateLimiter.cpp Utility/Hash.cpp Utility/IniFile.cpp Utility/MemoryHeap.cpp Utility/Preferences.cpp Utility/PrintOpCode.cpp Utility/Profiler.cpp Utility/ROMFile.cpp Utility/ROMFileCache.cpp Utility/ROMFileCompressed.cpp Utility/ROMFileMemory.cpp Utility/ROMFileUncompressed.cpp Utility/Stream.cpp Utility/StringUtil.cpp Utility/Synchroniser.cpp Utility/Timer.cpp  Utility/Translate.cpp Utility/ZLibWrapper.cpp)
set (UNKNOWN_FILES Utility/FastMemcpy_test.cpp)
set (DEBUG_ONLY Core/Registers.cpp)
set (BUILD ${BASE_FILES} ${CONFIG_FILES} ${CORE_FILES} ${DEBUG_FILES} ${DYNAREC_FILES} ${GRAPHICS_FILES} ${HLEAUDIO_FILES} ${HLEGRAPHICS_FILES} ${INTERFACE_FILES} ${MATH_FILES} ${OSHLE_FILES} ${PLUGIN_FILES} ${SYSTEM_FILES} ${TEST_FILES} ${UTILITY_FILES})


# N3DS(CTR)
set (CTR_MAIN_FILES SysCTR/main.cpp)
set (CTR_DEBUG_FILES SysCTR/Debug/DaedalusAssertCTR.cpp SysCTR/Debug/DBGConsoleCTR.cpp )
set (CTR_DYNAREC_FILES SysCTR/DynaRec/arm/AssemblyUtilsARM.cpp SysCTR/DynaRec/arm/AssemblyWriterARM.cpp SysCTR/DynaRec/arm/CodeBufferManagerARM.cpp SysCTR/DynaRec/arm/DynarecStubARM.S SysCTR/DynaRec/arm/CodeGeneratorARM.cpp SysCTR/DynaRec/arm/N64RegisterCacheARM.cpp)
set (CTR_GRAPHICS_FILES SysCTR/Graphics/GraphicsContextCTR.cpp SysCTR/Graphics/NativeTextureCTR.cpp)
set (CTR_HLEGRAPHICS_FILES SysCTR/HLEGraphics/GraphicsPluginCTR.cpp SysCTR/HLEGraphics/RendererCTR.cpp SysCTR/HLEGraphics/ColourAdjuster.cpp SysCTR/HLEGraphics/BlendModes.cpp SysCTR/HLEGraphics/Combiner/BlendConstant.cpp SysCTR/HLEGraphics/Combiner/CombinerExpression.cpp SysCTR/HLEGraphics/Combiner/CombinerTree.cpp SysCTR/HLEGraphics/Combiner/RenderSettings.cpp)
set (CTR_HLEAUDIO_FILES SysCTR/HLEAudio/AudioPluginCTR.cpp SysCTR/HLEAudio/AudioOutput.cpp)
set (CTR_INPUTMANAGER_FILES SysCTR/Input/InputManagerCTR.cpp SysCTR/Input/CTRMultiplayer.cpp)
set (CTR_UI_FILES SysCTR/UI/UserInterface.cpp SysCTR/UI/RomSelector.cpp SysCTR/UI/InGameMenu.cpp)
set (CTR_UTILITY_FILES SysCTR/Utility/CTRStorage.cpp SysCTR/Utility/CTRPerfLearning.cpp SysCTR/Utility/ThreadCTR.cpp SysCTR/Utility/IOCTR.cpp SysCTR/Utility/TimingCTR.cpp SysCTR/Utility/MemoryCTR.cpp SysCTR/Utility/CacheCTR.S)
set (CTR_BUILD ${CTR_DEBUG_FILES} ${CTR_DYNAREC_FILES} ${CTR_GRAPHICS_FILES} ${CTR_HLEAUDIO_FILES} ${CTR_HLEGRAPHICS_FILES} ${CTR_INPUTMANAGER_FILES} ${CTR_UI_FILES} ${CTR_UTILITY_FILES})




#Only difference between PSP Release and Debug is the definitions and Config Dev File - Try and merge



if (CTR_RELEASE)
	message("N3DS Release Build..")

	set(CMAKE_EXE_LINKER_FLAGS "-L${DEVKITPRO}/libctru/lib -L${DEVKITPRO}/picaGL/lib -L${DEVKITPRO}/portlibs/3ds/lib -specs=3dsx.specs -Wl,--gc-sections -Wl,-z,noexecstack -Wl,--no-warn-execstack")

	# Prefer this checkout's picaGL API header so Daedalus and the linked
	# picaGL library agree on the stereo extensions used by Preferences.cpp.
	include_directories(BEFORE ${PROJECT_SOURCE_DIR}/../../picaGL/include)
	include_directories(${DEVKITPRO}/libctru/include ${DEVKITPRO}/picaGL/include ${DEVKITPRO}/portlibs/3ds/include)

	add_definitions("-DDAEDALUS_CTR" "-D__3DS__")

	# Build Daedalus Lib
	add_library(daedalus STATIC
		${BUILD}
		${DEBUG_ONLY}
		${CTR_BUILD}
	)

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
		${PROJECT_SOURCE_DIR}/Config/Dev
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
	target_compile_definitions(${PROJECT_NAME}.elf PRIVATE DAEDALUS_TARGET_GAME_ID=${DAEDALUS_GAME_ID_VALUE})
	if(DAEDALUS_GAME_ID_VALUE AND EXISTS "${DAEDALUS_GAME_PROFILE}")
		target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
			"DAEDALUS_GAME_PROFILE_HEADER=\"Core/GameProfiles/${DAEDALUS_GAME_ID_HEX}.h\"")
	endif()

	target_include_directories(${PROJECT_NAME}.elf PRIVATE
		${PROJECT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/SysCTR/Include
		${PROJECT_SOURCE_DIR}/Config/Dev
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
	set(APP_ROMFS_DIR "romfs")
	set(APP_ROMFS_BIN "${PROJECT_NAME}.bin")

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.3dsx
		COMMAND ../Tools/3dsxtool.exe ${PROJECT_NAME}.elf ${PROJECT_NAME}.3dsx --romfs=${APP_ROMFS_DIR} --smdh=${APP_SMDH}
		DEPENDS ${PROJECT_NAME}.elf
	)

	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.cia
		COMMAND ../Tools/makerom.exe	-f cia
				-target t
				-exefslogo
				-o ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.cia
				-elf ${PROJECT_NAME}.elf
				-rsf ${APP_RSF}
				-banner ${APP_BANNER}
				-icon ${APP_SMDH}
				-romfs ${APP_ROMFS_BIN}
		DEPENDS ${PROJECT_NAME}.elf
    )

	add_custom_target( N3DS ALL
		DEPENDS ${PROJECT_NAME}.3dsx
				${PROJECT_NAME}.cia
	)

endif (CTR_RELEASE)