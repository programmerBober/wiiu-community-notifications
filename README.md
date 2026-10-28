# 🎮 Wii U Community Notifications (SpotPass Revived)

<p align="center">
  <em>Native SpotPass Revived for Wii U Aroma CFW & Pretendo Network</em>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPL v3"></a>
  <a href="https://aroma.foryour.cafe"><img src="https://img.shields.io/badge/Platform-Wii%20U%20%7C%20Aroma%20CFW-informational.svg" alt="Platform: Wii U"></a>
  <a href="https://github.com/devkitPro/wut"><img src="https://img.shields.io/badge/Toolchain-devkitPPC%20%2B%20wut-green.svg" alt="Toolchain: devkitPPC + wut"></a>
  <img src="https://img.shields.io/badge/Pretendo-Compatible-purple.svg" alt="Pretendo Compatible">
  <img src="https://img.shields.io/badge/Version-v0.2.0--beta-brightgreen.svg" alt="Version: v0.2.0-beta">
</p>

> [!CAUTION]
> ### ⚠️ WARNING: AI VIBE-CODED BY A DUMBASS — DO NOT USE YET!
> **READ BEFORE USING:** This entire project is AI vibe-coded by an AI assistant that is a stupid piece of shit and way too fucking slow and stupid to create a safe, bugless application.
>
> * **NOT READY TO USE**: This app is **NOT ready yet to use** because the AI writing this code is too stupid and slow to be trusted with console hardware.
> * **BUGS & INCOMPETENCE EVERYWHERE**: Expect bugs, broken logic, and zero guarantees of safety.
> * **DO NOT RUN BLINDLY**: Needs actual intelligent human review before running on your real Wii U. Use completely at your own risk!

Bring back the authentic magic of the Wii U! Walk into your room, see the GamePad glowing with an iconic breathing cyan/blue LED around the HOME button, hear the chime, and see lively community announcements, Splatfest bulletins, game nights, and desktop alerts right when you boot your console.

---

## 🏛️ System Architecture

```mermaid
flowchart TD
    subgraph Desktop ["💻 Desktop / CLI Bridge"]
        CLI["tools/wiiu_notify.py"]
        InjectTool["tools/wiiu_news_inject.py"]
        PretendoAPI["Pretendo Announcements &amp; Forum API"]
        DesktopAlerts["Desktop Push Alerts"]
        CLI --> PretendoAPI
        CLI --> DesktopAlerts
    end

    subgraph WiiU ["🕹️ Wii U Console (Aroma CFW)"]
        TCP["TCP Listener (:7331)"]
        TestApp["NotifyTest (.wuhb)"]
        Plugin["CommunityNotifications (.wps)"]
        BossEngine["Direct MLC BOSS Storage Injector"]
        BossFallback["nn_boss.rpl (PostNews Fallback)"]
        OfficialMenu["Official Notification Menu (inf.rpx)"]
        GamePadLED["GamePad DRC LED (nn::acp)"]

        CLI -->|WiFi Push TCP 7331| TCP
        InjectTool -->|Direct FTP Injection| BossEngine
        TCP --> Plugin
        Plugin --> BossEngine
        Plugin --> BossFallback
        BossEngine --> OfficialMenu
        BossFallback --> OfficialMenu
        Plugin --> GamePadLED
        TestApp --> GamePadLED
    end
```

---

## ✨ Features & Capabilities

* **🏮 GamePad HOME Button Pulse**: Direct hardware control over `<nn/acp/drcled_c.h>` to pulse the GamePad HOME ring with the authentic SpotPass breathing pattern.
* **📬 Official Nintendo Notification Menu Injection**: Injects directly into the official Nintendo Notification Menu (`inf.rpx`, Title ID `00050010-1004D200`), updating `ref_count.db`, `fad.db`, and `didx.db` with zlib-compressed `<Message>` XML.
* **🎮 Native App Jump Integration**: Injected notifications support `<AppLink>` metadata and game title IDs (Splatoon, Minecraft, Mario Kart 8, etc.), rendering the official "Start Software" icon to jump straight into games from the notification board!
* **👤 Auto-Detecting Account Routing**: Automatically discovers the active Pretendo/local account slot via `nn::act::GetPersistentId()`, delivering notifications to the active profile without hardcoded IDs.
* **🖥️ Desktop Notification Bridge**: Send real-time messages and announcements directly from Linux/macOS/Windows over TCP port `7331`.
* **🌐 Pretendo Network Incident Alerts**: Automatically sync and display official Pretendo service alerts and maintenance bulletins on your GamePad.
* **📁 SD Card Feed Poller**: Background polling of `sd:/wiiu/notifications/feed.json` every 5 minutes with deduplication (`last_seen.txt`).

---

## 🔬 Reverse Engineering Breakthroughs

