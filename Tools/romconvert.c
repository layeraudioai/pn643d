/*
 * DaedalusX64 / PN643D ROM conversion driver.
 *
 * Replaces make.bat, romconvert.bat, build_daedalus.sh and the two small
 * Python metadata/banner drivers for the CTR_RELEASE workflow.  Metadata and
 * file-copy operations are implemented here; the existing 3DS toolchain and
 * format tools are invoked directly (no shell/Python dependency).
 *
 * Build: cc -O2 -std=c11 -Wall -Wextra -o romconvert Tools/romconvert.c
 * Windows: compile with MinGW-w64; statically link its C runtime if desired.
 * Run from the repository root or daed/: romconvert [command] [options]
 * Add --gui to open the optional Windows graphical front-end.
 *
 * This is not a replacement for devkitPro/devkitARM, CMake, make, bannertool,
 * 3dstool, gbmin, 3dsxtool or makerom. Their upstream source/build inputs are
 * not part of this repository, so they cannot honestly be statically linked
 * into this source file. Put them on PATH / in daed/Tools as the batch flow
 * expects. See romconvert_aio_README.txt for the exact limits.
 */
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
#  define PATH_SEP '\\'
#  define strcasecmp _stricmp
#else
#  include <dirent.h>
#  include <strings.h>
#  include <unistd.h>
#  include <sys/wait.h>
#  define PATH_SEP '/'
#endif

/* The packager uses upstream LZ4-HC level 10 with favor-decompression-speed
   enabled. This is equivalent to the relevant lz4 CLI compression settings
   (-10 --favor-decSpeed), while keeping the converter self-contained. */
#include "../Source/third_party/lz4/lz4.c"
#include "../Source/third_party/lz4/lz4hc.c"
#include "../Source/Utility/LZ4Block.c"

#define PATH_CAP 4096
#define ROM_HEADER_READ 0x1000
#define MAX_ROM_FILES 4096

typedef struct { char **items; size_t count, capacity; } StringList;

static void path_join(char *out, size_t cap, const char *a, const char *b) {
    size_t a_len = strlen(a), b_len = strlen(b);
    int need_sep = a_len && a[a_len - 1] != '/' && a[a_len - 1] != '\\';
    const char *sep = PATH_SEP == '/' ? "/" : "\\";
    size_t sep_len = need_sep ? 1u : 0u;
    if (cap == 0) return;
    if (a_len + sep_len + b_len >= cap) { out[0] = 0; return; }
    memcpy(out, a, a_len);
    if (need_sep) out[a_len++] = sep[0];
    memcpy(out + a_len, b, b_len + 1);
}
static int is_dir(const char *p) {
    struct stat s;
    if (stat(p, &s) != 0) return 0;
#ifdef _WIN32
    return (s.st_mode & _S_IFMT) == _S_IFDIR;
#else
    return S_ISDIR(s.st_mode);
#endif
}
static int is_file(const char *p) {
    struct stat s;
    if (stat(p, &s) != 0) return 0;
#ifdef _WIN32
    return (s.st_mode & _S_IFMT) == _S_IFREG;
#else
    return S_ISREG(s.st_mode);
#endif
}
static int mkdir_one(const char *p) {
#ifdef _WIN32
    if (_mkdir(p) == 0) return 1;
#else
    if (mkdir(p, 0775) == 0) return 1;
#endif
    return errno == EEXIST && is_dir(p);
}
static int mkdirs(const char *p) {
    char tmp[PATH_CAP]; size_t i, n;
    if (!p || !*p || strlen(p) >= sizeof(tmp)) return 0;
    strcpy(tmp, p); n = strlen(tmp);
    for (i = 1; i < n; ++i) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i]; tmp[i] = 0;
            if (*tmp && !mkdir_one(tmp) && !is_dir(tmp)) return 0;
            tmp[i] = c;
        }
    }
    return mkdir_one(tmp) || is_dir(tmp);
}
static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb"), *out;
    unsigned char buf[65536]; size_t n;
    if (!in) { fprintf(stderr, "open failed: %s (%s)\n", src, strerror(errno)); return 0; }
    out = fopen(dst, "wb");
    if (!out) { fprintf(stderr, "create failed: %s (%s)\n", dst, strerror(errno)); fclose(in); return 0; }
    while ((n = fread(buf, 1, sizeof(buf), in)) != 0) if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return 0; }
    if (ferror(in)) { fclose(in); fclose(out); return 0; }
    fclose(in); return fclose(out) == 0;
}
/* N64 ROMs are commonly distributed in three byte orders. Daedalus's CTR
   loader can use the N64 word-swapped layout directly on its little-endian
   ARM11 host, avoiding its load-time word swap for canonical .z64 input. */
typedef enum { ROM_ORDER_INVALID = 0, ROM_ORDER_Z64, ROM_ORDER_V64, ROM_ORDER_N64, ROM_ORDER_WORD_SWAPPED } RomByteOrder;
static RomByteOrder detect_rom_order(const unsigned char magic[4]) {
    if (magic[0] == 0x80 && magic[1] == 0x37 && magic[2] == 0x12 && magic[3] == 0x40) return ROM_ORDER_Z64;
    if (magic[0] == 0x37 && magic[1] == 0x80 && magic[2] == 0x40 && magic[3] == 0x12) return ROM_ORDER_V64;
    if (magic[0] == 0x40 && magic[1] == 0x12 && magic[2] == 0x37 && magic[3] == 0x80) return ROM_ORDER_N64;
    /* Accept the legacy 16-bit-word-swapped form supported by read_rom_header. */
    if (magic[0] == 0x12 && magic[1] == 0x40 && magic[2] == 0x80 && magic[3] == 0x37) return ROM_ORDER_WORD_SWAPPED;
    return ROM_ORDER_INVALID;
}
static int normalize_rom_to_n64(const char *src, const char *dst) {
    FILE *in = NULL, *out = NULL;
    unsigned char buf[65536], magic[4];
    struct stat st;
    RomByteOrder order;
    size_t n;
    int ok = 1;
    if (stat(src, &st) != 0 || st.st_size < 0x40 || (st.st_size & 3) != 0) {
        fprintf(stderr, "invalid N64 ROM size: %s\n", src);
        return 0;
    }
    in = fopen(src, "rb");
    if (!in) return 0;
    if (fread(magic, 1, sizeof(magic), in) != sizeof(magic) ||
        (order = detect_rom_order(magic)) == ROM_ORDER_INVALID) {
        fprintf(stderr, "unrecognized N64 ROM byte order: %s\n", src);
        fclose(in);
        return 0;
    }
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return 0; }
    if (fseek(in, 0, SEEK_SET) != 0) ok = 0;
    while (ok && (n = fread(buf, 1, sizeof(buf), in)) != 0) {
        if (n & 3) { ok = 0; break; }
        if (order == ROM_ORDER_V64) {
            for (size_t i = 0; i < n; i += 2) { unsigned char t = buf[i]; buf[i] = buf[i + 1]; buf[i + 1] = t; }
        } else if (order == ROM_ORDER_N64) {
            for (size_t i = 0; i < n; i += 4) {
                unsigned char a = buf[i], b = buf[i + 1];
                buf[i] = buf[i + 3]; buf[i + 1] = buf[i + 2]; buf[i + 2] = b; buf[i + 3] = a;
            }
        } else if (order == ROM_ORDER_WORD_SWAPPED) {
            for (size_t i = 0; i < n; i += 4) {
                unsigned char a = buf[i], b = buf[i + 1];
                buf[i] = buf[i + 2]; buf[i + 1] = buf[i + 3]; buf[i + 2] = a; buf[i + 3] = b;
            }
        }
        /* At this point each group of four is canonical N64 big-endian.
           Reverse each word to the .n64/native-word layout consumed without
           a swap by ROMFile on the little-endian 3DS host. */
        for (size_t i = 0; i < n; i += 4) {
            unsigned char a = buf[i], b = buf[i + 1];
            buf[i] = buf[i + 3]; buf[i + 1] = buf[i + 2];
            buf[i + 2] = b; buf[i + 3] = a;
        }
        if (fwrite(buf, 1, n, out) != n) ok = 0;
    }
    if (ferror(in)) ok = 0;
    if (fclose(in) != 0) ok = 0;
    if (fclose(out) != 0) ok = 0;
    if (!ok) remove(dst);
    return ok;
}
static int move_file(const char *src, const char *dst) {
#ifdef _WIN32
    if (MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return 1;
#else
    if (rename(src, dst) == 0) return 1;
#endif
    if (!copy_file(src, dst)) return 0;
    return remove(src) == 0;
}
static int copy_tree(const char *src, const char *dst) {
#ifdef _WIN32
    char pattern[PATH_CAP]; WIN32_FIND_DATAA ent; HANDLE h;
    if (!mkdirs(dst)) return 0;
    path_join(pattern, sizeof(pattern), src, "*");
    h = FindFirstFileA(pattern, &ent);
    if (h == INVALID_HANDLE_VALUE) return 1;
    do {
        char a[PATH_CAP], b[PATH_CAP];
        if (!strcmp(ent.cFileName, ".") || !strcmp(ent.cFileName, "..")) continue;
        path_join(a, sizeof(a), src, ent.cFileName); path_join(b, sizeof(b), dst, ent.cFileName);
        if (ent.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (!copy_tree(a, b)) { FindClose(h); return 0; } }
        else if (!copy_file(a, b)) { FindClose(h); return 0; }
    } while (FindNextFileA(h, &ent));
    FindClose(h); return 1;
#else
    DIR *d = opendir(src); struct dirent *ent;
    if (!d) return 0;
    if (!mkdirs(dst)) { closedir(d); return 0; }
    while ((ent = readdir(d)) != NULL) {
        char a[PATH_CAP], b[PATH_CAP]; struct stat st;
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        path_join(a, sizeof(a), src, ent->d_name); path_join(b, sizeof(b), dst, ent->d_name);
        if (stat(a, &st) != 0) { closedir(d); return 0; }
        if (S_ISDIR(st.st_mode)) { if (!copy_tree(a, b)) { closedir(d); return 0; } }
        else if (S_ISREG(st.st_mode) && !copy_file(a, b)) { closedir(d); return 0; }
    }
    closedir(d); return 1;
#endif
}

/* Start a child without passing user-controlled paths through a shell. */
static int run_process(char *const argv[], const char *cwd) {
#ifdef _WIN32
    char cmd[32768]; size_t used = 0; int i; STARTUPINFOA si; PROCESS_INFORMATION pi;
    cmd[0] = 0;
    for (i = 0; argv[i]; ++i) {
        const char *s = argv[i]; int quote = !*s || strpbrk(s, " \t\"") != NULL;
        if (used && used + 1 < sizeof(cmd)) cmd[used++] = ' ';
        if (quote && used + 1 < sizeof(cmd)) cmd[used++] = '"';
        while (*s && used + 3 < sizeof(cmd)) {
            if (*s == '"') cmd[used++] = '\\';
            cmd[used++] = *s++;
        }
        if (quote && used + 1 < sizeof(cmd)) cmd[used++] = '"';
        cmd[used] = 0;
    }
    memset(&si, 0, sizeof(si)); memset(&pi, 0, sizeof(pi)); si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, cwd, &si, &pi)) {
        fprintf(stderr, "could not start %s (Win32 error %lu)\n", argv[0], GetLastError()); return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE); DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return (int)code;
