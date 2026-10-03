#include "CTRStorage.h"
#include "Utility/IO.h"
#include <stdio.h>
#include <string.h>

#ifndef DAEDALUS_DISABLE_OS_STORAGE
#include <3ds.h>
#endif

namespace {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
static bool OpenSaveArchive(FS_Archive* a) {
    FS_Path p = fsMakePath(PATH_EMPTY, "");
    return R_SUCCEEDED(FSUSER_OpenArchive(a, ARCHIVE_SAVEDATA, p));
}

static bool OpenExtArchive(FS_Archive* a, u64 id, bool create) {
    FS_ExtSaveDataInfo info = {};
    info.mediaType = MEDIATYPE_SD;
    info.saveId = id;
    if (create)
        FSUSER_CreateExtSaveData(info, 1, 4, 0x200000, 0, nullptr);
    FS_Path p = fsMakePath(PATH_BINARY, &info);
    return R_SUCCEEDED(FSUSER_OpenArchive(a, ARCHIVE_EXTDATA, p));
}

static bool ReadArchive(FS_Archive a, const char* n, void* d, size_t s, size_t* actual) {
    FS_Path p = fsMakePath(PATH_ASCII, n);
    Handle f = 0;
    if (R_FAILED(FSUSER_OpenFile(&f, a, p, FS_OPEN_READ, 0)))
        return false;
    u64 fs = 0;
    FSFILE_GetSize(f, &fs);
    u32 want = (u32)((fs < s) ? fs : s), got = 0;
    Result r = FSFILE_Read(f, &got, 0, d, want);
    FSFILE_Close(f);
    if (actual)
        *actual = got;
    return R_SUCCEEDED(r);
}

static bool WriteArchive(FS_Archive a, const char* n, const void* d, size_t s) {
    FS_Path p = fsMakePath(PATH_ASCII, n);
    Handle f = 0;
    Result r = FSUSER_OpenFile(&f, a, p, FS_OPEN_READ | FS_OPEN_WRITE, 0);
    if (R_FAILED(r))
        r = FSUSER_OpenFile(&f, a, p, FS_OPEN_WRITE | FS_OPEN_CREATE, 0);
    if (R_FAILED(r))
        return false;
    r = FSFILE_SetSize(f, s);
    if (R_FAILED(r)) {
        FSFILE_Close(f);
        return false;
    }
    u32 w = 0;
    r = FSFILE_Write(f, &w, 0, d, (u32)s, FS_WRITE_FLUSH);
    FSFILE_Close(f);
    return R_SUCCEEDED(r) && w == s;
}

static bool Commit(FS_Archive a) {
    Result r = FSUSER_ControlArchive(a, ARCHIVE_ACTION_COMMIT_SAVE_DATA, nullptr, 0, nullptr, 0);
    FSUSER_CloseArchive(a);
    return R_SUCCEEDED(r);
}

static bool ImportArchiveFile(FS_Archive a, const char* name, const char* path) {
    FS_Path p = fsMakePath(PATH_ASCII, name);
    Handle in = 0;
    if (R_FAILED(FSUSER_OpenFile(&in, a, p, FS_OPEN_READ, 0)))
        return false;

    u64 size = 0;
    if (R_FAILED(FSFILE_GetSize(in, &size))) {
        FSFILE_Close(in);
        return false;
    }

    FILE* out = fopen(path, "wb");
    if (!out) {
        FSFILE_Close(in);
        return false;
    }

    u8 buffer[0x4000];
    u64 offset = 0;
    bool ok = true;
    while (offset < size) {
        u32 want = (u32)((size - offset < sizeof(buffer)) ? size - offset : sizeof(buffer));
        u32 got = 0;
        Result r = FSFILE_Read(in, &got, offset, buffer, want);
        if (R_FAILED(r) || got != want || fwrite(buffer, 1, got, out) != got) {
            ok = false;
            break;
        }
        offset += got;
    }
    if (fclose(out) != 0)
        ok = false;
    FSFILE_Close(in);
    if (!ok)
        remove(path);
    return ok;
}

static bool ExportArchiveFile(FS_Archive a, const char* name, const char* path) {
    FILE* in = fopen(path, "rb");
    if (!in)
        return false;
    if (fseek(in, 0, SEEK_END) != 0) {
        fclose(in);
        return false;
    }
    long fileSize = ftell(in);
    if (fileSize < 0 || fseek(in, 0, SEEK_SET) != 0) {
        fclose(in);
        return false;
    }

    FS_Path p = fsMakePath(PATH_ASCII, name);
    Handle out = 0;
    Result r = FSUSER_OpenFile(&out, a, p, FS_OPEN_READ | FS_OPEN_WRITE, 0);
    if (R_FAILED(r))
        r = FSUSER_OpenFile(&out, a, p, FS_OPEN_WRITE | FS_OPEN_CREATE, 0);
    if (R_FAILED(r)) {
        fclose(in);
        return false;
    }
    r = FSFILE_SetSize(out, (u64)fileSize);
    u8 buffer[0x4000];
    u64 offset = 0;
    bool ok = R_SUCCEEDED(r);
    while (ok && offset < (u64)fileSize) {
        size_t want = (size_t)(((u64)fileSize - offset < sizeof(buffer)) ? (u64)fileSize - offset : sizeof(buffer));
        size_t got = fread(buffer, 1, want, in);
        u32 written = 0;
        if (got != want || R_FAILED(FSFILE_Write(out, &written, offset, buffer, (u32)got, FS_WRITE_FLUSH)) || written != got)
            ok = false;
        offset += got;
    }
    if (fclose(in) != 0)
        ok = false;
    FSFILE_Close(out);
    return ok;
}
#endif

static bool SdmcSaveRead(const char* n, void* d, size_t s, size_t* actual) {
    return CTRStorage::ImportFile("sdmc:/3ds/DaedalusX64/SaveGames", n, d, s, actual);
}

static bool SdmcSaveFileSize(const char* n, size_t* size) {
    char path[512];
    snprintf(path, sizeof(path), "sdmc:/3ds/DaedalusX64/SaveGames/%s", n);
    FILE* file = fopen(path, "rb");
    if (!file)
        return false;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long fileSize = ok ? ftell(file) : -1;
    fclose(file);
    if (fileSize < 0)
        return false;
    *size = (size_t)fileSize;
    return true;
}


static bool SdmcSaveWrite(const char* n, const void* d, size_t s) {
    IO::Directory::EnsureExists("sdmc:/3ds/DaedalusX64/SaveGames");
    return CTRStorage::ExportFile("sdmc:/3ds/DaedalusX64/SaveGames", n, d, s);
}

static bool SdmcSaveDelete(const char* n) {
    char path[512];
    snprintf(path, sizeof(path), "sdmc:/3ds/DaedalusX64/SaveGames/%s", n);
    return remove(path) == 0;
}

static bool SdmcExtRead(u64 id, const char* n, void* d, size_t s, size_t* actual) {
    (void)id;
    return CTRStorage::ImportFile("sdmc:/3ds/DaedalusX64", n, d, s, actual);
}

static bool SdmcExtWrite(u64 id, const char* n, const void* d, size_t s) {
    (void)id;
    IO::Directory::EnsureExists("sdmc:/3ds/DaedalusX64");
    return CTRStorage::ExportFile("sdmc:/3ds/DaedalusX64", n, d, s);
}
}

