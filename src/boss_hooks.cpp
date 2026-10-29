/*
 * nn_boss hooks: serve SD-card news (news_store) to the official Notification
 * applet (inf.rpx) without ever writing to MLC.
 *
 * - Storage objects initialised with dir "news" are tracked.
 * - Their GetDataList / GetUnreadDataList get SD items prepended (newest first),
 *   named "sdcn%08x" so they never collide with real BOSS DataNames.
 * - NsData objects opened with an "sdcn" name are tracked and every data call
 *   (Read/GetSize/ReadFlag/Delete/...) is answered from the SD card.
 * - Anything else passes straight through to stock nn_boss.
 */
#if defined(__WIIU__) || defined(__WUT__) || defined(__WUPS__)

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <wups.h>
#include <coreinit/title.h>

#include "news_store.h"
#include "notification_config.h"

#define BOSS_OK            0x00000000u
#define BOSS_ERR_NOT_FOUND 0xC0B00000u   // any value with the failure bit set
#define BOSS_IS_OK(r)      (((r) & 0x80000000u) == 0)

#define DATANAME_LEN 32

static void HookLog(const char* fmt, ...) {
    FILE* f = fopen(NOTIFY_SD_DIR "/post_log.txt", "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

/* ------------------------------------------------------------------------- */
/* Tracking tables                                                           */
/* ------------------------------------------------------------------------- */

#define MAX_STORAGES 8
#define MAX_NSDATA   32

static const void* s_news_storages[MAX_STORAGES];

struct SdNsData { const void* obj; uint32_t serial; uint32_t pos; };
static SdNsData s_nsdata[MAX_NSDATA];

static void TrackStorage(const void* st, const char* dir) {
    if (!dir) return;
    HookLog("[BossHook] Storage Init: dir='%s' (obj=%p)", dir, st);
    if (strcmp(dir, "news") != 0) return;
    for (int i = 0; i < MAX_STORAGES; i++) if (s_news_storages[i] == st) return;
    for (int i = 0; i < MAX_STORAGES; i++) {
        if (!s_news_storages[i]) {
            s_news_storages[i] = st;
            NewsStore_Init(NOTIFY_SD_DIR);
            HookLog("[BossHook] tracking news storage dir='%s' slot=%u", dir, i);
            return;
        }
    }
}

static void UntrackStorage(const void* st) {
    for (int i = 0; i < MAX_STORAGES; i++) if (s_news_storages[i] == st) s_news_storages[i] = nullptr;
}

static bool IsNewsStorage(const void* st) {
    if (!st) return false;
    for (int i = 0; i < MAX_STORAGES; i++) if (s_news_storages[i] == st) return true;
    return false;
}

static SdNsData* FindNsData(const void* obj) {
    if (!obj) return nullptr;
    for (int i = 0; i < MAX_NSDATA; i++) if (s_nsdata[i].obj == obj) return &s_nsdata[i];
    return nullptr;
}

static void UntrackNsData(const void* obj) {
    SdNsData* e = FindNsData(obj);
    if (e) memset(e, 0, sizeof(*e));
}

static void TrackNsData(const void* obj, const char* name) {
    UntrackNsData(obj);   // objects can be re-initialised
    if (!name) return;
    uint32_t serial = 0;
    if (!NewsStore_ParseName(name, &serial)) return;
    HookLog("[BossHook] TrackNsData: name='%s' serial=0x%08X (obj=%p)", name, serial, obj);
    for (int i = 0; i < MAX_NSDATA; i++) {
        if (!s_nsdata[i].obj) {
            s_nsdata[i] = { obj, serial, 0 };
            return;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* nn::boss::Storage                                                         */
/* ------------------------------------------------------------------------- */

DECL_FUNCTION(uint32_t, Storage_Init_DirKind, void* self, const char* dir, int kind) {
    uint32_t r = real_Storage_Init_DirKind(self, dir, kind);
    TrackStorage(self, dir);
    return r;
}
DECL_FUNCTION(uint32_t, Storage_Init_DirPidKind, void* self, const char* dir, uint32_t pid, int kind) {
    uint32_t r = real_Storage_Init_DirPidKind(self, dir, pid, kind);
    TrackStorage(self, dir);
    return r;
}
DECL_FUNCTION(uint32_t, Storage_Init_SlotDirKind, void* self, uint8_t slot, const char* dir, int kind) {
    uint32_t r = real_Storage_Init_SlotDirKind(self, slot, dir, kind);
    TrackStorage(self, dir);
    return r;
}
DECL_FUNCTION(void*, Storage_Ct_DirKind, void* self, const char* dir, int kind) {
    void* r = real_Storage_Ct_DirKind(self, dir, kind);
    TrackStorage(self, dir);
    return r;
}
DECL_FUNCTION(void*, Storage_Ct_DirPidKind, void* self, const char* dir, uint32_t pid, int kind) {
    void* r = real_Storage_Ct_DirPidKind(self, dir, pid, kind);
    TrackStorage(self, dir);
    return r;
}
DECL_FUNCTION(void*, Storage_Ct_SlotDirKind, void* self, uint8_t slot, const char* dir, int kind) {
    void* r = real_Storage_Ct_SlotDirKind(self, slot, dir, kind);
    TrackStorage(self, dir);
    return r;
}
DECL_FUNCTION(void*, Storage_Ct_Copy, void* self, const void* other) {
    void* r = real_Storage_Ct_Copy(self, other);
    if (IsNewsStorage(other)) TrackStorage(self, "news");
    return r;
}
DECL_FUNCTION(void, Storage_Finalize, void* self) {
    UntrackStorage(self);
    real_Storage_Finalize(self);
}
DECL_FUNCTION(void*, Storage_Dt, void* self, int flags) {
    UntrackStorage(self);
    return real_Storage_Dt(self, flags);
}

/* Shared list builder: SD items first (newest first), then real BOSS items. */
typedef uint32_t (*RealListFn)(const void*, char*, uint32_t, uint32_t*, uint32_t);

static uint32_t BuildList(RealListFn real, const void* self, char* names, uint32_t max,
                          uint32_t* out_count, uint32_t offset, bool unread_only) {
    HookLog("[BossHook] BuildList: isNews=%d, max=%u, offset=%u, unread=%d",
            IsNewsStorage(self) ? 1 : 0, max, offset, unread_only ? 1 : 0);
    if (!IsNewsStorage(self) || !names || !out_count) {
        return real(self, names, max, out_count, offset);
    }

    uint32_t all[NEWS_STORE_MAX];
    uint32_t total = NewsStore_List(all, NEWS_STORE_MAX);
    uint32_t sd[NEWS_STORE_MAX];
    uint32_t n = 0;
    for (uint32_t i = total; i-- > 0;) {                 // newest first
        if (unread_only && NewsStore_GetRead(all[i])) continue;
        sd[n++] = all[i];
    }
    HookLog("[BossHook] SD items available: total=%u, matching=%u", total, n);
    if (n == 0) return real(self, names, max, out_count, offset);

    uint32_t written = 0;
    for (uint32_t i = offset; i < n && written < max; i++, written++) {
        memset(names + written * DATANAME_LEN, 0, DATANAME_LEN);
        NewsStore_MakeName(sd[i], names + written * DATANAME_LEN);
    }
    if (written < max) {
        uint32_t real_count = 0;
        uint32_t real_offset = offset > n ? offset - n : 0;
        uint32_t r = real(self, names + written * DATANAME_LEN, max - written, &real_count, real_offset);
        if (BOSS_IS_OK(r)) written += real_count;
    }
    *out_count = written;
    HookLog("[BossHook] BuildList returning count=%u", written);
    return BOSS_OK;
}

DECL_FUNCTION(uint32_t, Storage_GetDataList, const void* self, char* names, uint32_t max, uint32_t* out_count, uint32_t offset) {
    return BuildList(real_Storage_GetDataList, self, names, max, out_count, offset, false);
}
DECL_FUNCTION(uint32_t, Storage_GetUnreadDataList, const void* self, char* names, uint32_t max, uint32_t* out_count, uint32_t offset) {
    return BuildList(real_Storage_GetUnreadDataList, self, names, max, out_count, offset, true);
}

/* Split a DataName array into SD names (handled here) and real ones (forwarded). */
static uint32_t ForwardRealNames(const char* names, uint32_t count, char** out_real, uint32_t** out_idx) {
    *out_real = (char*)malloc(count * DATANAME_LEN + 1);
    *out_idx = (uint32_t*)malloc(count * sizeof(uint32_t) + 1);
    if (!*out_real || !*out_idx) return UINT32_MAX;
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (NewsStore_ParseName(names + i * DATANAME_LEN, nullptr)) continue;
        memcpy(*out_real + n * DATANAME_LEN, names + i * DATANAME_LEN, DATANAME_LEN);
        (*out_idx)[n++] = i;
    }
    return n;
}

static bool HasSdName(const char* names, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        if (NewsStore_ParseName(names + i * DATANAME_LEN, nullptr)) return true;
    return false;
}

DECL_FUNCTION(uint32_t, Storage_GetReadFlags, const void* self, const char* names, uint32_t count, bool* flags) {
    if (!IsNewsStorage(self) || !names || !flags || !HasSdName(names, count))
        return real_Storage_GetReadFlags(self, names, count, flags);

    char* rn = nullptr; uint32_t* idx = nullptr;
    uint32_t n = ForwardRealNames(names, count, &rn, &idx);
    uint32_t r = BOSS_OK;
    if (n != UINT32_MAX && n > 0) {
        bool* rf = (bool*)malloc(n * sizeof(bool));
        if (rf) {
            r = real_Storage_GetReadFlags(self, rn, n, rf);
            for (uint32_t i = 0; i < n; i++) flags[idx[i]] = BOSS_IS_OK(r) ? rf[i] : true;
            free(rf);
        }
    }
    for (uint32_t i = 0, s; i < count; i++)
        if (NewsStore_ParseName(names + i * DATANAME_LEN, &s)) flags[i] = NewsStore_GetRead(s);
    free(rn); free(idx);
    return BOSS_OK;
}

DECL_FUNCTION(uint32_t, Storage_SetReadFlagsAll, void* self, const char* names, uint32_t count, bool flag) {
    if (!IsNewsStorage(self) || !names || !HasSdName(names, count))
        return real_Storage_SetReadFlagsAll(self, names, count, flag);

    char* rn = nullptr; uint32_t* idx = nullptr;
    uint32_t n = ForwardRealNames(names, count, &rn, &idx);
    if (n != UINT32_MAX && n > 0) real_Storage_SetReadFlagsAll(self, rn, n, flag);
    for (uint32_t i = 0, s; i < count; i++)
        if (NewsStore_ParseName(names + i * DATANAME_LEN, &s)) NewsStore_SetRead(s, flag);
    free(rn); free(idx);
    return BOSS_OK;
}

DECL_FUNCTION(uint32_t, Storage_SetReadFlagsEach, void* self, const char* names, uint32_t count, const bool* flags) {
    if (!IsNewsStorage(self) || !names || !flags || !HasSdName(names, count))
        return real_Storage_SetReadFlagsEach(self, names, count, flags);

    char* rn = nullptr; uint32_t* idx = nullptr;
    uint32_t n = ForwardRealNames(names, count, &rn, &idx);
    if (n != UINT32_MAX && n > 0) {
        bool* rf = (bool*)malloc(n * sizeof(bool));
        if (rf) {
            for (uint32_t i = 0; i < n; i++) rf[i] = flags[idx[i]];
            real_Storage_SetReadFlagsEach(self, rn, n, rf);
            free(rf);
        }
    }
    for (uint32_t i = 0, s; i < count; i++)
        if (NewsStore_ParseName(names + i * DATANAME_LEN, &s)) NewsStore_SetRead(s, flags[i]);
    free(rn); free(idx);
    return BOSS_OK;
}

/* ------------------------------------------------------------------------- */
/* nn::boss::NsData                                                          */
/* ------------------------------------------------------------------------- */

DECL_FUNCTION(uint32_t, NsData_Init, void* self, const void* storage, const char* name) {
    uint32_t r = real_NsData_Init(self, storage, name);
    TrackNsData(self, name);
    return FindNsData(self) ? BOSS_OK : r;
}
DECL_FUNCTION(void*, NsData_Ct, void* self, const void* storage, const char* name) {
    void* r = real_NsData_Ct(self, storage, name);
    TrackNsData(self, name);
    return r;
}
DECL_FUNCTION(void, NsData_Finalize, void* self) {
    UntrackNsData(self);
    real_NsData_Finalize(self);
}
DECL_FUNCTION(void*, NsData_Dt, void* self, int flags) {
    UntrackNsData(self);
    return real_NsData_Dt(self, flags);
}
DECL_FUNCTION(bool, NsData_Exist, const void* self) {
    SdNsData* e = FindNsData(self);
    if (!e) return real_NsData_Exist(self);
    return NewsStore_Size(e->serial) >= 0;
}
DECL_FUNCTION(uint32_t, NsData_GetSize, const void* self) {
    SdNsData* e = FindNsData(self);
    if (!e) return real_NsData_GetSize(self);
    int32_t sz = NewsStore_Size(e->serial);
    return sz > 0 ? (uint32_t)sz : 0;
}
DECL_FUNCTION(int32_t, NsData_Read, void* self, void* buf, uint32_t size) {
    SdNsData* e = FindNsData(self);
    if (!e) return real_NsData_Read(self, buf, size);
    int32_t got = NewsStore_Read(e->serial, e->pos, buf, size);
    HookLog("[BossHook] NsData_Read: serial=0x%08X pos=%u req=%u got=%d", e->serial, e->pos, size, got);
    if (got < 0) return (int32_t)BOSS_ERR_NOT_FOUND;
    e->pos += (uint32_t)got;
    return got;
}
DECL_FUNCTION(uint32_t, NsData_ReadEx, void* self, int32_t* out_read, void* buf, uint32_t size) {
    SdNsData* e = FindNsData(self);
    if (!e) return real_NsData_ReadEx(self, out_read, buf, size);
    int32_t got = NewsStore_Read(e->serial, e->pos, buf, size);
    HookLog("[BossHook] NsData_ReadEx: serial=0x%08X pos=%u req=%u got=%d", e->serial, e->pos, size, got);
    if (got < 0) return BOSS_ERR_NOT_FOUND;
    e->pos += (uint32_t)got;
    if (out_read) *out_read = got;
    return BOSS_OK;
}
DECL_FUNCTION(uint32_t, NsData_Seek, void* self, int32_t pos, int base) {
    SdNsData* e = FindNsData(self);
    if (!e) return real_NsData_Seek(self, pos, base);
    int32_t size = NewsStore_Size(e->serial);
    int64_t start = base == 0 ? 0 : base == 1 ? (int64_t)e->pos : (int64_t)size;   // BEGIN/CURRENT/END
    int64_t np = start + pos;
    if (np < 0 || np > size) return BOSS_ERR_NOT_FOUND;
    e->pos = (uint32_t)np;
    return BOSS_OK;
}
DECL_FUNCTION(int32_t, NsData_GetPosition, const void* self) {
    SdNsData* e = FindNsData(self);
    if (!e) return real_NsData_GetPosition(self);
    return (int32_t)e->pos;
}
DECL_FUNCTION(bool, NsData_GetReadFlag, const void* self) {
    SdNsData* e = FindNsData(self);
    if (!e) return real_NsData_GetReadFlag(self);
    return NewsStore_GetRead(e->serial);
}
DECL_FUNCTION(uint32_t, NsData_SetReadFlag, void* self, bool flag) {
    SdNsData* e = FindNsData(self);
    if (!e) return real_NsData_SetReadFlag(self, flag);
    NewsStore_SetRead(e->serial, flag);
    return BOSS_OK;
}
DECL_FUNCTION(bool, NsData_GetDeleteProtection, const void* self) {
    if (!FindNsData(self)) return real_NsData_GetDeleteProtection(self);
    return false;
}
DECL_FUNCTION(uint32_t, NsData_SetDeleteProtection, void* self, bool flag) {
    if (!FindNsData(self)) return real_NsData_SetDeleteProtection(self, flag);
    return BOSS_OK;
}

#define SD_DELETE_HOOK(fn)                                             \
    DECL_FUNCTION(uint32_t, fn, void* self) {                          \
        SdNsData* e = FindNsData(self);                                \
        if (!e) return real_##fn(self);                                \
        return NewsStore_Delete(e->serial) ? BOSS_OK : BOSS_ERR_NOT_FOUND; \
    }
SD_DELETE_HOOK(NsData_Delete)
SD_DELETE_HOOK(NsData_DeleteWithHistory)
SD_DELETE_HOOK(NsData_DeleteRealFile)
SD_DELETE_HOOK(NsData_DeleteRealFileWithHistory)

/* ------------------------------------------------------------------------- */
/* Registration                                                              */
/* ------------------------------------------------------------------------- */

#define BOSS_HOOK(fn, sym) WUPS_MUST_REPLACE_FOR_PROCESS(fn, WUPS_LOADER_LIBRARY_NN_BOSS, sym, WUPS_FP_TARGET_PROCESS_ALL)

BOSS_HOOK(Storage_Init_DirKind,      Initialize__Q3_2nn4boss7StorageFPCcQ3_2nn4boss11StorageKind);
BOSS_HOOK(Storage_Init_DirPidKind,   Initialize__Q3_2nn4boss7StorageFPCcUiQ3_2nn4boss11StorageKind);
BOSS_HOOK(Storage_Init_SlotDirKind,  Initialize__Q3_2nn4boss7StorageFUcPCcQ3_2nn4boss11StorageKind);
BOSS_HOOK(Storage_Ct_DirKind,        __ct__Q3_2nn4boss7StorageFPCcQ3_2nn4boss11StorageKind);
BOSS_HOOK(Storage_Ct_DirPidKind,     __ct__Q3_2nn4boss7StorageFPCcUiQ3_2nn4boss11StorageKind);
BOSS_HOOK(Storage_Ct_SlotDirKind,    __ct__Q3_2nn4boss7StorageFUcPCcQ3_2nn4boss11StorageKind);
BOSS_HOOK(Storage_Ct_Copy,           __ct__Q3_2nn4boss7StorageFRCQ3_2nn4boss7Storage);
BOSS_HOOK(Storage_Finalize,          Finalize__Q3_2nn4boss7StorageFv);
BOSS_HOOK(Storage_Dt,                __dt__Q3_2nn4boss7StorageFv);
BOSS_HOOK(Storage_GetDataList,       GetDataList__Q3_2nn4boss7StorageCFPQ3_2nn4boss8DataNameUiPUiT2);
BOSS_HOOK(Storage_GetUnreadDataList, GetUnreadDataList__Q3_2nn4boss7StorageCFPQ3_2nn4boss8DataNameUiPUiT2);
BOSS_HOOK(Storage_GetReadFlags,      GetReadFlagFromNsDatas__Q3_2nn4boss7StorageCFPCQ3_2nn4boss8DataNameUiPb);
BOSS_HOOK(Storage_SetReadFlagsAll,   SetReadFlagToNsDatas__Q3_2nn4boss7StorageFPCQ3_2nn4boss8DataNameUib);
BOSS_HOOK(Storage_SetReadFlagsEach,  SetReadFlagToNsDatas__Q3_2nn4boss7StorageFPCQ3_2nn4boss8DataNameUiPb);

BOSS_HOOK(NsData_Init,                     Initialize__Q3_2nn4boss6NsDataFRCQ3_2nn4boss7StoragePCc);
BOSS_HOOK(NsData_Ct,                       __ct__Q3_2nn4boss6NsDataFRCQ3_2nn4boss7StoragePCc);
BOSS_HOOK(NsData_Finalize,                 Finalize__Q3_2nn4boss6NsDataFv);
BOSS_HOOK(NsData_Dt,                       __dt__Q3_2nn4boss6NsDataFv);
BOSS_HOOK(NsData_Exist,                    Exist__Q3_2nn4boss6NsDataCFv);
BOSS_HOOK(NsData_GetSize,                  GetSize__Q3_2nn4boss6NsDataCFv);
BOSS_HOOK(NsData_Read,                     Read__Q3_2nn4boss6NsDataFPvUi);
BOSS_HOOK(NsData_ReadEx,                   Read__Q3_2nn4boss6NsDataFPLPvUi);
BOSS_HOOK(NsData_Seek,                     Seek__Q3_2nn4boss6NsDataFLQ3_2nn4boss12PositionBase);
BOSS_HOOK(NsData_GetPosition,              GetPosition__Q3_2nn4boss6NsDataCFv);
BOSS_HOOK(NsData_GetReadFlag,              GetReadFlag__Q3_2nn4boss6NsDataCFv);
BOSS_HOOK(NsData_SetReadFlag,              SetReadFlag__Q3_2nn4boss6NsDataFb);
BOSS_HOOK(NsData_GetDeleteProtection,      GetDeleteProtectionFlag__Q3_2nn4boss6NsDataCFv);
BOSS_HOOK(NsData_SetDeleteProtection,      SetDeleteProtectionFlag__Q3_2nn4boss6NsDataFb);
BOSS_HOOK(NsData_Delete,                   Delete__Q3_2nn4boss6NsDataFv);
BOSS_HOOK(NsData_DeleteWithHistory,        DeleteWithHistory__Q3_2nn4boss6NsDataFv);
BOSS_HOOK(NsData_DeleteRealFile,           DeleteRealFile__Q3_2nn4boss6NsDataFv);
BOSS_HOOK(NsData_DeleteRealFileWithHistory, DeleteRealFileWithHistory__Q3_2nn4boss6NsDataFv);

#endif // __WIIU__
