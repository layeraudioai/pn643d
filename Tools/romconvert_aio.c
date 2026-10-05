/*
 * romconvert_aio - build one instrumented DaedalusX64 CTR binary per unique
 * N64 cartridge ID found in ./roms. The repository's CTR CMake target has an
 * optional compile-time CartID; debug mode enables SDMC logging/profiling and
 * the debug console for each build.
 *
 * Build this helper with a host C compiler (not devkitARM), then run from the
 * project root: romconvert debug
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#include <sys/wait.h>
#else
#define strcasecmp _stricmp
#endif
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define getcwd _getcwd
#define chdir _chdir
#define mkdir(path, mode) _mkdir(path)
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#else
#include <dirent.h>
#include <unistd.h>
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#endif

#define PATH_CAP 4096

typedef struct { char **items; size_t count; size_t capacity; } StringList;
typedef struct { unsigned id; int attempted; int success; } BuildRecord;

static void join_path(char *out, size_t cap, const char *a, const char *b)
{
    size_t na = strlen(a), nb = strlen(b);
    int add_sep = na && a[na - 1] != '/' && a[na - 1] != '\\';
    size_t ns = add_sep ? 1u : 0u;
    if (na + ns + nb >= cap) { if (cap) out[0] = '\0'; return; }
    memcpy(out, a, na);
    if (add_sep) out[na++] = PATH_SEP;
    memcpy(out + na, b, nb + 1);
}

static int stat_is_regular(const struct stat *st)
{
#ifdef _WIN32
    return (st->st_mode & _S_IFMT) == _S_IFREG;
#else
    return S_ISREG(st->st_mode);
#endif
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && stat_is_regular(&st);
}

static int is_project_root(const char *path)
{
    char probe[PATH_CAP];
    struct stat st;
    join_path(probe, sizeof(probe), path, "CMakeLists.txt");
    if (stat(probe, &st) != 0 || !stat_is_regular(&st)) return 0;
    join_path(probe, sizeof(probe), path, "Source/CMakeLists.txt");
    return stat(probe, &st) == 0 && stat_is_regular(&st);
}

static int find_project_root(char *root, size_t cap)
{
    if (!getcwd(root, (int)cap)) return 0;
    for (;;) {
        if (is_project_root(root)) return 1;
        size_t n = strlen(root);
        while (n > 1 && (root[n - 1] == '/' || root[n - 1] == '\\')) root[--n] = '\0';
        char *slash = strrchr(root, '/');
        char *backslash = strrchr(root, '\\');
        if (backslash && (!slash || backslash > slash)) slash = backslash;
        if (!slash) return 0;
#ifdef _WIN32
        if (slash == root + 2 && root[1] == ':') slash[1] = '\0';
        else if (slash == root) slash[1] = '\0';
        else *slash = '\0';
#else
        if (slash == root) slash[1] = '\0';
        else *slash = '\0';
#endif
    }
}

static int ensure_dir(const char *path)
{
    char tmp[PATH_CAP];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) return 0;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i < n; ++i) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char saved = tmp[i]; tmp[i] = '\0';
            if (tmp[0] && mkdir(tmp, 0777) != 0 && errno != EEXIST) return 0;
            tmp[i] = saved;
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return 0;
    return 1;
}

static int list_push(StringList *list, const char *value)
{
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2 : 32;
        char **items = (char **)realloc(list->items, next * sizeof(*items));
        if (!items) return 0;
        list->items = items; list->capacity = next;
    }
    list->items[list->count] = (char *)malloc(strlen(value) + 1);
    if (!list->items[list->count]) return 0;
    strcpy(list->items[list->count++], value);
    return 1;
}

static void list_free(StringList *list)
{
    for (size_t i = 0; i < list->count; ++i) free(list->items[i]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int has_n64_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return strcasecmp(dot, ".z64") == 0 || strcasecmp(dot, ".n64") == 0 || strcasecmp(dot, ".v64") == 0;
}

static int enumerate_roms(const char *rom_dir, StringList *out)
{
#ifdef _WIN32
    char pattern[PATH_CAP];
    struct _finddata_t item;
    intptr_t handle;
    join_path(pattern, sizeof(pattern), rom_dir, "*");
    handle = _findfirst(pattern, &item);
    if (handle == -1) return 0;
    do {
        if ((item.attrib & _A_SUBDIR) == 0 && has_n64_extension(item.name)) {
            char path[PATH_CAP]; join_path(path, sizeof(path), rom_dir, item.name);
            if (!list_push(out, path)) { _findclose(handle); return -1; }
        }
    } while (_findnext(handle, &item) == 0);
    _findclose(handle);
#else
    DIR *dir = opendir(rom_dir);
    if (!dir) return 0;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL) {
        if (item->d_name[0] == '.' || !has_n64_extension(item->d_name)) continue;
        char path[PATH_CAP]; join_path(path, sizeof(path), rom_dir, item->d_name);
        if (!list_push(out, path)) { closedir(dir); return -1; }
    }
    closedir(dir);
#endif
    return 1;
}

static int n64_cart_id(const char *path, unsigned *id)
{
    unsigned char h[0x40];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t got = fread(h, 1, sizeof(h), f); fclose(f);
    if (got != sizeof(h)) return 0;
    unsigned char a = h[0], b = h[1], c = h[2], d = h[3];
    unsigned high, low;
    if (a == 0x80 && b == 0x37 && c == 0x12 && d == 0x40) { /* .z64 */
        high = h[0x3e]; low = h[0x3f];
    } else if (a == 0x37 && b == 0x80 && c == 0x40 && d == 0x12) { /* .v64 */
        high = h[0x3f]; low = h[0x3e];
    } else if (a == 0x40 && b == 0x12 && c == 0x37 && d == 0x80) { /* .n64 */
        high = h[0x3d]; low = h[0x3c];
    } else return 0;
    *id = (high << 8) | low;
    return *id != 0;
}