namespace CTRStorage {
bool SaveDataRead(const char* n, void* d, size_t s, size_t* actual) {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenSaveArchive(&a)) {
        bool ok = ReadArchive(a, n, d, s, actual);
        FSUSER_CloseArchive(a);
        if (ok) return true;
    }
#endif

    size_t bytesRead = 0;
    if (!SdmcSaveRead(n, d, s, actual ? actual : &bytesRead))
        return false;

#ifndef DAEDALUS_DISABLE_OS_STORAGE
    // Migrate only when the bounded read included the complete source file.
    // Otherwise the archive would contain a truncated prefix and take
    // precedence over the still-useful legacy SD copy on later reads.
    size_t sourceSize = 0;
    size_t migrateSize = actual ? *actual : bytesRead;
    if (SdmcSaveFileSize(n, &sourceSize) && sourceSize <= s &&
        migrateSize == sourceSize && sourceSize > 0 && OpenSaveArchive(&a)) {
        bool written = WriteArchive(a, n, d, sourceSize);
        bool committed = Commit(a);
        if (written && committed)
            SdmcSaveDelete(n);
    }
#endif
    return true;
}

bool SaveDataWrite(const char* n, const void* d, size_t s) {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenSaveArchive(&a)) {
        bool ok = WriteArchive(a, n, d, s);
        bool committed = Commit(a);
        if (ok && committed) return true;
    }
#endif
    return SdmcSaveWrite(n, d, s);
}

bool SaveDataDelete(const char* n) {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenSaveArchive(&a)) {
        FS_Path p = fsMakePath(PATH_ASCII, n);
        bool ok = R_SUCCEEDED(FSUSER_DeleteFile(a, p));
        bool committed = Commit(a);
        if (ok && committed) return true;
    }
#endif
    return SdmcSaveDelete(n);
}

bool SaveDataImportFile(const char* n, const char* path) {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenSaveArchive(&a)) {
        bool ok = ImportArchiveFile(a, n, path);
        FSUSER_CloseArchive(a);
        if (ok) return true;
    }

    // An existing SD-card savestate is a legacy copy. Migrate it to
    // savedata, but leave it in place for this read and as a fallback.
    if (OpenSaveArchive(&a)) {
        bool migrated = ExportArchiveFile(a, n, path);
        bool committed = Commit(a);
        (void)migrated;
        (void)committed;
    }
#else
    (void)n;
    (void)path;
#endif
    return false;
}

bool SaveDataExportFile(const char* n, const char* path) {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenSaveArchive(&a)) {
        bool ok = ExportArchiveFile(a, n, path);
        bool committed = Commit(a);
        if (ok && committed) return true;
    }
#endif
    (void)n;
    (void)path;
    return false;
}

bool ExtDataEnsure(u64 id, u64 quota) {
    (void)quota;
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenExtArchive(&a, id, true)) {
        FSUSER_CloseArchive(a);
        return true;
    }
#endif
    IO::Directory::EnsureExists("sdmc:/3ds/DaedalusX64");
    return true;
}

bool ExtDataRead(u64 id, const char* n, void* d, size_t s, size_t* actual) {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenExtArchive(&a, id, true)) {
        bool ok = ReadArchive(a, n, d, s, actual);
        FSUSER_CloseArchive(a);
        if (ok) return true;
    }
#endif
    return SdmcExtRead(id, n, d, s, actual);
}

bool ExtDataWrite(u64 id, const char* n, const void* d, size_t s) {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenExtArchive(&a, id, true)) {
        bool ok = WriteArchive(a, n, d, s);
        bool committed = Commit(a);
        if (ok && committed) return true;
    }
#endif
    return SdmcExtWrite(id, n, d, s);
}

bool ExportFile(const char* p, const char* n, const void* d, size_t s) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", p, n);
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(d, 1, s, f) == (size_t)s;
    fclose(f);
    return ok;
}

bool ImportFile(const char* p, const char* n, void* d, size_t s, size_t* actual) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", p, n);
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    size_t nread = fread(d, 1, s, f);
    fclose(f);
    if (actual)
        *actual = nread;
    return nread > 0;
}
}