#else
    pid_t pid = fork(); int status;
    if (pid < 0) return -1;
    if (pid == 0) { if (cwd && chdir(cwd) != 0) _exit(126); execvp(argv[0], argv); perror(argv[0]); _exit(127); }
    while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
#endif
}
static const char *tool(const char *name) { static char p[PATH_CAP]; snprintf(p, sizeof(p), "Tools/%s", name); return p; }
static int g_dry_run = 0;
static int g_minimal_emulator = 0;
static int g_nintenstation643d = 0;

/* Refuse to package the wrong profile if a stale CMake tree or bad option
   propagation ever turns the requested Nintenstation build into a normal one. */
static int cache_option_is_on(const char *builddir, const char *option) {
    char cache_path[PATH_CAP], line[512];
    FILE *cache;
    size_t key_len = strlen(option);
    path_join(cache_path, sizeof(cache_path), builddir, "CMakeCache.txt");
    cache = fopen(cache_path, "r");
    if (!cache) return 0;
    while (fgets(line, sizeof(line), cache)) {
        if (strncmp(line, option, key_len) == 0 &&
            strstr(line, ":BOOL=ON") != NULL) {
            fclose(cache);
            return 1;
        }
    }
    fclose(cache);
    return 0;
}

static int run_logged(char *const argv[], const char *cwd, const char *label) {
    int rc; int i; fprintf(stdout, "[romconvert] %s\n", label); fflush(stdout);
    if (g_dry_run) { fputs("+", stdout); for (i=0; argv[i]; ++i) printf(" \"%s\"", argv[i]); putchar('\n'); return 1; }
    rc = run_process(argv, cwd);
    if (rc != 0) fprintf(stderr, "[romconvert] failed (%d): %s\n", rc, label);
    return rc == 0;
}

static void sanitize_folder(const char *in, char *out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < cap; ++i) {
        unsigned char c = (unsigned char)in[i];
        /* The result becomes a Windows and POSIX path component, a CMake
           project name, and part of the ROMFS path. Keep only portable chars. */
        if (isalnum(c)) out[j++] = (char)c;
        else if ((c == '_' || c == '+') && j && out[j - 1] != '_') out[j++] = (char)c;
    }
    out[j] = 0;
    if (!j) snprintf(out, cap, "N64Game");
}
static void title_id(unsigned int cart_id, char *out, size_t cap) {
    /* Keep the RSF UniqueId a valid, fixed-width five-hex-digit value and
       make it stable per N64 cartridge ID instead of guessing from a title. */
    snprintf(out, cap, "0xD%04X", cart_id & 0xFFFFu);
}
static int replace_all(char *line, size_t cap, const char *needle, const char *replacement) {
    char result[4096];
    const char *cursor = line;
    size_t used = 0, needle_len = strlen(needle), replacement_len = strlen(replacement);
    const char *match;
    if (cap > sizeof(result) || needle_len == 0) return 0;
    while ((match = strstr(cursor, needle)) != NULL) {
        size_t prefix = (size_t)(match - cursor);
        if (used + prefix + replacement_len >= cap) return 0;
        memcpy(result + used, cursor, prefix); used += prefix;
        memcpy(result + used, replacement, replacement_len); used += replacement_len;
        cursor = match + needle_len;
    }
    size_t tail = strlen(cursor);
    if (used + tail >= cap) return 0;
    memcpy(result + used, cursor, tail + 1);
    memcpy(line, result, used + tail + 1);
    return 1;
}
static int write_rsf(const char *template_path, const char *out_path, const char *folder, unsigned int cart_id) {
    FILE *in = fopen(template_path, "rb"), *out; char line[4096], tid[32];
    int ok = 1;
    if (!in) return 0;
    out = fopen(out_path, "wb");
    if (!out) { fclose(in); return 0; }
    title_id(cart_id, tid, sizeof(tid));
    while (fgets(line, sizeof(line), in)) {
        if (!replace_all(line, sizeof(line), "0xDAED3", tid) ||
            !replace_all(line, sizeof(line), "DaedalusX64", folder) ||
            fputs(line, out) == EOF) {
            ok = 0;
            break;
        }
    }
    if (ferror(in)) ok = 0;
    if (fclose(in) != 0) ok = 0;
    if (fclose(out) != 0) ok = 0;
    return ok;
}
static uint32_t be32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void put_le32(FILE *f, uint32_t x) { for (int i = 0; i < 4; ++i) fputc((int)(x >> (8*i)) & 255, f); }
static void put_le64(FILE *f, uint64_t x) { for (int i = 0; i < 8; ++i) fputc((int)(x >> (8*i)) & 255, f); }
static uint32_t rotl32(uint32_t x, unsigned n) { return (x << n) | (x >> (32-n)); }
static uint32_t get_le32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint32_t xxh32_small(const unsigned char *p, size_t n) {
    const uint32_t p1=2654435761u,p2=2246822519u,p3=3266489917u,p4=668265263u,p5=374761393u;
    const unsigned char *end=p+n; uint32_t h;
    if (n>=16) { uint32_t v1=p1+p2,v2=p2,v3=0,v4=0-p1; const unsigned char *limit=end-16;
        do { uint32_t x=get_le32(p); v1=rotl32(v1+x*p2,13)*p1; p+=4; x=get_le32(p); v2=rotl32(v2+x*p2,13)*p1; p+=4; x=get_le32(p); v3=rotl32(v3+x*p2,13)*p1; p+=4; x=get_le32(p); v4=rotl32(v4+x*p2,13)*p1; p+=4; } while(p<=limit);
        h=rotl32(v1,1)+rotl32(v2,7)+rotl32(v3,12)+rotl32(v4,18);
    } else h=p5;
    h+=(uint32_t)n;
    while(p+4<=end) { uint32_t x=get_le32(p); h=rotl32(h+x*p3,17)*p4; p+=4; }
    while(p<end) h=rotl32(h+(*p++)*p5,11)*p1;
    h^=h>>15; h*=p2; h^=h>>13; h*=p3; h^=h>>16; return h;
}
/* Emit a standard LZ4 frame with independent 64 KiB blocks and no optional
   checksums. Match `lz4 -10 --favor-decSpeed`: level 10 enables LZ4-HC's
   decode-speed parser, while resetting before every block prevents a cross-
   block dictionary and keeps the frame compatible with the CTR range reader. */
