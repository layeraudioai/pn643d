/*
Copyright (C) 2006 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"
#include "ROMBuffer.h"

#include "ROM.h"
#include "DMA.h"

#ifdef DAEDALUS_PSP
#include "Graphics/GraphicsContext.h"
#include "SysPSP/Graphics/intraFont/intraFont.h"
#endif

#include "Math/MathUtil.h"

#include "Debug/DBGConsole.h"

#include "Utility/Preferences.h"
#include "Utility/ROMFile.h"
#include "Utility/ROMFileCache.h"
#include "Utility/ROMFileMemory.h"
#include "Utility/Stream.h"
#include "Utility/IO.h"

#ifdef DAEDALUS_CTR
#include "SysCTR/Utility/CTRPerfLearning.h"
#include "Utility/Timing.h"
#include <3ds.h>
extern bool isN3DS;
#endif

namespace
{
	bool			sRomLoaded( false );
	u8 *			spRomData( nullptr );
	u32				sRomSize( 0 );
	bool			sRomFixed( false );
	ROMFileCache *	spRomFileCache( nullptr );

#ifdef DAEDALUS_COMPRESSED_ROM_SUPPORT
	static bool		DECOMPRESS_ROMS( true );
#endif
	// Maximum read length is 8 bytes (i.e. double, u64)
	const u32		SCRATCH_BUFFER_LENGTH = 16;
	u8				sScratchBuffer[ SCRATCH_BUFFER_LENGTH ];

	bool		ShouldLoadAsFixed( u32 rom_size, bool learned_preload )
	{
#ifdef DAEDALUS_CTR
		// Keep the established New 3DS fast path. On Old 3DS, only use the
		// larger allocation after per-cartridge telemetry shows that streaming
		// cache misses are expensive; CTRPerfLearning limits this to small ROMs.
		if (isN3DS)
			return rom_size <= 32 * 1024 * 1024;
		return learned_preload;
#else
		(void)learned_preload;
		return true;
#endif
	}

#ifdef DAEDALUS_COMPRESSED_ROM_SUPPORT
	ROMFile *	DecompressRom( ROMFile * p_rom_file, const char * temp_filename, COutputStream & messages )
	{
		ROMFile *	p_new_file( nullptr );
		FILE *		fh( fopen( temp_filename, "wb" ) );

		if( fh == nullptr )
		{
			messages << "Unable to create temporary rom '" << temp_filename << "' for decompression\n";
		}
		else
		{
			bool			failed = false;
			const u32		TEMP_BUFFER_SIZE = 32 * 1024;
			u8 *			p_temp_buffer( new u8[ TEMP_BUFFER_SIZE ] );

#ifdef DAEDALUS_DEBUG_CONSOLE
			CDebugConsole::Get()->MsgOverwriteStart();
#endif

			u32				offset( 0 );
			u32				total_length( p_rom_file->GetRomSize() );
			u32				length_remaining( total_length );

			while( length_remaining > 0 )
			{
#ifdef DAEDALUS_DEBUG_CONSOLE
				if ((offset % 0x8000) == 0)
				{
					CDebugConsole::Get()->MsgOverwrite(0, "Converted [M%d / %d] KB", offset /1024, total_length / 1024 );
				}
#endif
				u32			length_to_process( Min( length_remaining, TEMP_BUFFER_SIZE ) );

				if( !p_rom_file->ReadChunk( offset, p_temp_buffer, length_to_process ) )
				{
					failed = true;
					break;
				}

				if( fwrite( p_temp_buffer, 1, length_to_process, fh ) != length_to_process )
				{
					failed = true;
					break;
				}

				offset += length_to_process;
				length_remaining -= length_to_process;
			}
#ifdef DAEDALUS_DEBUG_CONSOLE
			CDebugConsole::Get()->MsgOverwrite(0, "Converted [M%d / %d] KB", offset /1024, total_length / 1024 );
			CDebugConsole::Get()->MsgOverwriteEnd();
#endif

			fclose( fh );
			delete [] p_temp_buffer;

			if( failed )
			{
				messages << "Failed to decompress rom to '" << temp_filename << "' - out of disk space?\n";
			}
			else
			{
				//
				//	Open the newly created file
				//
				p_new_file = ROMFile::Create( temp_filename );
				if( p_new_file == nullptr )
				{
					messages << "Failed to open temporary rom '" << temp_filename << "' we just created\n";
				}
				else
				{
					if( !p_new_file->Open( messages ) )
					{
						messages << "Failed to open temporary rom '" << temp_filename << "' we just created\n";
						delete p_new_file;
						p_new_file = nullptr;
					}
				}
			}
		}

		return p_new_file;
	}
#endif
}

//*****************************************************************************
//
//*****************************************************************************
bool RomBuffer::Create()
{
	// Create memory heap used for either ROM Cache or ROM buffer
	// We do this to avoid memory fragmentation
	CROMFileMemory::Create();
	return true;
}

//*****************************************************************************
//
//*****************************************************************************
void RomBuffer::Destroy()
{

}

//*****************************************************************************
//
//*****************************************************************************
bool RomBuffer::Open()
{
	CNullOutputStream messages;
	const char * filename   = g_ROM.mFileName;
	ROMFile *    p_rom_file = ROMFile::Create( filename );
	if(p_rom_file == nullptr)
	{
		#ifdef DAEDALUS_DEBUG_CONSOLE
		DBGConsole_Msg(0, "Failed to create [C%s]\n", filename);
		#endif
		return false;
	}

	if( !p_rom_file->Open( messages ) )
	{
		#ifdef DAEDALUS_DEBUG_CONSOLE
		DBGConsole_Msg(0, "Failed to open [C%s]\n", filename);
		#endif
		delete p_rom_file;
		return false;
	}

	sRomSize = p_rom_file->GetRomSize();

#ifdef DAEDALUS_CTR
	// ROMBuffer opens before ROM_LoadFile fills g_ROM.mRomID, so identify the
	// cartridge directly from its canonicalized header for the learning profile.
	ROMHeader learning_header = {};
	bool have_learning_id = p_rom_file->LoadData(
		sizeof(learning_header), reinterpret_cast<u8 *>(&learning_header), messages);
	if (have_learning_id)
	{
		ROMFile::ByteSwap_3210(&learning_header, sizeof(learning_header));
		const RomID learning_id(learning_header);
		if (!learning_id.Empty())
			CTRPerfLearning::BeginGame(learning_id.CRC[0], learning_id.CRC[1],
				learning_id.CountryID, sRomSize);
	}
	const bool learned_preload = CTRPerfLearning::ShouldPreloadROM(sRomSize, isN3DS);
	u64 rom_load_start = 0;
	NTiming::GetPreciseTime(&rom_load_start);
#else
	const bool learned_preload = false;
#endif

	const bool try_fixed_load = ShouldLoadAsFixed(sRomSize, learned_preload);
	u8 *p_bytes = nullptr;
	if (try_fixed_load)
	{
		// Now, allocate memory for rom - round up to a 4 byte boundary. Keep
		// headroom for dynarec, textures, and audio; a successful allocation
		// without this reserve can still leave the emulator unstable later.
		const u32 size_aligned = AlignPow2(sRomSize, 4);
#ifdef DAEDALUS_CTR
		const u32 runtime_reserve = isN3DS ? (12 * 1024 * 1024) : (10 * 1024 * 1024);
		const u32 free_memory = osGetMemRegionFree(MEMREGION_APPLICATION);
		if (free_memory > size_aligned && free_memory - size_aligned > runtime_reserve)
			p_bytes = (u8 *)CROMFileMemory::Get()->Alloc(size_aligned);
#else
		p_bytes = (u8 *)CROMFileMemory::Get()->Alloc(size_aligned);
#endif
	}

	if (try_fixed_load && p_bytes != nullptr)
	{

#ifndef DAEDALUS_PSP
		if( !p_rom_file->LoadData( sRomSize, p_bytes, messages ) )
		{
			#ifdef DAEDALUS_DEBUG_CONSOLE
			DBGConsole_Msg(0, "Full ROM load failed for [C%s]; falling back to the ROM cache\n", filename);
			#endif
			CROMFileMemory::Get()->Free( p_bytes );
			p_bytes = nullptr;
		}
#else
		u32 offset( 0 );
		u32 length_remaining( sRomSize );
		const u32 TEMP_BUFFER_SIZE {128 * 1024};

		intraFont* ltn8  = intraFontLoad( "flash0:/font/ltn8.pgf", INTRAFONT_CACHE_ASCII);
		intraFontSetStyle( ltn8, 1.5f, 0xFFFFFFFF, 0, 0.f, INTRAFONT_ALIGN_CENTER );

		while( offset < sRomSize )
		{
			u32 length_to_process( Min( length_remaining, TEMP_BUFFER_SIZE ) );

			if( !p_rom_file->ReadChunk( offset, p_bytes + offset, length_to_process ) )
			{
				break;
			}

			offset += length_to_process;
			length_remaining -= length_to_process;

			CGraphicsContext::Get()->BeginFrame();
			CGraphicsContext::Get()->ClearToBlack();
			intraFontPrintf( ltn8, 480/2, (272>>1), "Buffering ROM %d%%...", offset * 100 / sRomSize );
			CGraphicsContext::Get()->EndFrame();
			CGraphicsContext::Get()->UpdateFrame( false );
		}

		intraFontUnload( ltn8 );
		if (offset < sRomSize)
		{
			CROMFileMemory::Get()->Free(p_bytes);
			p_bytes = nullptr;
		}
#endif
		if (p_bytes != nullptr)
		{
			spRomData = p_bytes;
			sRomFixed = true;
			delete p_rom_file;
			p_rom_file = nullptr;
		}
	}

	if (!sRomFixed)
	{
#ifdef DAEDALUS_COMPRESSED_ROM_SUPPORT
		if(DECOMPRESS_ROMS)
		{
			bool	compressed( p_rom_file->IsCompressed() );
			bool	byteswapped( p_rom_file->RequiresSwapping() );
			if(compressed)// || byteswapped)
			{
				const char * temp_filename( "daedrom.tmp" );

				#ifdef DAEDALUS_DEBUG_CONSOLE
				if(compressed && byteswapped)
				{
					DBGConsole_Msg( 0, "Rom is [Mcompressed] and [Mbyteswapped]" );
				}
				else if(compressed)
				{
					DBGConsole_Msg( 0, "Rom is [Mcompressed]" );
				}
				else
				{
					DBGConsole_Msg( 0, "Rom is [Mbyteswapped]" );
				}
				DBGConsole_Msg( 0, "Decompressing rom to [C%s] (this may take some time)", temp_filename );
				#endif
				CNullOutputStream		local_messages;

				ROMFile * p_new_file = DecompressRom( p_rom_file, temp_filename, local_messages );
				#ifdef DAEDALUS_DEBUG_CONSOLE
				DBGConsole_Msg( 0, "messages:\n%s", local_messages.c_str() );
				#endif
				messages << local_messages;

				if(p_new_file != nullptr)
				{
					#ifdef DAEDALUS_DEBUG_CONSOLE
					DBGConsole_Msg( 0, "Decompression [gsuccessful]. Booting using decompressed rom" );
					#endif
					delete p_rom_file;
					p_rom_file = p_new_file;
				}
				#ifdef DAEDALUS_DEBUG_CONSOLE
				else
				{
					DBGConsole_Msg( 0, "Decompression [rfailed]. Booting using original rom" );
				}
				#endif
			}
		}
#endif
		spRomFileCache = new ROMFileCache();
		if (spRomFileCache == nullptr || !spRomFileCache->Open(p_rom_file))
		{
			delete spRomFileCache;
			spRomFileCache = nullptr;
			delete p_rom_file;
#ifdef DAEDALUS_CTR
			u64 load_end = 0;
			NTiming::GetPreciseTime(&load_end);
			CTRPerfLearning::RecordRomLoad(try_fixed_load, false, false,
				load_end - rom_load_start);
			CTRPerfLearning::EndGame();
#endif
			return false;
		}
		sRomFixed = false;
	}
#ifdef DAEDALUS_CTR
	u64 load_end = 0;
	NTiming::GetPreciseTime(&load_end);
	CTRPerfLearning::RecordRomLoad(try_fixed_load, sRomFixed, true,
		load_end - rom_load_start);
#endif
	#ifdef DAEDALUS_DEBUG_CONSOLE
	DBGConsole_Msg(0, "Opened [C%s]\n", filename);
	#endif
	sRomLoaded = true;
	return true;
}

//*****************************************************************************
//
//*****************************************************************************
void	RomBuffer::Close()
{
#ifdef DAEDALUS_CTR
	CTRPerfLearning::EndGame();
#endif
	if (spRomData)
	{
		CROMFileMemory::Get()->Free( spRomData );
		spRomData = nullptr;
	}

	if (spRomFileCache)
	{
		spRomFileCache->Close();
		delete spRomFileCache;
		spRomFileCache = nullptr;
	}

	sRomSize   = 0;
	sRomLoaded = false;
	sRomFixed  = false;
}

bool	RomBuffer::IsRomLoaded() { return sRomLoaded; }
u32		RomBuffer::GetRomSize() { return sRomSize; }

namespace
{
	void	CopyBytesRaw( ROMFileCache * p_cache, u8 * p_dst, u32 rom_offset, u32 length )
	{
		// Read the cached bytes into our scratch buffer, and return that
		u32		dst_offset( 0 );
		u32		src_offset( rom_offset );

		// Similar algorithm to below - we don't care about byte swapping though
		while(length > 0)
		{
			u8 *	p_chunk_base {};
			u32		chunk_offset {};
			u32		chunk_size {};

			if( !p_cache->GetChunk( src_offset, &p_chunk_base, &chunk_offset, &chunk_size ) )
			{
				// Out of range
				break;
			}

			// Calculate how many bytes we can transfer this pass
			u32		offset_into_chunk( src_offset - chunk_offset );
			u32		bytes_remaining_in_chunk( chunk_size - offset_into_chunk );
			u32		bytes_this_pass( Min( length, bytes_remaining_in_chunk ) );

			#ifdef DAEDALUS_ENABLE_ASSERTS
			DAEDALUS_ASSERT( s32( bytes_this_pass ) > 0, "How come we're trying to copy <= 0 bytes across?" );
			#endif
			// Copy this chunk across
			memcpy( p_dst + dst_offset, p_chunk_base + offset_into_chunk, bytes_this_pass );

			// Update the src/dst pointers and reduce length by the number of copied bytes
			dst_offset += bytes_this_pass;
			src_offset += bytes_this_pass;
			length -= bytes_this_pass;
		}
	}
}

//*****************************************************************************
//
//*****************************************************************************
void	RomBuffer::GetRomBytesRaw( void * p_dst, u32 rom_start, u32 length )
{
	if( sRomFixed )
	{
		memcpy(p_dst, (const u8*)spRomData + rom_start, length );
	}
	else
	{
		#ifdef DAEDALUS_ENABLE_ASSERTS
		DAEDALUS_ASSERT( spRomFileCache != nullptr, "How come we have no file cache?" );
		#endif
		CopyBytesRaw( spRomFileCache, reinterpret_cast< u8 * >( p_dst ), rom_start, length );
	}
}

//*****************************************************************************
//
//*****************************************************************************
void	RomBuffer::PutRomBytesRaw( u32 rom_start, const void * p_src, u32 length )
{
	#ifdef DAEDALUS_ENABLE_ASSERTS
	DAEDALUS_ASSERT( IsRomAddressFixed(), "Cannot put rom bytes when the data isn't fixed" );
	#endif

	memcpy( (u8*)spRomData + rom_start, p_src, length );

}

//*****************************************************************************
//
//*****************************************************************************
void * RomBuffer::GetAddressRaw( u32 rom_start )
{
	if( rom_start < sRomSize )
	{
		if( sRomFixed )
		{
			return (u8 *)spRomData + rom_start;
		}
		else
		{
			// Read the cached bytes into our scratch buffer, and return that
			#ifdef DAEDALUS_ENABLE_ASSERTS
			DAEDALUS_ASSERT( spRomFileCache != nullptr, "How come we have no file cache?" );
				#endif
			CopyBytesRaw( spRomFileCache, sScratchBuffer, rom_start, SCRATCH_BUFFER_LENGTH );

			return sScratchBuffer;
		}
	}

	return nullptr;
}

//*****************************************************************************
//
//*****************************************************************************
void RomBuffer::CopyToRam( u8 * p_dst, u32 dst_offset, u32 dst_size, u32 src_offset, u32 length )
{
	if( sRomFixed )
	{
		const u8 *	p_src( (const u8 *)spRomData );
		u32			src_size( sRomSize );

		DMA_HandleTransfer( p_dst, dst_offset, dst_size, p_src, src_offset, src_size, length );
	}
	else
	{
		while(length > 0)
		{
			u8 *	p_chunk_base {};
			u32		chunk_offset {};
			u32		chunk_size {};

			if( !spRomFileCache->GetChunk( src_offset, &p_chunk_base, &chunk_offset, &chunk_size ) )
			{
				// Out of range
				break;
			}

			// Calculate how many bytes we can transfer this pass
			u32		offset_into_chunk( src_offset - chunk_offset );
			u32		bytes_remaining_in_chunk( chunk_size - offset_into_chunk );
			u32		bytes_this_pass( Min( length, bytes_remaining_in_chunk ) );
			#ifdef DAEDALUS_ENABLE_ASSERTS
			DAEDALUS_ASSERT( s32( bytes_this_pass ) > 0, "How come we're trying to copy <= 0 bytes across?" );
			#endif
			// Copy this chunk across
			if( !DMA_HandleTransfer( p_dst, dst_offset, dst_size, p_chunk_base, offset_into_chunk, chunk_size, bytes_this_pass  ) )
			{
				break;
			}

			// Update the src/dst pointers and reduce length by the number of copied bytes
			dst_offset += bytes_this_pass;
			src_offset += bytes_this_pass;
			length -= bytes_this_pass;
		}
	}
}

//*****************************************************************************
//
//*****************************************************************************
bool RomBuffer::IsRomAddressFixed()
{
	return sRomFixed;
}

//*****************************************************************************
//
//*****************************************************************************
const void * RomBuffer::GetFixedRomBaseAddress()
{
	#ifdef DAEDALUS_ENABLE_ASSERTS
	DAEDALUS_ASSERT( IsRomLoaded(), "The rom isn't loaded" );
	DAEDALUS_ASSERT( IsRomAddressFixed(), "Trying to access the rom base address when it's not fixed" );
#endif
	return spRomData;

}