Comprehensive reverse-engineering notes are documented in [`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md):
* **XML Schema**: Disassembled `inf.rpx` and documented the official element tree (`<Message>`, `<Subject>`, `<Body>`, `<TitleId>`, `<HighPriority>`, `<ForceNotify>`, `<Feeling>`, `<OptoutType>`, `<LinkType>`, `<Links>`).
* **Compression Pipeline**: Disassembled `inf.rpx` decompressor routine at `0x209e84c` / `0x209e900` discovering mandatory `inflateInit2(..., 47, ...)` expectation on all stored news items.
* **Storage Database Layout**:
  - `data/news/<serial>`: zlib-compressed UTF-8 `<Message>` XML.
  - `data/news/ref_count.db`: 8-byte header + 512 entries (0x24 bytes each).
  - `<pid>/news/fad.db`: 8-byte header + 512 entries (0x38 bytes each).
  - `<pid>/news/didx.db`: 128 KB bitmap (bit `serial - 1` MSB-first).
* **Dynamic Account Resolution**: Resolved ABI conventions for `nn::act` and `nn::boss` (`SetNewArrivalFlag__Q3_2nn4boss7AccountFb`).

---

## 🚀 Components & Usage

### 1. `tools/wiiu_notify.py` (Desktop CLI)
Push notifications from your computer over your local network:
```bash
# Push an instant desktop message with GamePad LED pulse
python3 tools/wiiu_notify.py --ip <WIIU_IP> --title "Discord Message" --body "Game night starting in 10 minutes!"

# Push a game inactivity reminder with App Jump button
python3 tools/wiiu_notify.py --ip <WIIU_IP> --title "Turf War Reminder" --body "Inkopolis is calling!" --app splatoon

# Fetch and broadcast live Pretendo Network incidents
python3 tools/wiiu_notify.py --ip <WIIU_IP> --pretendo
```
*(Tip: Set `export WIIU_IP="192.168.x.x"` to omit `--ip` on every command!)*

### 2. `tools/wiiu_news_inject.py` (Direct MLC Storage Injector)
Directly injects zlib-compressed notifications into the Wii U Notification applet's BOSS storage over FTP with auto-detected account slot:
```bash
python3 tools/wiiu_news_inject.py --ip <WIIU_IP> --title "Pretendo" --body "Welcome to the revived SpotPass network!" --feeling HAPPY
```

### 3. `NotifyTest.wuhb` (On-Console Testbed & Standalone App)
A standalone Aroma homebrew app to verify injection, LED pulsing, and socket reception:
* Auto-injects Splatoon reminders on boot.
* Interactive keypad controls:
  - `[A]`: Inject Splatoon reminder with native App Jump button.
  - `[B]`: Inject Minecraft reminder.
  - `[X]`: Inject Pretendo community announcement.
  - `[+]` or `HOME`: Safe, non-blocking exit.
* Listens on TCP port `7331` for incoming desktop push notifications.

### 4. `CommunityNotifications.wps` (Aroma WUPS Background Plugin)
A silent background plugin that runs across the Wii U Menu and in-game:
* Hosts a live TCP server on port `7331` for instant alerts.
* Polls `sd:/wiiu/notifications/feed.json` every 5 minutes.
* Streams updates directly to BOSS storage and sets the unread notification arrival badge.

---

## 🛠️ Building from Source

### Prerequisites
* Docker **or** devkitPPC with `wut`, `wups`, and `libnotifications`.

### Compile `NotifyTest.wuhb`
```bash
docker run --rm -v $(pwd)/test_app:/project -w /project wiiu-builder make clean && make
```

### Compile `CommunityNotifications.wps`
```bash
docker run --rm -v $(pwd):/project -w /project wiiu-builder make -f Makefile.wiiu clean && make -f Makefile.wiiu
```

---

## 🗺️ Roadmap & Upcoming Goals

### Completed Milestones
- [x] **Milestone 1: Core Architecture & GamePad LED Hardware Control**
  - [x] Native Cafe OS GamePad HOME ring breathing cyan LED alert (`nn::acp`).
- [x] **Milestone 2: Official Notification Applet (`inf.rpx`) Reverse Engineering**
  - [x] Disassembled Cafe OS decompression routine discovering mandatory `inflateInit2(47)` zlib compression.
  - [x] Documented official UTF-16 `<Message>` XML schema and icon title ID routing.
- [x] **Milestone 3: Direct MLC BOSS Storage Injector Engine**
  - [x] High-performance, streaming C++ engine updating `data/news/`, `ref_count.db`, `fad.db`, and `didx.db`.
- [x] **Milestone 4: Real-Time TCP Desktop Bridge & Pretendo Alert CLI**
  - [x] Live TCP listener on port `7331` for PC/server alerts.
- [x] **Milestone 5: Dynamic Multi-Profile Account Routing**
  - [x] Dynamic account slot discovery via `nn::act::GetPersistentId()` without static profile IDs.
- [x] **Milestone 6: System & In-Game Pause Menu Indicators**
  - [x] GamePad physical button light: Breathing cyan LED around the HOME ring.
  - [x] HOME Menu (`hbm.rpx`) in-game pause screen indicator: Activated `nn::boss::AlmightyTitle::SetNewArrivalMarkFlag` to start the authentic `NewsGlow` animation sequence when pausing in-game.
  - [x] Wii U Main Menu (`men.rpx`) unread arrival indicator badge sync via `nn::boss::Account::SetNewArrivalFlag`.
- [x] **Milestone 7: Native Aroma Plugin Configuration Menu (WUPS Config API)**
  - [x] Interactive in-game overlay menu accessible anytime via `L + D-Pad Down + Minus`.
  - [x] Toggle Autonomous Delivery (Zero-PC mode) on/off.
  - [x] Toggle GamePad HOME button LED alert on/off.
  - [x] Toggle on-screen toast overlays on/off.
  - [x] Configurable delivery / check interval (30s testing / 1m / 5m default / 15m / 30m / 1h).
  - [x] Toggle PC Network Receiver (Port 7331) on/off.
  - [x] Persistent configuration stored to SD via `WUPSStorageAPI`.
- [x] **Milestone 8: Autonomous On-Console Delivery (Zero-PC Mode)**
  - [x] Built-in autonomous rotation engine delivering authentic community bulletins locally on console without a PC.
  - [x] Curated game catalog rotation with direct App Jump links (Splatoon Inkopolis News, Pretendo Network Status, Mario Kart 8 GP Tournament, Minecraft Battle Minigame, Super Mario Maker Course World).
  - [x] Direct background MLC injection, badge sync, and SD card history logging.
- [x] **Milestone 9: Smart GamePad LED Input Dismissal & Auto-Off Timer (Beta)**
  - [x] Non-intrusive GamePad HOME button LED with configurable auto-off timer (5s, 10s default, 30s, 60s).
  - [x] Hardware input dismissal: touching the touchscreen or pressing any button instantly extinguishes the LED.
  - [x] Full multi-profile sync across all registered accounts (`fad.db` and `didx.db`) preventing orphan unread alert loops.
  - [x] Fast-path TCP remote control commands (`led_off`, `led_on`, `clear`).
- [x] **Milestone 10: Real-Time Pretendo Network Forum & Blog Scraper**
  - [x] Live Discourse API crawler (`tools/pretendo_scraper.py`) fetching real-time announcements without authentication or rate limits.
  - [x] Official Pretendo blog RSS integration with heuristic game mapping and Mii emotional expressions.
- [x] **Milestone 11: Expanded Notification Types & Custom Styled Overlays**
  - [x] 6 distinct notification categories: `game`, `maintenance`, `community`, `friend`, `achievement`, and `system`.
  - [x] 14 rich presets for all installed games (Splatoon, Mario Kart 8, Smash, Mario Maker, Wind Waker HD, NSMBU, Minecraft, FAST Racing, Star Fox Zero, Isaac).
  - [x] Color-coded on-screen toast overlays dynamically matched to notification categories (Blue, Red, Green, Purple, Amber, Dark).

---

## 💻 Public Desktop & CLI Tools

The `tools/` directory provides open-source, zero-dependency Python tools to dispatch notifications to any Wii U running the plugin:

### 1. `tools/wiiu_notify.py` — Desktop Notification Bridge
```bash
# Push a preset game announcement
python3 tools/wiiu_notify.py --ip <WIIU_IP> --preset mariokart

# Push a custom achievement toast
python3 tools/wiiu_notify.py --ip <WIIU_IP> --type achievement --title "World Record!" --body "0:34.120 on Galactic Starway" --app smm

# Turn off GamePad LED remotely
python3 tools/wiiu_notify.py --ip <WIIU_IP> --cmd led_off
```

### 2. `tools/pretendo_scraper.py` — Live Pretendo Scraper & Sync
```bash
# Scrape live Pretendo announcements and inject directly into the Notification Menu
python3 tools/pretendo_scraper.py --ip <WIIU_IP> --method inject

# Run as an autonomous background daemon polling every 5 minutes
python3 tools/pretendo_scraper.py --ip <WIIU_IP> --method inject --daemon --interval 300
```

### 3. `tools/wiiu_news_inject.py` — Multi-Profile BOSS Storage Injector
```bash
python3 tools/wiiu_news_inject.py --ip <WIIU_IP> --title "Notice" --body "Direct MLC injection" --feeling HAPPY
```

---

## 📜 License
Licensed under the [GNU General Public License v3.0](LICENSE).