static void safe_component(char *out, size_t cap, const char *input)
{
    size_t j = 0;
    for (size_t i = 0; input[i] && j + 1 < cap; ++i) {
        unsigned char c = (unsigned char)input[i];
        out[j++] = (isalnum(c) || c == '-' || c == '_' || c == '.') ? (char)c : '_';
    }
    while (j && (out[j - 1] == '.' || out[j - 1] == ' ')) --j;
    out[j] = '\0';
    if (!j) snprintf(out, cap, "rom");
}

static const char *rom_stem(const char *path, char *out, size_t cap)
{
    const char *base1 = strrchr(path, '/'), *base2 = strrchr(path, '\\');
    const char *base = base1 ? base1 + 1 : path;
    if (base2 && (!base1 || base2 > base1)) base = base2 + 1;
    snprintf(out, cap, "%s", base);
    char *dot = strrchr(out, '.'); if (dot) *dot = '\0';
    return out;
}

static int run_command(const char *command, int dry_run)
{
    if (dry_run) { printf("[dry-run] %s\n", command); return 1; }
    int rc = system(command);
#ifdef _WIN32
    return rc == 0;
#else
    return rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
#endif
}

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) return 0;
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); return 0; }
    char buf[65536]; size_t n; int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) != 0)
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    if (ferror(in)) ok = 0;
    if (fclose(in) != 0) ok = 0;
    if (fclose(out) != 0) ok = 0;
    return ok;
}

static BuildRecord *record_for(BuildRecord *records, size_t *count, unsigned id)
{
    for (size_t i = 0; i < *count; ++i) if (records[i].id == id) return &records[i];
    records[*count] = (BuildRecord){ id, 0, 0 };
    return &records[(*count)++];
}

