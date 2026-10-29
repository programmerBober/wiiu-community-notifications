// Host unit test for the SD-backed news store (news_store.cpp).
#include "news_store.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int s_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

int main() {
    const char* base = "./test_data/sd_news";
    system("rm -rf ./test_data/sd_news && mkdir -p ./test_data");
    NewsStore_Init(base);

    printf("[news_store] save / list / read\n");
    const uint8_t a[] = "payload-A", b[] = "payload-BB";
    uint32_t sa = 0, sb = 0;
    CHECK(NewsStore_Save(a, sizeof(a), &sa));
    CHECK(NewsStore_Save(b, sizeof(b), &sb));
    CHECK(sa == 1 && sb == 2);
    uint32_t list[NEWS_STORE_MAX];
    CHECK(NewsStore_List(list, NEWS_STORE_MAX) == 2);
    CHECK(list[0] == 1 && list[1] == 2);
    CHECK(NewsStore_Size(sb) == (int32_t)sizeof(b));
    char buf[32] = {0};
    CHECK(NewsStore_Read(sb, 0, buf, sizeof(buf)) == (int32_t)sizeof(b));
    CHECK(memcmp(buf, b, sizeof(b)) == 0);
    CHECK(NewsStore_Read(sb, 8, buf, 2) == 2 && memcmp(buf, "BB", 2) == 0);
    CHECK(NewsStore_Size(99) == -1);

    printf("[news_store] read flags\n");
    CHECK(!NewsStore_GetRead(sa));
    NewsStore_SetRead(sa, true);
    CHECK(NewsStore_GetRead(sa));
    NewsStore_SetRead(sa, false);
    CHECK(!NewsStore_GetRead(sa));

    printf("[news_store] names\n");
    char name[32];
    NewsStore_MakeName(0x2a, name);
    CHECK(strcmp(name, "sdcn0000002a") == 0);
    uint32_t parsed = 0;
    CHECK(NewsStore_ParseName(name, &parsed) && parsed == 0x2a);
    CHECK(!NewsStore_ParseName("0000002a", nullptr));
    CHECK(!NewsStore_ParseName("sdcn", nullptr));
    CHECK(!NewsStore_ParseName("sdcn12zz", nullptr));

    printf("[news_store] delete + FIFO trim at %d\n", NEWS_STORE_MAX);
    CHECK(NewsStore_Delete(sa));
    CHECK(NewsStore_List(list, NEWS_STORE_MAX) == 1);
    for (int i = 0; i < NEWS_STORE_MAX + 5; i++) CHECK(NewsStore_Save(a, sizeof(a), nullptr));
    uint32_t n = NewsStore_List(list, NEWS_STORE_MAX);
    CHECK(n == NEWS_STORE_MAX);
    CHECK(list[n - 1] == 2 + NEWS_STORE_MAX + 5);   // newest kept
    CHECK(list[0] == list[n - 1] - NEWS_STORE_MAX + 1); // oldest trimmed

    printf("[news_store] no stray .tmp files\n");
    CHECK(system("ls ./test_data/sd_news/news | grep -q '\\.tmp$'") != 0);

    printf("[news_store] clear all\n");
    CHECK(NewsStore_ClearAll() == NEWS_STORE_MAX);
    CHECK(NewsStore_List(list, NEWS_STORE_MAX) == 0);

    printf(s_fail ? "news_store: %d FAILURE(S)\n" : "news_store: ALL PASSED\n", s_fail);
    return s_fail ? 1 : 0;
}
