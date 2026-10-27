# 🎮 Wii U Community Notification System (SpotPass Revived)

Bring back the authentic magic of the Wii U! Walk into your room, see the GamePad glowing with a breathing blue LED around the HOME button, hear the chime, and see lively community announcements, Splatfest bulletins, game nights, and server updates right as you boot your console.

---

## 🔬 Research & System Architecture

### 1. The Original Wii U Notification Feel
* **GamePad Notification Ring**: In Cafe OS, background events (SpotPass, friends coming online) pulsed the blue DRC LED around the HOME button.
* **On-Screen Popups**: Non-intrusive cyan/blue system toasts in the corner of the TV and GamePad.
* **Notification Applet**: System Title ID `00050010-1004A000` (EUR) / `1004A100` (USA) loaded BOSS notifications.

### 2. Verified Wii U Homebrew APIs
* **Toast Overlays (`wiiu-env/libnotifications` & `NotificationModule`)**:
  - Connects via `NotificationModule_InitLibrary()`.
  - Pushes toasts using `NotificationModule_AddInfoNotificationEx()` with authentic Wii U cyan/blue styling (`RGBA: 0, 162, 232, 240`).
  - Supports persistence (`keepUntilShown = true`) so notifications survive game transitions without being dropped.
* **GamePad HOME Button LED (`nn::acp` & `nn::act`)**:
  - Controlled natively via `<nn/acp/drcled_c.h>`:
    ```c
    uint32_t persistentId = nn::act::GetPersistentId();
    ACPTurnOnDrcLed(persistentId, 1); // Pattern 1: Iconic breathing notification pulse
    ACPTurnOffDrcLed();
    ```
* **Hardware Safety**:
  - 100% RAM-only WUPS plugin.
  - Zero modifications to NAND/MLC storage. State and history are stored strictly on SD card (`sd:/wiiu/notifications/`).

---

## 🗺️ Project Roadmap

- [x] **Milestone 1: Core Architecture & Prototype (Current)**
  - Native host simulator & unit test suite with mock LED + toast renderer.
  - JSON feed parser with defensive bounds checking.
  - Seen-state deduplication (`last_seen.txt`) and notification history logger (`history.log`).
  - WUPS plugin implementation hooks (`INITIALIZE_PLUGIN`, background `OSThread`).
- [ ] **Milestone 2: Network Fetcher & WUPS Config Menu**
  - Background HTTP client (`nsysnet` / `libcurl`) to fetch live JSON feeds from Pretendo / Community endpoints.
  - In-game WUPS configuration menu (`L + D-Pad Down + Minus`) to toggle LED pulse, test notifications, and adjust poll intervals.
- [ ] **Milestone 3: Standalone Notification Viewer Applet (.wuhb)**
  - A nostalgic HOME menu companion applet replicating the original Wii U notification list UI to browse past announcements.
- [ ] **Milestone 4: Pretendo Community Feed Integration**
  - Dedicated public API feed syncing official Pretendo status, maintenance alerts, and game tournaments.

---

## 🚀 Building & Running

### 1. Desktop Simulator & Unit Tests
You can compile and test the full notification pipeline directly on your PC:
```bash
# Run unit tests
make test

# Run interactive desktop simulator with sample feed
make run
```

### 2. Wii U Console Build (Aroma CFW)
Using devkitPPC & WUPS rules:
```bash
export DEVKITPRO=/opt/devkitpro
make wiiu
```
Or via Docker:
```bash
docker run --rm -v $(pwd):/project ghcr.io/wiiu-env/cflpack:latest make -f Makefile.wiiu
```

### 3. Installation to Wii U SD Card
1. Copy `CommunityNotifications.wups` to `sd:/wiiu/environments/aroma/plugins/`.
2. Ensure `NotificationModule.wms` is in `sd:/wiiu/environments/aroma/modules/` (standard in Aroma).
3. Place `server/feed_example.json` into `sd:/wiiu/notifications/feed.json`.
4. Power on your Wii U and enjoy!
