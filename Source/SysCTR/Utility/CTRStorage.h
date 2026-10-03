#pragma once
#include <stddef.h>
#include "Utility/DaedalusTypes.h"
namespace CTRStorage {
bool SaveDataRead(const char *name, void *data, size_t size, size_t *actual_size = nullptr);
bool SaveDataImportFile(const char *name, const char *sdmc_file);
bool SaveDataExportFile(const char *name, const char *sdmc_file);
bool SaveDataWrite(const char *name, const void *data, size_t size);
bool SaveDataDelete(const char *name);
bool ExtDataRead(u64 id, const char *name, void *data, size_t size, size_t *actual_size = nullptr);
bool ExtDataWrite(u64 id, const char *name, const void *data, size_t size);
bool ExtDataEnsure(u64 id, u64 quota = 0x200000);
bool ExportFile(const char *sdmc_path, const char *name, const void *data, size_t size);
bool ImportFile(const char *sdmc_path, const char *name, void *data, size_t size, size_t *actual_size = nullptr);
}