static int compress_lz4_frame(const char *input, const char *output, uint64_t size) {
    FILE *in=fopen(input,"rb"), *out=NULL;
    unsigned char raw[65536], packed[66000];
    LZ4_streamHC_t *hc_stream=NULL;
    int ok=0;
    if (!in) return 0;
    out=fopen(output,"wb");
    if (!out) { fclose(in); return 0; }
    hc_stream=LZ4_createStreamHC();
    if (!hc_stream) goto done;
    { const unsigned char magic[4]={4,34,77,24}; unsigned char desc[10]; desc[0]=0x68; desc[1]=0x40;
      for(int i=0;i<8;i++) desc[2+i]=(unsigned char)(size>>(8*i));
      unsigned char hc=(unsigned char)(xxh32_small(desc,sizeof(desc))>>8);
      if(fwrite(magic,1,4,out)!=4 || fwrite(desc,1,sizeof(desc),out)!=sizeof(desc) || fputc(hc,out)==EOF) goto done; }
    uint64_t left=size;
    while(left) {
        size_t n=left>sizeof(raw)?sizeof(raw):(size_t)left;
        if(fread(raw,1,n,in)!=n) goto done;
        LZ4_resetStreamHC_fast(hc_stream,10);
        LZ4_favorDecompressionSpeed(hc_stream,1);
        int z=LZ4_compress_HC_continue(hc_stream,(const char *)raw,(char *)packed,(int)n,(int)sizeof(packed));
        if(z<=0) goto done;
        if((size_t)z>=n) { put_le32(out,0x80000000u|(uint32_t)n); if(fwrite(raw,1,n,out)!=n) goto done; }
        else { put_le32(out,(uint32_t)z); if(fwrite(packed,1,(size_t)z,out)!=(size_t)z) goto done; }
        left-=n;
    }
    put_le32(out,0); if(ferror(in)||ferror(out)) goto done; ok=1;
done:
    if(hc_stream) LZ4_freeStreamHC(hc_stream);
    if(fclose(in)!=0) ok=0;
    if(fclose(out)!=0) ok=0;
    return ok;
}
static int read_rom_header(const char *rom, unsigned char *header, uint64_t *size) {
    FILE *f = fopen(rom, "rb"); size_t n; struct stat st; if (!f) return 0;
    n = fread(header, 1, ROM_HEADER_READ, f);
    if (ferror(f)) { fclose(f); return 0; }
    fclose(f);
    if (n < 0x40 || stat(rom, &st) != 0 || st.st_size < 0x40) return 0;
    *size = (uint64_t)st.st_size;
    if (header[0] == 0x40 && header[1] == 0x12 && header[2] == 0x37 && header[3] == 0x80) {
        for (size_t i = 0; i + 3 < n; i += 4) { unsigned char a=header[i],b=header[i+1]; header[i]=header[i+3];header[i+1]=header[i+2];header[i+2]=b;header[i+3]=a; }
    } else if (header[0] == 0x37 && header[1] == 0x80 && header[2] == 0x40 && header[3] == 0x12) {
        for (size_t i = 0; i + 3 < n; i += 4) { unsigned char a=header[i];header[i]=header[i+1];header[i+1]=a;a=header[i+2];header[i+2]=header[i+3];header[i+3]=a; }
    } else if (header[0] == 0x12 && header[1] == 0x40 && header[2] == 0x80 && header[3] == 0x37) {
        for (size_t i = 0; i + 3 < n; i += 4) { unsigned char a=header[i];header[i]=header[i+2];header[i+2]=a;a=header[i+1];header[i+1]=header[i+3];header[i+3]=a; }
    } else if (!(header[0] == 0x80 && header[1] == 0x37 && header[2] == 0x12 && header[3] == 0x40)) return 0;
    return 1;
}
static int generate_metadata(const char *rom, const char *folder, const char *romfs) {
    unsigned char h[ROM_HEADER_READ]; uint64_t size; char path[PATH_CAP], rompath[512]; FILE *f;
    if (!read_rom_header(rom, h, &size)) { fprintf(stderr, "Invalid N64 ROM/header: %s\n", rom); return 0; }
    int rompath_len = snprintf(rompath, sizeof(rompath), "romfs:/Roms/%s.n64.lz4", folder);
    if (rompath_len < 0 || (size_t)rompath_len >= sizeof(rompath) || (size_t)rompath_len > 260) {
        fprintf(stderr, "ROM title path too long\n"); return 0;
    }
    path_join(path, sizeof(path), romfs, "rom.db"); f = fopen(path, "wb"); if (!f) return 0;
    put_le64(f, UINT64_C(0x42444D5244454144)); put_le32(f, 4); put_le32(f, 1);
    fwrite(rompath, 1, strlen(rompath), f); for (size_t i = strlen(rompath); i < 261; ++i) fputc(0, f);
    put_le32(f, be32(h + 0x10)); put_le32(f, be32(h + 0x14)); fputc(h[0x3e], f); fputc(0, f); fputc(0, f); fputc(0, f);
    put_le32(f, 1); put_le32(f, be32(h + 0x10)); put_le32(f, be32(h + 0x14)); fputc(h[0x3e], f); fputc(0, f); fputc(0, f); fputc(0, f);
    put_le32(f, (uint32_t)size); put_le32(f, 1); if (fclose(f) != 0) return 0;
    path_join(path, sizeof(path), romfs, "preferences.ini"); f = fopen(path, "wb"); if (!f) return 0;
    fputs("DisplayFramerate=0\nForceLinearFilter=yes\nRumblePak=yes\nBatteryWarning=yes\nLargeROMBuffer=yes\nGuiColor=0\nStickMinDeadzone=0.200000\nStickMaxDeadzone=0.900000\nViewportType=0\nTVEnable=no\nTVLaced=no\nTVType=0\n\n", f);
    return fclose(f) == 0;
}
static int get_rom_name_publisher(const char *rom, char *romname, size_t cap, char *publisher, size_t pcap) {
    unsigned char h[ROM_HEADER_READ]; uint64_t size; char pub = 'N';
    if (!read_rom_header(rom, h, &size)) return 0;
    size_t j = 0; for (size_t i = 0x20; i < 0x34 && j + 1 < cap; ++i) { unsigned char c = h[i]; if (c == 0) break; romname[j++] = isprint(c) ? (char)c : ' '; }
    while (j && romname[j-1] == ' ') --j;
    romname[j] = 0;
    if (!j) strcpy(romname, "N64 Game");
    pub = (char)h[0x3b];
    const char *name = "DaedalusX64 Team";
    switch (pub) { case 'N': name="Nintendo"; break; case 'R': name="Rare"; break; case 'A': name="Acclaim"; break; case 'C': name="Capcom"; break; case 'E': name="Electronic Arts"; break; case 'H': name="Hudson Soft"; break; case 'K': name="Konami"; break; case 'T': name="Tecmo"; break; case 'W': name="Midway"; break; case 'S': name="Square"; break; case 'B': name="Bandai"; break; case 'L': name="Vitus"; break; case 'I': name="Interplay"; break; case 'U': name="Ubisoft"; break; case 'V': name="SEGA"; break; }
    snprintf(publisher, pcap, "%s", name); return 1;
}
static void normalize_key(const char *s, char *out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 1 < cap; ++i)
        if (isalnum((unsigned char)s[i])) out[j++] = (char)tolower((unsigned char)s[i]);
    out[j] = 0;
}
static void choose_preview(const char *folder, const char *romname, char *out, size_t cap) {
    const char *dir = "Data/Resources/Preview";
    char folderkey[512], romkey[512], first[PATH_CAP] = "", partial[PATH_CAP] = "";
    normalize_key(folder, folderkey, sizeof(folderkey)); normalize_key(romname, romkey, sizeof(romkey));
#ifdef _WIN32
    { char pat[PATH_CAP]; WIN32_FIND_DATAA e; HANDLE h; path_join(pat,sizeof(pat),dir,"*.png"); h=FindFirstFileA(pat,&e); if(h!=INVALID_HANDLE_VALUE){do{char base[512],key[512],p[PATH_CAP];snprintf(base,sizeof(base),"%s",e.cFileName);char *dot=strrchr(base,'.');if(dot)*dot=0;normalize_key(base,key,sizeof(key));path_join(p,sizeof(p),dir,e.cFileName);if(!first[0])strcpy(first,p);if((folderkey[0]&&!strcmp(key,folderkey))||(romkey[0]&&!strcmp(key,romkey))){strcpy(first,p);partial[0]=0;break;}if(!partial[0]&&((folderkey[0]&&(strstr(key,folderkey)||strstr(folderkey,key)))||(romkey[0]&&(strstr(key,romkey)||strstr(romkey,key)))))strcpy(partial,p);}while(FindNextFileA(h,&e));FindClose(h);} }
#else
    { DIR *d=opendir(dir);struct dirent *e;if(d){while((e=readdir(d))){size_t n=strlen(e->d_name);char base[512],key[512],p[PATH_CAP];if(n<4||strcasecmp(e->d_name+n-4,".png"))continue;snprintf(base,sizeof(base),"%s",e->d_name);char *dot=strrchr(base,'.');if(dot)*dot=0;normalize_key(base,key,sizeof(key));path_join(p,sizeof(p),dir,e->d_name);if(!first[0])strcpy(first,p);if((folderkey[0]&&!strcmp(key,folderkey))||(romkey[0]&&!strcmp(key,romkey))){strcpy(first,p);partial[0]=0;break;}if(!partial[0]&&((folderkey[0]&&(strstr(key,folderkey)||strstr(folderkey,key)))||(romkey[0]&&(strstr(key,romkey)||strstr(romkey,key)))))strcpy(partial,p);}closedir(d);} }
#endif
    if (partial[0]) snprintf(out,cap,"%s",partial); else if(first[0]) snprintf(out,cap,"%s",first); else snprintf(out,cap,"Data/Resources/logo.png");
}
static int choose_random_wav(const char *dir, char *out, size_t cap) {
    unsigned int seen=0;
#ifdef _WIN32
    { char pattern[PATH_CAP];WIN32_FIND_DATAA e;HANDLE h;path_join(pattern,sizeof(pattern),dir,"*.wav");h=FindFirstFileA(pattern,&e);if(h==INVALID_HANDLE_VALUE)return 0;do{if(!(e.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&rand()%(++seen)==0)path_join(out,cap,dir,e.cFileName);}while(FindNextFileA(h,&e));FindClose(h); }
#else
    { DIR *d=opendir(dir);struct dirent *e;if(!d)return 0;while((e=readdir(d))){size_t n=strlen(e->d_name);if(n<4||strcasecmp(e->d_name+n-4,".wav"))continue;if(rand()%(++seen)==0)path_join(out,cap,dir,e->d_name);}closedir(d); }
#endif
    return seen != 0;
}
static int prepare_banner(const char *rom, const char *folder, const char *romname, const char *publisher, const char *builddir) {
    char exe[PATH_CAP], outwav[PATH_CAP], audio[PATH_CAP], previewdir[PATH_CAP], bannerpng[PATH_CAP], iconpng[PATH_CAP], output[PATH_CAP];
    char toolpath[PATH_CAP], extracted[PATH_CAP]; char *audio_argv[8];
    snprintf(exe, sizeof(exe), "%s", tool("n64sfxdump.exe")); path_join(extracted, sizeof(extracted), builddir, "extracted_wavs"); mkdirs(extracted);
    audio_argv[0]=exe; audio_argv[1]=(char*)rom; audio_argv[2]=extracted; audio_argv[3]=NULL;
    /* Audio extractor failures are non-fatal: use the bundled default clip. */
    (void)run_process(audio_argv, ".");
    path_join(outwav, sizeof(outwav), builddir, "__aio_audio.wav");
    snprintf(audio, sizeof(audio), "Source/SysCTR/Resources/audio.wav");
    if (!copy_file(audio, outwav)) return 0;
    char selected_audio[PATH_CAP];
    if (choose_random_wav(extracted,selected_audio,sizeof(selected_audio)))
        (void)copy_file(selected_audio,outwav);
    snprintf(previewdir, sizeof(previewdir), "Data/Resources/Preview");
    { char leaf[600]; snprintf(leaf, sizeof(leaf), "%s_banner.png", folder); path_join(bannerpng, sizeof(bannerpng), builddir, leaf); }
    { char leaf[600]; snprintf(leaf, sizeof(leaf), "%s_icon.png", folder); path_join(iconpng, sizeof(iconpng), builddir, leaf); }
    /* Match the ROM title/folder to the closest bundled preview. */
    char preview[PATH_CAP];
    choose_preview(folder,romname,preview,sizeof(preview));
    (void)previewdir;
    char *ff_banner[] = {"ffmpeg", "-y", "-i", preview, "-vf", "scale=256:128", bannerpng, NULL};
    char *ff_icon[] = {"ffmpeg", "-y", "-i", preview, "-vf", "scale=48:48:force_original_aspect_ratio=increase,crop=48:48", iconpng, NULL};
    if (run_process(ff_banner, ".") != 0 || run_process(ff_icon, ".") != 0) {
        (void)copy_file(preview, bannerpng); (void)copy_file(preview, iconpng);
    }
    { char leaf[600]; snprintf(leaf, sizeof(leaf), "%s.bnr", folder); path_join(output, sizeof(output), builddir, leaf); } snprintf(toolpath,sizeof(toolpath),"%s",tool("bannertool.exe"));
    char *bnr[] = {toolpath,"makebanner","-i",bannerpng,"-a",outwav,"-o",output,NULL};
    if (!run_logged(bnr,".","create banner")) return 0;
    { char leaf[600]; snprintf(leaf, sizeof(leaf), "%s.smdh", folder); path_join(output, sizeof(output), builddir, leaf); }
    char shortname[17]; snprintf(shortname,sizeof(shortname),"%.16s",romname);
    char *smdh[] = {toolpath,"makesmdh","-s",shortname,"-l",(char*)romname,"-p",(char*)publisher,"-i",iconpng,"-o",output,NULL};
    if (!run_logged(smdh,".","create SMDH icon")) return 0;
    { char leaf[600]; snprintf(leaf, sizeof(leaf), "%s.wav", folder); path_join(output, sizeof(output), builddir, leaf); }
    return move_file(outwav,output);
}
static int ends_with_rom(const char *s) {
    const char *dot = strrchr(s, '.'); if (!dot) return 0;
    return !strcasecmp(dot,".z64") || !strcasecmp(dot,".n64") || !strcasecmp(dot,".v64") || !strcasecmp(dot,".rom");
}
static int path_basename_noext(const char *path, char *out, size_t cap) {
    const char *base = strrchr(path, '/'); const char *b2 = strrchr(path, '\\'); if (!base || (b2 && b2 > base)) base = b2; base = base ? base + 1 : path;
    size_t n = strlen(base); const char *dot = strrchr(base, '.'); if (dot) n = (size_t)(dot - base); if (n >= cap) return 0; memcpy(out, base, n); out[n] = 0; return 1;
}
static int convert_one(const char *rom, int seed, int debug_mode, int profile) {
    char rawname[512], folder[512], builddir[PATH_CAP], romfs[PATH_CAP], filepath[PATH_CAP], src[PATH_CAP], dst[PATH_CAP];
    char artifact_dir[PATH_CAP], artifact_3ds_dir[PATH_CAP];
    char romdir_path[PATH_CAP], rom_filename[520], stage_filename[600], header_filename[520], output_filename[520], rom_staging[PATH_CAP], rom_compressed[PATH_CAP];
    char romname[128], publisher[128], cmakefile[PATH_CAP], game_id_arg[64], toolpath[PATH_CAP], seedbuf[32];
    unsigned char rom_header[ROM_HEADER_READ]; uint64_t rom_size; uint16_t game_id;
    if (!path_basename_noext(rom, rawname, sizeof(rawname))) return 0;
    if (!read_rom_header(rom, rom_header, &rom_size)) {
        fprintf(stderr, "[AIO] invalid N64 ROM/header: %s\n", rom);
        return 0;
    }
    game_id = (uint16_t)(((uint16_t)rom_header[0x3C] << 8) | rom_header[0x3D]);
    snprintf(game_id_arg, sizeof(game_id_arg), "-DDAEDALUS_GAME_ID=%04X", (unsigned int)game_id);
    sanitize_folder(rawname, folder, sizeof(folder));
    /* Keep normal and diagnostics builds in separate CMake trees so CMake
       caches, staged ROMFS data, and generated binaries can never leak between
       modes. Preserve the historical normal-build directory and add a
       debug_ prefix only for the opt-in diagnostics build. */
    if (snprintf(builddir, sizeof(builddir), "%s%s%s",
            debug_mode ? "debug_" : "",
            profile == 2 ? "nintenstation_" : (profile == 1 ? "minimal_" : ""),
            folder) >= (int)sizeof(builddir)) {
        fprintf(stderr, "Build path is too long\n"); return 0;
    }
    if (debug_mode) snprintf(artifact_dir, sizeof(artifact_dir), profile == 2 ? "dist/debug/nintenstation643d" : (profile == 1 ? "dist/debug/minimal" : "dist/debug"));
    else snprintf(artifact_dir, sizeof(artifact_dir), profile == 2 ? "dist/nintenstation643d" : (profile == 1 ? "dist/minimal" : "dist"));
    path_join(artifact_3ds_dir, sizeof(artifact_3ds_dir), artifact_dir, "3ds");
    if (!artifact_3ds_dir[0]) return 0;
    path_join(romfs, sizeof(romfs), builddir, "romfs");
    if (!romfs[0]) { fprintf(stderr, "Build path is too long\n"); return 0; }
    printf("\n[AIO] === %s -> %s ===\n", rom, folder);
    if (!mkdirs("rom_locks") || !mkdirs("cmakedirs") || !mkdirs(artifact_3ds_dir) || !mkdirs("used")) return 0;
    if (!mkdirs(artifact_dir)) return 0;
    path_join(filepath,sizeof(filepath),"rom_locks",builddir); FILE *lock=fopen(filepath,"wb"); if(lock) fclose(lock);
    /* The old batch deleted its CMake cache and copied a clean ROMFS tree. */
    path_join(filepath, sizeof(filepath), builddir, "CMakeCache.txt");
    if (!filepath[0]) goto fail;
    remove(filepath);
    if (!mkdirs(romfs)) goto fail;
    snprintf(src,sizeof(src),"Source/SysCTR/Resources/romfs");
    if (is_dir(src) && !copy_tree(src,romfs)) goto fail;
    snprintf(src,sizeof(src),"Data/roms.ini"); if (is_file(src)) { path_join(dst,sizeof(dst),romfs,"roms.ini"); if(!copy_file(src,dst)) goto fail; }
    snprintf(src,sizeof(src),"Source/SysCTR/Resources/template2.rsf");
    snprintf(filepath, sizeof(filepath), "%s.rsf", folder);
    path_join(dst, sizeof(dst), builddir, filepath);
    if (!dst[0] || !write_rsf(src,dst,folder,game_id)) goto fail;
    path_join(romdir_path, sizeof(romdir_path), romfs, "Roms");
    if (!romdir_path[0] || !mkdirs(romdir_path)) goto fail;
    snprintf(stage_filename, sizeof(stage_filename), "%s.n64.stage", folder);
    path_join(rom_staging, sizeof(rom_staging), romdir_path, stage_filename);
    snprintf(rom_filename, sizeof(rom_filename), "%s.n64.lz4", folder);
    path_join(rom_compressed, sizeof(rom_compressed), romdir_path, rom_filename);
    /* Keep byte-order normalization before compression; the ROM loader then
       reads independent LZ4 blocks without expanding the image into RAM. */
    if (!rom_staging[0] || !normalize_rom_to_n64(rom,rom_staging)) goto fail;
    printf("[AIO] normalized ROM to CTR-native .n64 byte order\n");
    snprintf(dst, sizeof(dst), "%s", rom_staging);
    snprintf(header_filename, sizeof(header_filename), "%s.h", folder);
    if (seed > 0) {
        path_join(filepath, sizeof(filepath), romdir_path, header_filename);
        if (!filepath[0]) goto fail;
        snprintf(toolpath,sizeof(toolpath),"%s",tool("gbmin.exe"));
        char *gbcreate[]={toolpath,"-c",dst,filepath,"0",NULL}; if(!run_logged(gbcreate,".","make randomized ROM header")) goto fail;
        snprintf(seedbuf,sizeof(seedbuf),"0.000000000%d",seed); char *gbrandom[]={toolpath,"-g",filepath,dst,seedbuf,NULL}; if(!run_logged(gbrandom,".","randomize ROM")) goto fail;
    }
    if (!compress_lz4_frame(rom_staging,rom_compressed,rom_size)) { fprintf(stderr,"[AIO] LZ4 compression failed\\n"); goto fail; }
    remove(rom_staging);
    printf("[AIO] stored game ROM as independent 64 KiB LZ4 frame (HC level 10, favor decode speed)\\n");
    if (!generate_metadata(rom,folder,romfs)) goto fail;
    (void)get_rom_name_publisher(rom,romname,sizeof(romname),publisher,sizeof(publisher));
    if (!prepare_banner(rom,folder,romname,publisher,builddir)) goto fail;
    if (seed > 0) {
        path_join(src, sizeof(src), romdir_path, header_filename);
        path_join(dst, sizeof(dst), builddir, header_filename);
        if (!src[0] || !dst[0]) goto fail;
        if (is_file(src) && !move_file(src,dst)) goto fail;
    }
    snprintf(toolpath,sizeof(toolpath),"%s",tool("3dstool.exe"));
    snprintf(output_filename, sizeof(output_filename), "%s.bin", folder);
    path_join(filepath, sizeof(filepath), builddir, output_filename);
    if (!filepath[0]) goto fail;
    char *pack[] = {toolpath,"-c","--type","romfs","--romfs-dir",romfs,"--file",filepath,NULL};
    if(!run_logged(pack,".","pack RomFS")) goto fail;
    remove(filepath);
    snprintf(cmakefile,sizeof(cmakefile),"-DPROJECT_NAME=%s",folder);
    printf("[AIO] detected ROM CartID %04X (%llu bytes); passing it to the compiler\n", (unsigned int)game_id, (unsigned long long)rom_size);
    char *cmake[20]; int cmake_argc = 0;
    cmake[cmake_argc++] = "cmake";
    cmake[cmake_argc++] = cmakefile;
    cmake[cmake_argc++] = "-DCTR_RELEASE=1";
    cmake[cmake_argc++] = game_id_arg;
    cmake[cmake_argc++] = debug_mode ? "-DDAEDALUS_ENABLE_SDMC_DIAGNOSTICS=ON" : "-DDAEDALUS_ENABLE_SDMC_DIAGNOSTICS=OFF";
    cmake[cmake_argc++] = debug_mode ? "-DDAEDALUS_ENABLE_FUNCTION_PROFILING=ON" : "-DDAEDALUS_ENABLE_FUNCTION_PROFILING=OFF";
    cmake[cmake_argc++] = debug_mode ? "-DDAEDALUS_ENABLE_DEBUG_CONSOLE=ON" : "-DDAEDALUS_ENABLE_DEBUG_CONSOLE=OFF";
    cmake[cmake_argc++] = profile != 0 ? "-DDAEDALUS_MINIMAL_EMULATOR=ON" : "-DDAEDALUS_MINIMAL_EMULATOR=OFF";
    cmake[cmake_argc++] = profile == 2 ? "-DDAEDALUS_NINTENSTATION643D=ON" : "-DDAEDALUS_NINTENSTATION643D=OFF";
    cmake[cmake_argc++] = "-DCMAKE_TOOLCHAIN_FILE=../Tools/3dstoolchain.cmake";
    cmake[cmake_argc++] = "-G";
    cmake[cmake_argc++] = "Unix Makefiles";
    cmake[cmake_argc++] = "../Source";
    cmake[cmake_argc] = NULL;
    if(!run_logged(cmake,builddir,debug_mode ? "configure 3DS diagnostics/profiling build" : "configure optimized 3DS build")) goto fail;
    if (profile == 2 &&
        (!cache_option_is_on(builddir, "DAEDALUS_MINIMAL_EMULATOR") ||
         !cache_option_is_on(builddir, "DAEDALUS_NINTENSTATION643D"))) {
        fprintf(stderr, "[AIO] refusing to package: CMake did not enable both Nintenstation and minimal profiles\n");
        goto fail;
    }
    printf("[AIO] build mode: %s%s (separate CMake tree: %s)\n",
        profile == 2 ? "Nintenstation643D " : (profile == 1 ? "minimal " : ""),
        debug_mode ? "debug diagnostics/profiling" : "optimized", builddir);
    char *make[]={"make","-j4",NULL}; if(!run_logged(make,builddir,"build CIA and 3DSX")) goto fail;
    snprintf(output_filename, sizeof(output_filename), "%s.cia", folder);
    path_join(src, sizeof(src), builddir, output_filename);
    path_join(dst, sizeof(dst), artifact_dir, output_filename);
    if (!src[0] || !dst[0] || !is_file(src) || !move_file(src,dst)) goto fail;
    snprintf(output_filename, sizeof(output_filename), "%s.3dsx", folder);
    path_join(src, sizeof(src), builddir, output_filename);
    path_join(filepath, sizeof(filepath), artifact_3ds_dir, output_filename);
    if (!src[0] || !filepath[0] || !is_file(src) || !move_file(src,filepath)) goto fail;
    const char *rom_basename = strrchr(rom, '/');
    const char *rom_basename_backslash = strrchr(rom, '\\');
    if (!rom_basename || (rom_basename_backslash && rom_basename_backslash > rom_basename))
        rom_basename = rom_basename_backslash;
    rom_basename = rom_basename ? rom_basename + 1 : rom;
    path_join(dst, sizeof(dst), "used", rom_basename);
    if(!move_file(rom,dst)) fprintf(stderr,"[AIO] warning: output built, but could not move source ROM to %s\n",dst);
    path_join(filepath,sizeof(filepath),"rom_locks",builddir); remove(filepath);
    path_join(dst,sizeof(dst),"cmakedirs",builddir);
#ifdef _WIN32
    if (!MoveFileExA(builddir,dst,MOVEFILE_COPY_ALLOWED)) fprintf(stderr,"[AIO] warning: could not archive build tree at %s\n",dst);
#else
    if (rename(builddir,dst) != 0) fprintf(stderr,"[AIO] warning: could not archive build tree at %s\n",dst);
#endif
    printf("[AIO] done: %s; package: %s/%s.cia\n",folder,artifact_dir,folder); return 1;
fail:
    path_join(filepath,sizeof(filepath),"rom_locks",builddir); remove(filepath);
    fprintf(stderr,"[AIO] conversion failed: %s (kept work files in %s)\n",folder,builddir); return 0;
}
static int scan_roms(const char *dir) {
#ifdef _WIN32
    char p[PATH_CAP]; WIN32_FIND_DATAA e; HANDLE h; path_join(p,sizeof(p),dir,"*"); h=FindFirstFileA(p,&e); if(h==INVALID_HANDLE_VALUE)return 1;
    do { char f[PATH_CAP],name[128],pub[128]; unsigned char head[ROM_HEADER_READ]; uint64_t sz; if(e.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue; if(!ends_with_rom(e.cFileName))continue; path_join(f,sizeof(f),dir,e.cFileName); if(read_rom_header(f,head,&sz)){get_rom_name_publisher(f,name,sizeof(name),pub,sizeof(pub));printf("%s | %08x%08x-%02x | %s\n",e.cFileName,be32(head+0x10),be32(head+0x14),head[0x3e],name);} }while(FindNextFileA(h,&e)); FindClose(h); return 0;
#else
    DIR *d=opendir(dir); struct dirent *e; if(!d){perror(dir);return 1;} while((e=readdir(d))){char f[PATH_CAP],name[128],pub[128];unsigned char head[ROM_HEADER_READ];uint64_t sz;if(!ends_with_rom(e->d_name))continue;path_join(f,sizeof(f),dir,e->d_name);if(read_rom_header(f,head,&sz)){get_rom_name_publisher(f,name,sizeof(name),pub,sizeof(pub));printf("%s | %08x%08x-%02x | %s\n",e->d_name,be32(head+0x10),be32(head+0x14),head[0x3e],name);}}closedir(d);return 0;
#endif
}

/* Unified romconvert command line: this file owns ROM conversion and build orchestration. */
static int enter_daed_dir(void) {
    if (is_file("daed/Source/CMakeLists.txt")) {
#ifdef _WIN32
        return SetCurrentDirectoryA("daed") != 0;
#else
        return chdir("daed") == 0;
#endif
    }
    return is_file("Source/CMakeLists.txt");
}
static int mode_setup(const char *root) {
    const char *dkp = getenv("DEVKITPRO");
    char pica[PATH_CAP], imgui[PATH_CAP];
    if (!dkp || !*dkp) { fprintf(stderr,"error: DEVKITPRO is not set; install devkitPro first\n"); return 2; }
    path_join(pica,sizeof(pica),root,"deps/picaGL"); path_join(imgui,sizeof(imgui),root,"deps/imgui-picagl");
    if (!is_file("deps/picaGL/Makefile") || !is_file("deps/imgui-picagl/Makefile")) {
        fprintf(stderr,"error: bundled picaGL/imgui-picagl Makefile is missing\n"); return 2;
    }
    { char *a[]={"make","-C",pica,"install",NULL}; if(!run_logged(a,root,"install picaGL"))return 1; }
    { char *a[]={"make","-C",imgui,"install",NULL}; if(!run_logged(a,root,"install imgui-picagl"))return 1; }
    puts(g_dry_run ? "Dry run complete; no support libraries were installed." : "3DS support libraries installed."); return 0;
}
static int mode_build3ds(const char *root,int jobs,int profile) {
    char src[PATH_CAP],build[PATH_CAP],toolchain[PATH_CAP],toolarg[PATH_CAP+64],jobs_s[16];
    const int minimal_emulator = profile != 0;
    const char *build_tree = profile == 2 ? "build/3ds-nintenstation643d" :
        (profile == 1 ? "build/3ds-minimal" : "build/3ds");
    path_join(src,sizeof(src),root,"Source");
    path_join(build,sizeof(build),root,build_tree);
    path_join(toolchain,sizeof(toolchain),root,"Tools/3dstoolchain.cmake");
    if(!is_file(toolchain)){fprintf(stderr,"error: missing 3DS toolchain %s\n",toolchain);return 2;}
    snprintf(toolarg,sizeof(toolarg),"-DCMAKE_TOOLCHAIN_FILE=%s",toolchain); snprintf(jobs_s,sizeof(jobs_s),"%d",jobs);
    { char *a[]={"cmake","-S",src,"-B",build,"-DCTR_RELEASE=ON",
        minimal_emulator ? "-DDAEDALUS_MINIMAL_EMULATOR=ON" : "-DDAEDALUS_MINIMAL_EMULATOR=OFF",
        profile == 2 ? "-DDAEDALUS_NINTENSTATION643D=ON" : "-DDAEDALUS_NINTENSTATION643D=OFF",toolarg,NULL};
      const char *description = profile == 2 ? "configure Nintenstation643D build" :
          (profile == 1 ? "configure minimal-touch 3DS build" : "configure Daedalus 3DS build");
      if(!run_logged(a,root,description))return 1; }
    { char *a[]={"cmake","--build",build,"--target","N3DS","--parallel",jobs_s,NULL}; if(!run_logged(a,root,"build Daedalus 3DS"))return 1; }
    if(g_dry_run) puts("Dry run complete; no 3DS build was performed."); else printf("3DS build finished; inspect %s\n",build); return 0;
}
static int mode_build_recomp(const char *root,int jobs) {
    char source[PATH_CAP],build[PATH_CAP],jobs_s[16];
    path_join(source,sizeof(source),root,"deps/N64Recomp"); path_join(build,sizeof(build),source,"build");
    if(!is_file("deps/N64Recomp/CMakeLists.txt")){
        char *clone[]={"git","clone","--depth","1","--recurse-submodules","https://github.com/N64Recomp/N64Recomp.git",source,NULL};
        if(!run_logged(clone,root,"clone N64Recomp"))return 1;
        if(g_dry_run){puts("Dry run complete; no checkout was cloned and no build was performed.");return 0;}
    }
    {char *a[]={"cmake","-S",source,"-B",build,NULL};if(!run_logged(a,root,"configure N64Recomp"))return 1;}
    snprintf(jobs_s,sizeof(jobs_s),"%d",jobs);
    {char *a[]={"cmake","--build",build,"--target","N64RecompCLI","--parallel",jobs_s,NULL};if(!run_logged(a,root,"build N64Recomp CLI"))return 1;}
    if(g_dry_run) puts("Dry run complete; N64Recomp was not built."); else printf("N64Recomp built under %s\n",build);return 0;
}
static int mode_downloadplay(const char *root,const char *rom,int jobs) {
    char id[8],stem[256],work[PATH_CAP],packed[PATH_CAP],stage[PATH_CAP],childbuild[PATH_CAP],hostbuild[PATH_CAP],toolchain[PATH_CAP],src[PATH_CAP],dist[PATH_CAP],cmakearg[PATH_CAP+64],jobs_s[16];
    unsigned char hdr[ROM_HEADER_READ]; uint64_t size;
    if(!rom||!*rom||!is_file(rom)){fprintf(stderr,"error: downloadplay requires --rom PATH to a valid ROM\n");return 2;}
    if(!read_rom_header(rom,hdr,&size)){fprintf(stderr,"error: invalid N64 ROM %s\n",rom);return 2;}
    snprintf(id,sizeof(id),"%02X%02X",hdr[0x3e],hdr[0x3f]);
    if(!path_basename_noext(rom,stem,sizeof(stem))){fprintf(stderr,"error: invalid ROM filename\n");return 2;} sanitize_folder(stem,stem,sizeof(stem));
    { char relative[512]; snprintf(relative,sizeof(relative),"build/downloadplay/%s",stem); path_join(work,sizeof(work),root,relative); }
    path_join(stage,sizeof(stage),work,"input/normalized.n64"); path_join(packed,sizeof(packed),work,"input/game.n64.lz4");
    if(g_dry_run){printf("Would normalize %s, create %s, build a <=32 MiB child CIA and bundle it into a host CIA under dist/downloadplay/%s\n",rom,packed,stem);return 0;}
    if(!mkdirs(work))return 1;
    { char inputdir[PATH_CAP]; path_join(inputdir,sizeof(inputdir),work,"input"); if(!mkdirs(inputdir))return 1; }
    if(!normalize_rom_to_n64(rom,stage)||!compress_lz4_frame(stage,packed,size)){fprintf(stderr,"error: failed to normalize/compress ROM\n");return 1;} remove(stage);
    path_join(toolchain,sizeof(toolchain),root,"Tools/3dstoolchain.cmake"); if(!is_file(toolchain)){fprintf(stderr,"error: missing 3DS toolchain %s\n",toolchain);return 2;}
    path_join(src,sizeof(src),root,"Source"); path_join(childbuild,sizeof(childbuild),work,"child-build"); path_join(hostbuild,sizeof(hostbuild),work,"host-build"); snprintf(jobs_s,sizeof(jobs_s),"%d",jobs);
    snprintf(cmakearg,sizeof(cmakearg),"-DCMAKE_TOOLCHAIN_FILE=%s",toolchain);
    {char gamearg[64],romarg[PATH_CAP+64];snprintf(gamearg,sizeof(gamearg),"-DDAEDALUS_GAME_ID=%s",id);snprintf(romarg,sizeof(romarg),"-DDAEDALUS_DOWNLOADPLAY_ROM=%s",packed);
     char *a[]={"cmake","-S",src,"-B",childbuild,"-DPROJECT_NAME=DaedalusX64","-DCTR_RELEASE=ON",gamearg,"-DDAEDALUS_DOWNLOADPLAY=ON",romarg,cmakearg,NULL};
     if(!run_logged(a,root,"configure Download Play child"))return 1;}
    {char *a[]={"cmake","--build",childbuild,"--target","N3DS","--parallel",jobs_s,NULL};if(!run_logged(a,root,"build Download Play child"))return 1;}
    {char childcia[PATH_CAP];path_join(childcia,sizeof(childcia),childbuild,"DaedalusX64.cia");if(!is_file(childcia)){fprintf(stderr,"error: child CIA was not produced\n");return 1;}
     {struct stat st;if(stat(childcia,&st)||st.st_size>32LL*1024*1024){fprintf(stderr,"error: child CIA exceeds 32 MiB or cannot be read\n");return 1;}}
     {char gamearg[64],bundlearg[PATH_CAP+64];snprintf(gamearg,sizeof(gamearg),"-DDAEDALUS_GAME_ID=%s",id);snprintf(bundlearg,sizeof(bundlearg),"-DDAEDALUS_DOWNLOADPLAY_CHILD_CIA=%s",childcia);
      char *a[]={"cmake","-S",src,"-B",hostbuild,"-DPROJECT_NAME=DaedalusX64","-DCTR_RELEASE=ON",gamearg,bundlearg,cmakearg,NULL};if(!run_logged(a,root,"configure Download Play host"))return 1;}
     {char *a[]={"cmake","--build",hostbuild,"--target","N3DS","--parallel",jobs_s,NULL};if(!run_logged(a,root,"build Download Play host"))return 1;}
     {char hostcia[PATH_CAP],childout[PATH_CAP],hostout[PATH_CAP];path_join(hostcia,sizeof(hostcia),hostbuild,"DaedalusX64.cia");if(!is_file(hostcia)){fprintf(stderr,"error: host CIA was not produced\n");return 1;}
      snprintf(dist,sizeof(dist),"dist/downloadplay/%s",stem);if(!mkdirs(dist))return 1;path_join(childout,sizeof(childout),dist,"downloadplay-child.cia");path_join(hostout,sizeof(hostout),dist,"DaedalusX64.cia");
      if(!copy_file(childcia,childout)||!copy_file(hostcia,hostout)){fprintf(stderr,"error: could not stage Download Play CIAs\n");return 1;}
      printf("Staged child and host CIA packages in %s. The host/runtime Download Play transmission still requires console validation.\n",dist);}}
    return 0;
}
static void usage(const char *exe) {
    printf("romconvert - unified DaedalusX64 ROM/build tool\nUsage: %s [command] [options]\n",exe);
    puts("Commands: (no command) convert ROMs; nintenstation643d convert ROM batch with Nintenstation profile; setup; build-3ds; minimal; debug; downloadplay --rom PATH; build-n64recomp; recompile --rom PATH --profile FILE");
    puts("Conversion options: --roms DIR (default roms), --seed N, --minimal, --nintenstation643d; add debug for diagnostic per-ROM builds.");
    puts("Build options: --root PATH, --jobs N, --dry-run. 'minimal' builds the minimal main CIA; use 'build-3ds --nintenstation643d' for a standalone profile build.");
    puts("GUI: --gui opens the optional native Windows interface; the command-line interface is unchanged.");
}
static int is_mode(const char *s) {
    return !strcmp(s,"setup")||!strcmp(s,"build-3ds")||!strcmp(s,"minimal")||!strcmp(s,"nintenstation643d")||!strcmp(s,"build-n64recomp")||!strcmp(s,"debug")||!strcmp(s,"downloadplay")||!strcmp(s,"recompile");
}
#ifdef _WIN32
/* Optional native Windows front-end. The normal command-line interface remains
 * unchanged; the GUI starts the same executable in a worker process and shows
 * its stdout/stderr in the log pane. */
#define GUI_WM_LOG (WM_APP + 41)
#define GUI_WM_DONE (WM_APP + 42)
#define GUI_ID_ROOT  100
#define GUI_ID_ROMS  101
#define GUI_ID_FIRST 200
#define GUI_ID_RUN   1
#define GUI_ID_DEBUG 2
#define GUI_ID_NINTEN 3
#define GUI_ID_SETUP 4
#define GUI_ID_BUILD 5
#define GUI_ID_MINIMAL 6
#define GUI_ID_RECOMP 7
static HWND gui_window, gui_root_edit, gui_roms_edit, gui_log_edit;
static HWND gui_buttons[7];
static HANDLE gui_worker_handle;
static const int gui_button_ids[7] = {GUI_ID_RUN, GUI_ID_DEBUG, GUI_ID_NINTEN,
    GUI_ID_SETUP, GUI_ID_BUILD, GUI_ID_MINIMAL, GUI_ID_RECOMP};
static const char *gui_button_labels[7] = {"Convert ROMs", "Debug convert",
    "Nintenstation batch", "Setup libraries", "Build 3DS", "Build minimal", "Build N64Recomp"};

typedef struct { HWND window; char mode[32]; char root[PATH_CAP]; char roms[PATH_CAP]; } GuiJob;
typedef struct { int exit_code; } GuiResult;

static void gui_append(const char *text) {
    if (!gui_log_edit || !text) return;
    SendMessageA(gui_log_edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageA(gui_log_edit, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageA(gui_log_edit, EM_SCROLLCARET, 0, 0);
}
static void gui_set_running(int running) {
    size_t i;
    for (i = 0; i < 7; ++i) EnableWindow(gui_buttons[i], !running);
}
static DWORD WINAPI gui_run_job(LPVOID opaque) {
    GuiJob *job = (GuiJob *)opaque;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE read_pipe = NULL, write_pipe = NULL, nul = INVALID_HANDLE_VALUE;
    char exe[PATH_CAP], cmd[PATH_CAP * 3], buffer[1024];
    DWORD got = 0, exit_code = 1;
    BOOL ok;
    GuiResult *result;
    memset(&sa, 0, sizeof(sa)); sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) goto done;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe; si.hStdError = write_pipe;
    nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    si.hStdInput = nul;
    if (!GetModuleFileNameA(NULL, exe, sizeof(exe))) goto done;
    if (!strcmp(job->mode, "convert"))
        _snprintf(cmd, sizeof(cmd), "\"%s\" --root \"%s\" --roms \"%s\"", exe, job->root, job->roms);
    else if (!strcmp(job->mode, "debug") || !strcmp(job->mode, "nintenstation643d"))
        _snprintf(cmd, sizeof(cmd), "\"%s\" %s --root \"%s\" --roms \"%s\"", exe, job->mode, job->root, job->roms);
    else
        _snprintf(cmd, sizeof(cmd), "\"%s\" %s --root \"%s\"", exe, job->mode, job->root);
    cmd[sizeof(cmd)-1] = 0;
    ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
        NULL, job->root, &si, &pi);
    CloseHandle(write_pipe); write_pipe = NULL;
    if (nul != INVALID_HANDLE_VALUE) { CloseHandle(nul); nul = INVALID_HANDLE_VALUE; }
    if (!ok) {
        const char *msg = "Could not start romconvert worker process.\r\n";
        char *copy = (char *)malloc(strlen(msg) + 1);
        if (copy) { strcpy(copy, msg); PostMessageA(job->window, GUI_WM_LOG, 0, (LPARAM)copy); }
        goto done;
    }
    while (ReadFile(read_pipe, buffer, sizeof(buffer)-1, &got, NULL) && got) {
        char *copy = (char *)malloc(got + 1);
        if (copy) { memcpy(copy, buffer, got); copy[got] = 0; PostMessageA(job->window, GUI_WM_LOG, 0, (LPARAM)copy); }
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
done:
    if (write_pipe) CloseHandle(write_pipe);
    if (read_pipe) CloseHandle(read_pipe);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    result = (GuiResult *)malloc(sizeof(*result));
    if (result) { result->exit_code = (int)exit_code; PostMessageA(job->window, GUI_WM_DONE, 0, (LPARAM)result); }
    free(job);
    return 0;
}
static void gui_start_job(int which) {
    GuiJob *job;
    DWORD tid;
    if (gui_worker_handle) return;
    job = (GuiJob *)calloc(1, sizeof(*job));
    if (!job) { gui_append("Out of memory.\r\n"); return; }
    job->window = gui_window;
    GetWindowTextA(gui_root_edit, job->root, sizeof(job->root));
    GetWindowTextA(gui_roms_edit, job->roms, sizeof(job->roms));
    if (!job->root[0]) { gui_append("Enter the project root folder first.\r\n"); free(job); return; }
    if ((which <= GUI_ID_NINTEN) && !job->roms[0]) { gui_append("Enter the ROM folder first.\r\n"); free(job); return; }
    switch (which) {
        case GUI_ID_RUN: strcpy(job->mode, "convert"); break;
        case GUI_ID_DEBUG: strcpy(job->mode, "debug"); break;
        case GUI_ID_NINTEN: strcpy(job->mode, "nintenstation643d"); break;
        case GUI_ID_SETUP: strcpy(job->mode, "setup"); break;
        case GUI_ID_BUILD: strcpy(job->mode, "build-3ds"); break;
        case GUI_ID_MINIMAL: strcpy(job->mode, "minimal"); break;
        default: strcpy(job->mode, "build-n64recomp"); break;
    }
    gui_append("\r\n> Starting "); gui_append(job->mode); gui_append("...\r\n");
    gui_set_running(1);
    gui_worker_handle = CreateThread(NULL, 0, gui_run_job, job, 0, &tid);
    if (!gui_worker_handle) { gui_set_running(0); free(job); gui_append("Could not create worker thread.\r\n"); }
}
static LRESULT CALLBACK gui_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    int i;
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE inst = ((LPCREATESTRUCTA)lp)->hInstance;
        CreateWindowA("STATIC", "Project root:", WS_CHILD|WS_VISIBLE, 12, 14, 90, 22, hwnd, NULL, inst, NULL);
        gui_root_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 105, 10, 640, 24, hwnd, (HMENU)GUI_ID_ROOT, inst, NULL);
        CreateWindowA("STATIC", "ROM folder:", WS_CHILD|WS_VISIBLE, 12, 46, 90, 22, hwnd, NULL, inst, NULL);
        gui_roms_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "roms", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 105, 42, 640, 24, hwnd, (HMENU)GUI_ID_ROMS, inst, NULL);
        for (i=0; i<7; ++i) {
            int x = 12 + (i%4)*184, y = 78 + (i/4)*36;
            gui_buttons[i] = CreateWindowA("BUTTON", gui_button_labels[i], WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                x, y, 174, 28, hwnd, (HMENU)(INT_PTR)gui_button_ids[i], inst, NULL);
        }
        gui_log_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL|WS_HSCROLL,
            12, 154, 733, 330, hwnd, NULL, inst, NULL);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) gui_start_job(LOWORD(wp));
        return 0;
    case GUI_WM_LOG: {
        char *text = (char *)lp; gui_append(text); free(text); return 0;
    }
    case GUI_WM_DONE: {
        GuiResult *r = (GuiResult *)lp;
        char message[96];
        _snprintf(message, sizeof(message), "\r\n> Finished (exit code %d).\r\n", r ? r->exit_code : -1);
        message[sizeof(message)-1]=0; gui_append(message); free(r);
        if (gui_worker_handle) { CloseHandle(gui_worker_handle); gui_worker_handle = NULL; }
        gui_set_running(0); return 0;
    }
    case WM_CLOSE:
        if (gui_worker_handle) { gui_append("Wait for the active command to finish before closing.\r\n"); return 0; }
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static int run_gui(void) {
    WNDCLASSA wc;
    MSG msg;
    char cwd[PATH_CAP];
    HINSTANCE inst = GetModuleHandleA(NULL);
    FreeConsole();
    memset(&wc,0,sizeof(wc)); wc.lpfnWndProc=gui_wndproc; wc.hInstance=inst;
    wc.lpszClassName="DaedalusRomconvertGui"; wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    if (!RegisterClassA(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 2;
    gui_window=CreateWindowA(wc.lpszClassName,"PN643D romconvert",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,780,540,NULL,NULL,inst,NULL);
    if (!gui_window) return 2;
    if (GetCurrentDirectoryA(sizeof(cwd),cwd)) SetWindowTextA(gui_root_edit,cwd);
    ShowWindow(gui_window,SW_SHOW); UpdateWindow(gui_window);
    while (GetMessageA(&msg,NULL,0,0)>0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    return (int)msg.wParam;
}
#else
static int run_gui(void) {
    fputs("romconvert: --gui is available only in Windows builds.\n", stderr);
    return 2;
}
#endif

int main(int argc,char **argv) {
    int gui_i;
    for (gui_i = 1; gui_i < argc; ++gui_i) if (!strcmp(argv[gui_i], "--gui")) return run_gui();
    char cwd[PATH_CAP],root[PATH_CAP],romdir[PATH_CAP]="roms",scan[PATH_CAP]="",rom[PATH_CAP]="",profile[PATH_CAP]="",recomp[PATH_CAP]="";
    const char *command="convert"; int seed=0,jobs=2,dry=0,debug_mode=0,romdir_set=0;
    int i,started_at_root;
    for(i=1;i<argc;i++) if(is_mode(argv[i])) { command=argv[i]; break; }
#ifdef _WIN32
    if(!_getcwd(cwd,sizeof(cwd))){perror("getcwd");return 2;}
#else
    if(!getcwd(cwd,sizeof(cwd))){perror("getcwd");return 2;}
#endif
    snprintf(root,sizeof(root),"%s",cwd);
    if(argc>1&&(!strcmp(argv[1],"--help")||!strcmp(argv[1],"-h"))){usage(argv[0]);return 0;}
    for(i=1;i<argc;i++){
        const char *a=argv[i];
        if(is_mode(a))continue;
        if(!strcmp(a,"--help")||!strcmp(a,"-h")){usage(argv[0]);return 0;}
        if(!strcmp(a,"--dry-run")){dry=1;continue;}
        if(!strcmp(a,"--minimal")){g_minimal_emulator=1;continue;}
        if(!strcmp(a,"--nintenstation643d")){g_nintenstation643d=1;g_minimal_emulator=1;continue;}
        if(!strcmp(a,"debug")||!strcmp(a,"--debug")){debug_mode=1;continue;}
        if(!strcmp(a,"--root")&&i+1<argc){snprintf(root,sizeof(root),"%s",argv[++i]);continue;}
        if(!strcmp(a,"--rom")&&i+1<argc){snprintf(rom,sizeof(rom),"%s",argv[++i]);continue;}
        if(!strcmp(a,"--roms")&&i+1<argc){snprintf(romdir,sizeof(romdir),"%s",argv[++i]);romdir_set=1;continue;}
        if(!strcmp(a,"--scan")&&i+1<argc){snprintf(scan,sizeof(scan),"%s",argv[++i]);continue;}
        if(!strcmp(a,"--profile")&&i+1<argc){snprintf(profile,sizeof(profile),"%s",argv[++i]);continue;}
        if(!strcmp(a,"--recompiler")&&i+1<argc){snprintf(recomp,sizeof(recomp),"%s",argv[++i]);continue;}
        if(!strcmp(a,"--jobs")&&i+1<argc){jobs=atoi(argv[++i]);if(jobs<1||jobs>256){fprintf(stderr,"error: --jobs must be 1..256\n");return 2;}continue;}
        if(!strcmp(a,"--seed")&&i+1<argc){seed=atoi(argv[++i]);if(seed<0){fprintf(stderr,"error: --seed must be nonnegative\n");return 2;}continue;}
        fprintf(stderr,"error: unknown/incomplete option: %s\n",a);usage(argv[0]);return 2;
    }
    if(!strcmp(command,"nintenstation643d")){
        /* Match the normal ROM batch workflow, but build each ROM with the
           Nintenstation profile. Use `build-3ds --nintenstation643d` when a
           standalone, non-ROM-specific profile build is desired. */
        g_nintenstation643d=1;
        g_minimal_emulator=1;
        command="convert";
    }
    g_dry_run=dry;
    if(!strcmp(command,"convert")&&dry){fprintf(stderr,"warning: --dry-run is not supported for full ROM conversion; no dry-run conversion performed\n");return 2;}
    if(!strcmp(root,cwd)){
        if(is_file("daed/Source/CMakeLists.txt")){path_join(root,sizeof(root),cwd,"daed");}
    }
    /* Run all repository-relative tools and build commands from the checkout root. */
#ifdef _WIN32
    if(!SetCurrentDirectoryA(root)){fprintf(stderr,"error: cannot enter repository root %s\n",root);return 2;}
#else
    if(chdir(root)!=0){perror(root);return 2;}
#endif
#ifdef _WIN32
    if(!_getcwd(root,sizeof(root))){perror("getcwd");return 2;}
#else
    if(!getcwd(root,sizeof(root))){perror("getcwd");return 2;}
#endif
    started_at_root=is_file("Source/CMakeLists.txt");
    if(!started_at_root){fprintf(stderr,"error: repository root must contain Source/CMakeLists.txt\n");return 2;}
    if(!strcmp(command,"setup"))return mode_setup(root);
    if(!strcmp(command,"build-3ds"))return mode_build3ds(root,jobs,g_nintenstation643d ? 2 : (g_minimal_emulator ? 1 : 0));
    if(!strcmp(command,"minimal"))return mode_build3ds(root,jobs,1);
    if(!strcmp(command,"build-n64recomp"))return mode_build_recomp(root,jobs);
    if(!strcmp(command,"downloadplay"))return mode_downloadplay(root,rom,jobs);
    if(!strcmp(command,"recompile")){
        const char *exe=recomp[0]?recomp:getenv("N64RECOMP");
        if(!rom[0]||!profile[0]){fprintf(stderr,"error: recompile requires --rom PATH and --profile FILE\n");return 2;}
        if(!exe||!*exe){fprintf(stderr,"error: N64Recomp not found; run build-n64recomp or pass --recompiler PATH\n");return 2;}
        {char *a[]={(char*)exe,profile,NULL};int rc=run_logged(a,root,"generate native sources with N64Recomp");if(rc) return 1;}
        if(g_dry_run) return 0;
        puts("N64Recomp source generation completed. A game-specific runtime/Daedalus adapter is required to build a playable 3DS package.");return 0;
    }
    if(!strcmp(command,"debug"))debug_mode=1;
    if(scan[0])return scan_roms(scan);
    srand((unsigned int)time(NULL));
    if(!enter_daed_dir()){fprintf(stderr,"error: Run from the repository root or daed/ directory.\n");return 2;}
    snprintf(romdir,sizeof(romdir),"%s",romdir_set?romdir:"roms");
#ifdef _WIN32
    {WIN32_FIND_DATAA e;char pat[PATH_CAP];HANDLE h;int found=0,failed=0;path_join(pat,sizeof(pat),romdir,"*");h=FindFirstFileA(pat,&e);if(h==INVALID_HANDLE_VALUE){fprintf(stderr,"ROM directory unavailable: %s\n",romdir);return 2;}do{char p[PATH_CAP];if((e.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||!ends_with_rom(e.cFileName))continue;path_join(p,sizeof(p),romdir,e.cFileName);found++;if(!convert_one(p,seed,debug_mode,g_nintenstation643d ? 2 : (g_minimal_emulator ? 1 : 0)))failed++;}while(FindNextFileA(h,&e));FindClose(h);if(!found){fprintf(stderr,"No ROM files found in %s\n",romdir);return 2;}return failed?1:0;}
#else
    {DIR *d=opendir(romdir);struct dirent *e;int found=0,failed=0;if(!d){perror(romdir);return 2;}while((e=readdir(d))){char p[PATH_CAP];if(!ends_with_rom(e->d_name))continue;path_join(p,sizeof(p),romdir,e->d_name);if(!is_file(p))continue;found++;if(!convert_one(p,seed,debug_mode,g_nintenstation643d ? 2 : (g_minimal_emulator ? 1 : 0)))failed++;}closedir(d);if(!found){fprintf(stderr,"No ROM files found in %s\n",romdir);return 2;}return failed?1:0;}
#endif
}
