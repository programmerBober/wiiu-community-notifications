#include "news_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static char s_news_dir[192] = "";

static void MakePath(char* out, size_t sz, uint32_t serial, const char* ext) {
    snprintf(out, sz, "%s/%08x.%s", s_news_dir, (unsigned)serial, ext);
}

static int CmpU32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

void NewsStore_Init(const char* base_dir) {
    mkdir(base_dir, 0777);
    snprintf(s_news_dir, sizeof(s_news_dir), "%s/news", base_dir);
    mkdir(s_news_dir, 0777);
}

void NewsStore_MakeName(uint32_t serial, char out[32]) {
    snprintf(out, 32, NEWS_STORE_NAME_PREFIX "%08x", (unsigned)serial);
}

bool NewsStore_ParseName(const char* name, uint32_t* out_serial) {
    const size_t plen = sizeof(NEWS_STORE_NAME_PREFIX) - 1;
    if (!name || strncmp(name, NEWS_STORE_NAME_PREFIX, plen) != 0) return false;
    char* end = nullptr;
    unsigned long v = strtoul(name + plen, &end, 16);
    if (end == name + plen || *end != '\0') return false;
    if (out_serial) *out_serial = (uint32_t)v;
    return true;
}

uint32_t NewsStore_List(uint32_t* serials, uint32_t max) {
    if (!s_news_dir[0]) return 0;
    DIR* d = opendir(s_news_dir);
    if (!d) return 0;
    uint32_t tmp[NEWS_STORE_MAX * 2];
    uint32_t n = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr && n < sizeof(tmp) / sizeof(tmp[0])) {
        const char* dot = strrchr(e->d_name, '.');
        if (!dot || strcmp(dot, ".bin") != 0 || dot - e->d_name != 8) continue;
        char* end = nullptr;
        unsigned long v = strtoul(e->d_name, &end, 16);
        if (end != dot) continue;
        tmp[n++] = (uint32_t)v;
    }
    closedir(d);
    qsort(tmp, n, sizeof(uint32_t), CmpU32);
    uint32_t out = n < max ? n : max;
    if (serials) memcpy(serials, tmp, out * sizeof(uint32_t));
    return out;
}

bool NewsStore_Save(const uint8_t* buf, uint32_t len, uint32_t* out_serial) {
    if (!s_news_dir[0] || !buf || len == 0) return false;

    uint32_t list[NEWS_STORE_MAX * 2];
    uint32_t n = NewsStore_List(list, sizeof(list) / sizeof(list[0]));
    uint32_t serial = n ? list[n - 1] + 1 : 1;

    char tmp_path[256], bin_path[256];
    MakePath(tmp_path, sizeof(tmp_path), serial, "tmp");
    MakePath(bin_path, sizeof(bin_path), serial, "bin");

    FILE* f = fopen(tmp_path, "wb");
    if (!f) return false;
    bool ok = fwrite(buf, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp_path, bin_path) != 0) {
        remove(tmp_path);
        return false;
    }

    // FIFO trim: keep at most NEWS_STORE_MAX items (n existing + 1 new)
    for (uint32_t i = 0; n + 1 > NEWS_STORE_MAX && i < n; i++, n--) {
        NewsStore_Delete(list[i]);
    }

    if (out_serial) *out_serial = serial;
    return true;
}

int32_t NewsStore_Size(uint32_t serial) {
    char path[256];
    MakePath(path, sizeof(path), serial, "bin");
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int32_t)st.st_size;
}

int32_t NewsStore_Read(uint32_t serial, uint32_t offset, void* buf, uint32_t len) {
    char path[256];
    MakePath(path, sizeof(path), serial, "bin");
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    int32_t got = -1;
    if (fseek(f, (long)offset, SEEK_SET) == 0) {
        got = (int32_t)fread(buf, 1, len, f);
    }
    fclose(f);
    return got;
}

bool NewsStore_GetRead(uint32_t serial) {
    char path[256];
    MakePath(path, sizeof(path), serial, "read");
    struct stat st;
    return stat(path, &st) == 0;
}

void NewsStore_SetRead(uint32_t serial, bool read) {
    char path[256];
    MakePath(path, sizeof(path), serial, "read");
    if (read) {
        if (NewsStore_GetRead(serial)) return;   // avoid needless SD writes
        FILE* f = fopen(path, "wb");
        if (f) fclose(f);
    } else {
        remove(path);
    }
}

bool NewsStore_Delete(uint32_t serial) {
    char path[256];
    MakePath(path, sizeof(path), serial, "read");
    remove(path);
    MakePath(path, sizeof(path), serial, "bin");
    return remove(path) == 0;
}

uint32_t NewsStore_ClearAll(void) {
    uint32_t list[NEWS_STORE_MAX * 2];
    uint32_t n = NewsStore_List(list, sizeof(list) / sizeof(list[0]));
    uint32_t removed = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (NewsStore_Delete(list[i])) removed++;
    }
    return removed;
}
