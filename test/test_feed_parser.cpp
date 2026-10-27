#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "notification_feed.h"
#include "notifications_overlay.h"
#include "drc_led_controller.h"

void test_json_parser_valid() {
    printf("[TEST] Running test_json_parser_valid...\n");
    const char* sample = "{\n"
        "  \"version\": 1,\n"
        "  \"notifications\": [\n"
        "    {\n"
        "      \"id\": \"test-01\",\n"
        "      \"sender\": \"Pretendo\",\n"
        "      \"title\": \"Hello World\",\n"
        "      \"message\": \"System notification revived!\",\n"
        "      \"timestamp\": 1730000000,\n"
        "      \"priority\": 1,\n"
        "      \"duration\": 5.0\n"
        "    }\n"
        "  ]\n"
        "}";

    NotificationFeed feed;
    bool ok = Feed_ParseJSON(sample, strlen(sample), &feed);
    assert(ok == true);
    assert(feed.version == 1);
    assert(feed.count == 1);
    assert(strcmp(feed.items[0].id, "test-01") == 0);
    assert(strcmp(feed.items[0].sender, "Pretendo") == 0);
    assert(strcmp(feed.items[0].title, "Hello World") == 0);
    assert(strcmp(feed.items[0].message, "System notification revived!") == 0);
    assert(feed.items[0].priority == 1);
    assert(feed.items[0].duration_sec == 5.0f);
    printf("  -> PASS\n");
}

void test_seen_state_tracking() {
    printf("[TEST] Running test_seen_state_tracking...\n");
    system("rm -rf ./test_data && mkdir -p ./test_data");

    Feed_Init("./test_data");
    assert(Feed_IsSeen("msg-100") == false);

    Feed_MarkSeen("msg-100");
    assert(Feed_IsSeen("msg-100") == true);
    assert(Feed_IsSeen("msg-200") == false);

    // Re-initialize to verify persistent disk reload
    Feed_Shutdown();
    Feed_Init("./test_data");
    assert(Feed_IsSeen("msg-100") == true);
    assert(Feed_IsSeen("msg-200") == false);

    Feed_Shutdown();
    system("rm -rf ./test_data");
    printf("  -> PASS\n");
}

void test_drc_led_and_overlay_simulation() {
    printf("[TEST] Running test_drc_led_and_overlay_simulation...\n");
    assert(DrcLed_Init() == true);
    DrcLed_TurnOn(1);
    DrcLed_TurnOff();
    DrcLed_Shutdown();

    assert(Overlay_Init() == true);
    assert(Overlay_IsReady() == true);
    bool toast_ok = Overlay_ShowToast("Test Title", "Test Message Body", 3.0f, NOTIFY_STYLE_SPOTPASS_BLUE);
    assert(toast_ok == true);
    Overlay_Shutdown();
    printf("  -> PASS\n");
}

int main() {
    printf("==========================================\n");
    printf(" Running Wii U Community Notification Tests\n");
    printf("==========================================\n");

    test_json_parser_valid();
    test_seen_state_tracking();
    test_drc_led_and_overlay_simulation();

    printf("\n>>> All tests passed successfully! <<<\n");
    return 0;
}
