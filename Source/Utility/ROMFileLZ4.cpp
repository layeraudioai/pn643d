#include "stdafx.h"
#include "ROMFileLZ4.h"
#include "LZ4Block.h"
#include "Math/MathUtil.h"
#include "Utility/Stream.h"
#include <string.h>
#include <new>

namespace {
static u32 read_le32(const u8 *p) { return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static u64 read_le64(const u8 *p) { return (u64)read_le32(p) | ((u64)read_le32(p+4)<<32); }
}
ROMFileLZ4::ROMFileLZ4(const char *filename) : ROMFile(filename), mFile(NULL), mRomSize(0), mScratch(NULL), mCachedBlockIndex(0xFFFFFFFFu) {}
ROMFileLZ4::~ROMFileLZ4() { if (mFile) fclose(mFile); delete [] mScratch; }

bool ROMFileLZ4::Open(COutputStream &messages)
{
    mBlocks.clear();
    mCachedBlockIndex = 0xFFFFFFFFu;
    static const u8 magic[4]={0x04,0x22,0x4d,0x18};
    u8 hdr[15];
    mFile=fopen(mFilename,"rb");
    if (!mFile) { messages << "Couldn't open LZ4 ROM '" << mFilename << "'"; return false; }
    if (fread(hdr,1,sizeof(hdr),mFile)!=sizeof(hdr) || memcmp(hdr,magic,4)!=0 || hdr[4]!=0x68 || hdr[5]!=0x40) {
        messages << "Unsupported LZ4 ROM frame (expected independent 64 KiB blocks)"; return false;
    }
    const u64 size=read_le64(hdr+6);
    if (size < 0x40 || size > 0xffffffffu) { messages << "Invalid LZ4 ROM size"; return false; }
    mRomSize=(u32)size;
    mScratch=new (std::nothrow) u8[131072];
    if (!mScratch) { messages << "Out of memory opening LZ4 ROM"; return false; }
    if (fseek(mFile,0,SEEK_END)!=0) { messages << "Unable to seek in LZ4 ROM"; return false; }
    const long file_end=ftell(mFile);
    if (file_end < 0 || fseek(mFile,(long)sizeof(hdr),SEEK_SET)!=0) { messages << "Unable to seek in LZ4 ROM"; return false; }
    u32 out_offset=0;
    for (;;) {
        u8 b[4]; if (fread(b,1,4,mFile)!=4) { messages << "Truncated LZ4 ROM block table"; return false; }
        const u32 field=read_le32(b); if (!field) break;
        const bool raw=(field&0x80000000u)!=0;
        const u32 compressed=field&0x7fffffffu;
        if (!compressed || compressed>65536 || out_offset>=mRomSize) { messages << "Invalid LZ4 ROM block size"; return false; }
        const long file_position = ftell(mFile);
        if (file_position < 0) { messages << "Unable to seek in LZ4 ROM"; return false; }
        const u32 file_offset = (u32)file_position;
        if (file_position > file_end || compressed > (u32)(file_end - file_position)) {
            messages << "Truncated LZ4 ROM data";
            return false;
        }
        const u32 expected = Min((u32)65536, mRomSize - out_offset);
        // This ROM format uses independent 64 KiB blocks. The frame content
        // size provides each block's decoded length, so defer decompression
        // until a block is actually requested rather than decoding the entire
        // ROM during startup. Raw blocks can still be checked immediately.
        if (raw && compressed != expected) { messages << "LZ4 ROM block size mismatch"; return false; }
        Block block={file_offset,out_offset,compressed,expected,raw}; mBlocks.push_back(block);
        if (fseek(mFile,(long)(file_offset+compressed),SEEK_SET)!=0) return false;
        out_offset+=expected;
    }
    if (out_offset!=mRomSize) { messages << "LZ4 ROM is incomplete"; return false; }
    u8 first[4]; if (!ReadRange(0,first,4)) { messages << "Unable to read LZ4 ROM header"; return false; }
    u32 header; memcpy(&header,first,sizeof(header));
    if (!SetHeaderMagic(header)) { messages << "Invalid N64 header in LZ4 ROM"; return false; }
    return true;
}

bool ROMFileLZ4::ReadRange(u32 offset,u8 *dst,u32 length)
{
    if (!mFile || !dst || offset>mRomSize || length>mRomSize-offset) return false;
    u32 done=0;
    while (done<length) {
        const u32 pos=offset+done;
        size_t lo=0,hi=mBlocks.size();
        while (lo<hi) { size_t mid=lo+(hi-lo)/2; const Block &b=mBlocks[mid]; if (pos<b.output_offset) hi=mid; else if (pos>=b.output_offset+b.output_size) lo=mid+1; else {lo=mid;break;} }
        if (lo>=mBlocks.size()) return false;
        const Block &b=mBlocks[lo]; const u32 in_block=pos-b.output_offset;
        const u32 take=Min(length-done,b.output_size-in_block);
        if (fseek(mFile,(long)b.file_offset,SEEK_SET)!=0) return false;
        if (b.raw) {
            if (fseek(mFile,(long)(b.file_offset+in_block),SEEK_SET)!=0 || fread(dst+done,1,take,mFile)!=take) return false;
        } else {
            if (mCachedBlockIndex != (u32)lo) {
                if (fread(mScratch,1,b.compressed_size,mFile)!=b.compressed_size) return false;
                const int n=DaedalusLZ4_decompress_safe(mScratch,(int)b.compressed_size,mScratch+65536,65536);
                if (n!=(int)b.output_size) return false;
                mCachedBlockIndex = (u32)lo;
            }
            memcpy(dst+done,mScratch+65536+in_block,take);
        }
        done+=take;
    }
    return true;
}
bool ROMFileLZ4::ReadChunk(u32 offset,u8 *dst,u32 length)
{
    if (!ReadRange(offset,dst,length)) return false;
    CorrectSwap(dst,length);
    return true;
}
bool ROMFileLZ4::LoadRawData(u32 bytes_to_read,u8 *dst,COutputStream &messages)
{
    if (!ReadRange(0,dst,bytes_to_read)) { messages << "Unable to read LZ4 ROM data"; return false; }
    CorrectSwap(dst,bytes_to_read);
    return true;
}
