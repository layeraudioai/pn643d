#pragma once
#ifndef UTILITY_ROMFILELZ4_H_
#define UTILITY_ROMFILELZ4_H_
#include <stdio.h>
#include "ROMFile.h"
#include <vector>

class ROMFileLZ4 : public ROMFile
{
public:
    explicit ROMFileLZ4(const char *filename);
    virtual ~ROMFileLZ4();
    virtual bool Open(COutputStream &messages);
    virtual bool ReadChunk(u32 offset, u8 *dst, u32 length);
    virtual bool IsCompressed() const { return true; }
    virtual u32 GetRomSize() const { return mRomSize; }
private:
    virtual bool LoadRawData(u32 bytes_to_read, u8 *dst, COutputStream &messages);
    struct Block { u32 file_offset, output_offset, compressed_size, output_size; bool raw; };
    bool ReadRange(u32 offset, u8 *dst, u32 length);
    FILE *mFile;
    u32 mRomSize;
    std::vector<Block> mBlocks;
    u8 *mScratch;
};
#endif