static int build_cart(unsigned id, const char *root, const char *build_dir, int dry_run)
{
    char configure[PATH_CAP * 3], build[PATH_CAP * 2];
    snprintf(configure, sizeof(configure),
        "cmake -S \"%s\" -B \"%s\" -DCTR_RELEASE=ON -DDAEDALUS_GAME_ID=%04X "
        "-DDAEDALUS_ENABLE_SDMC_DIAGNOSTICS=ON -DDAEDALUS_ENABLE_DEBUG_CONSOLE=ON",
        root, build_dir, id);
    snprintf(build, sizeof(build), "cmake --build \"%s\" --target N3DS --parallel 2", build_dir);
    printf("\n=== Debug instrumentation build for CartID %04X ===\n", id);
    if (!run_command(configure, dry_run)) return 0;
    if (!run_command(build, dry_run)) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "debug") != 0 && strcmp(argv[1], "--help") != 0 && strcmp(argv[1], "-h") != 0)) {
        fprintf(stderr, "Usage: %s debug\nBuild instrumented 3DS binaries for N64 ROMs in ./roms.\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        printf("Usage: %s debug\nBuild one debug/profiling 3DS binary per unique N64 CartID in ./roms.\n", argv[0]);
        return 0;
    }

    char root[PATH_CAP], rom_dir[PATH_CAP];
    if (!find_project_root(root, sizeof(root))) {
        fprintf(stderr, "Run from the project checkout (could not find CMakeLists.txt and Source/CMakeLists.txt).\n");
        return 2;
    }
    if (chdir(root) != 0) { perror("chdir project root"); return 2; }
    join_path(rom_dir, sizeof(rom_dir), root, "roms");
    StringList roms = {0};
    int found = enumerate_roms(rom_dir, &roms);
    if (found <= 0) {
        fprintf(stderr, "No ROMs found in %s. Put raw .z64, .n64, or .v64 files there.\n", rom_dir);
        return found < 0 ? 2 : 1;
    }
    int dry_run = getenv("ROMCONVERT_DRY_RUN") && strcmp(getenv("ROMCONVERT_DRY_RUN"), "0") != 0;
    char build_root[PATH_CAP], dist_root[PATH_CAP];
    join_path(build_root, sizeof(build_root), root, "build/debug");
    join_path(dist_root, sizeof(dist_root), root, "dist/debug");
    if (!ensure_dir(build_root) || !ensure_dir(dist_root)) {
        fprintf(stderr, "Cannot create debug build/output directories.\n"); list_free(&roms); return 2;
    }

    BuildRecord *records = (BuildRecord *)calloc(roms.count, sizeof(*records));
    if (!records) { list_free(&roms); return 2; }
    size_t record_count = 0;
    int failed = 0, skipped = 0, completed = 0;
    printf("Found %lu ROM file(s); creating instrumented N3DS builds.\n", (unsigned long)roms.count);
    for (size_t i = 0; i < roms.count; ++i) {
        unsigned id;
        char raw_stem[PATH_CAP], safe_stem[256], cart_build[PATH_CAP], output_dir[PATH_CAP];
        if (!n64_cart_id(roms.items[i], &id)) {
            fprintf(stderr, "Skipping invalid/unsupported N64 ROM header: %s\n", roms.items[i]);
            ++skipped; continue;
        }
        safe_component(safe_stem, sizeof(safe_stem), rom_stem(roms.items[i], raw_stem, sizeof(raw_stem)));
        char cart_dir[64]; snprintf(cart_dir, sizeof(cart_dir), "cart_%04X", id);
        join_path(cart_build, sizeof(cart_build), build_root, cart_dir);
        BuildRecord *record = record_for(records, &record_count, id);
        if (!record->attempted) {
            record->attempted = 1;
            record->success = build_cart(id, root, cart_build, dry_run);
            if (!record->success) { fprintf(stderr, "Build failed for CartID %04X.\n", id); ++failed; continue; }
        }
        if (!record->success) { ++failed; continue; }
        char output_leaf[300]; snprintf(output_leaf, sizeof(output_leaf), "%.240s_%04X", safe_stem, id);
        join_path(output_dir, sizeof(output_dir), dist_root, output_leaf);
        if (!ensure_dir(output_dir)) { fprintf(stderr, "Cannot create %s\n", output_dir); ++failed; continue; }
        const char *exts[] = { ".3dsx", ".cia", ".elf" };
        int copied = 0;
        for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); ++e) {
            char from[PATH_CAP], to[PATH_CAP];
            join_path(from, sizeof(from), cart_build, "DaedalusX64");
            strncat(from, exts[e], sizeof(from) - strlen(from) - 1);
            if (!file_exists(from)) continue;
            join_path(to, sizeof(to), output_dir, "DaedalusX64");
            strncat(to, exts[e], sizeof(to) - strlen(to) - 1);
            if (!dry_run && !copy_file(from, to)) { fprintf(stderr, "Copy failed: %s\n", from); ++failed; copied = -1; break; }
            if (dry_run) printf("[dry-run] Copy %s -> %s\n", from, to);
            ++copied;
        }
        if (dry_run) copied = 1; /* Build artifacts do not exist yet during planning. */
        if (copied <= 0) {
            if (!dry_run) fprintf(stderr, "No .3dsx/.cia/.elf build artifact found for %s\n", roms.items[i]);
            if (!dry_run) ++failed;
        } else {
            printf("Built %s (CartID %04X) -> %s\n", roms.items[i], id, output_dir);
            ++completed;
        }
    }
    printf("\nFinished: %d ROM(s) packaged, %d failed, %d invalid/skipped.\n", completed, failed, skipped);
    printf("Output: %s\n", dist_root);
    if (dry_run) puts("Dry-run only: set ROMCONVERT_DRY_RUN=0 (or unset it) to configure/build.");
    free(records); list_free(&roms);
    return failed ? 1 : 0;
}
