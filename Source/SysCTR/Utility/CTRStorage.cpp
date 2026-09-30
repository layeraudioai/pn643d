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
    FSFILE_SetSize(f, s);
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
#endif

static bool SdmcSaveRead(const char* n, void* d, size_t s, size_t* actual) {
    return CTRStorage::ImportFile("sdmc:/3ds/DaedalusX64/SaveGames", n, d, s, actual);
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
bool SaveDataRead(const char* n, void* d, size_t s) {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenSaveArchive(&a)) {
        bool ok = ReadArchive(a, n, d, s, nullptr);
        FSUSER_CloseArchive(a);
        if (ok) return true;
    }
#endif
    return SdmcSaveRead(n, d, s, nullptr);
}

bool SaveDataWrite(const char* n, const void* d, size_t s) {
#ifndef DAEDALUS_DISABLE_OS_STORAGE
    FS_Archive a;
    if (OpenSaveArchive(&a)) {
        bool ok = WriteArchive(a, n, d, s);
        Commit(a);
        if (ok) return true;
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
        Commit(a);
        if (ok) return true;
    }
#endif
    return SdmcSaveDelete(n);
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
        Commit(a);
        if (ok) return true;
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
