# 🎮 Wii U Community Notification System (SpotPass Revived)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: Wii U / Aroma](https://img.shields.io/badge/Platform-Wii%20U%20%7C%20Aroma%20CFW-informational.svg)](https://aroma.foryour.cafe)
[![Toolchain: devkitPPC & wut](https://img.shields.io/badge/Toolchain-devkitPPC%20%2B%20wut-green.svg)](https://github.com/devkitPro/wut)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()

> [!WARNING]
> **⚠️ VIBE CODED & EXPERIMENTAL — ACTIVE RESEARCH**
>
> This repository is an experimental prototype exploring Wii U SpotPass notification injection, GamePad DRC LED breathing patterns, and Aroma overlay rendering. It is in active research/early development and **not ready for daily end-user setups**. Use for testing and research purposes only.

Bring back the authentic magic of the Wii U! Walk into your room, see the GamePad glowing with an iconic breathing cyan/blue LED around the HOME button, hear the chime, and see lively community announcements, Splatfest bulletins, game nights, and desktop alerts right when you boot your console.

---

## 🏛️ System Architecture

```mermaid
flowchart TD
    subgraph Desktop [💻 Desktop / Server Bridge]
        CLI["tools/wiiu_notify.py"]
        PretendoAPI["Pretendo Incident API"]
        DesktopAlerts["Desktop Push Alerts"]
        CLI --> PretendoAPI
        CLI --> DesktopAlerts
    end

    subgraph WiiU [🕹️ Wii U Console (Aroma CFW)]
        TCP["TCP Listener (:7331)"]
        TestApp["NotifyTest (.wuhb)"]
        Plugin["CommunityNotifications (.wps)"]
        Boss["nn_boss.rpl (SpotPass Engine)"]
        OfficialMenu["Official Notification Menu (inf.rpx)"]
        GamePadLED["GamePad DRC LED (nn::acp)"]
        Toast["Overlay Toasts (NotificationModule)"]

        CLI -->|WiFi Push| TCP
        TCP --> TestApp
        TestApp -->|PostNews IPC| Boss
        Boss --> OfficialMenu
        TestApp --> GamePadLED
        Plugin --> Toast
        Plugin --> GamePadLED
    end
```

---

## ✨ Features & Capabilities

* **🏮 GamePad HOME Button Pulse**: Direct hardware control over `<nn/acp/drcled_c.h>` to pulse the GamePad HOME ring with the authentic SpotPass breathing pattern.
* **📬 Official Nintendo Notification Menu Injection**: Direct dynamic linkage to `nn_boss.rpl`'s `nn::boss::News::PostNews()`, feeding compressed XML items directly into Nintendo's notification applet (`inf.rpx`, Title ID `00050010-1004D200`).
* **🎮 Native App Jump Integration**: Injected notifications include `<AppLink>` metadata, rendering the official "Start Software" icon to jump straight into Splatoon, Minecraft, or Mario Kart 8 directly from the notification board!
* **🖥️ Desktop Notification Bridge**: Send real-time messages and announcements directly from Linux/macOS/Windows over TCP port `7331`.
* **🌐 Pretendo Network Incident Alerts**: Automatically sync and display official Pretendo service alerts and maintenance bulletins on your GamePad.
* **🛡️ Non-Volatile RAM Safety**: Clean in-memory execution via WUPS and Aroma applets. Zero MLC/NAND modifications.

---

## 🔬 Reverse Engineering Breakthroughs

Comprehensive reverse-engineering notes are documented in [`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md):
* **XML Schema**: Disassembled `inf.elf` and documented the official UTF-16 element tree (`<Message>`, `<Subject>`, `<Body>`, `<TitleId>`, `<Feeling>`, `<OptoutType>`, `<LinkType>`, `<Links>`).
* **Compression Pipeline**: Identified `inflateInit2_` stream decompression with `windowBits = 47` (auto-detecting zlib/gzip headers).
* **IPC Linkage**: Reversed `nn_boss.rpl` dynamic entry points:
  - `Initialize__Q2_2nn4bossFv()`
  - `PostNews__Q3_2nn4boss4NewsSFPUcUiPUiT2()`
  - `Finalize__Q2_2nn4bossFv()`

---

## 🚀 Components & Usage

### 1. `tools/wiiu_notify.py` (Desktop CLI)
Push notifications from your computer over your local network:
```bash
# Push an instant desktop message with GamePad LED pulse
python3 tools/wiiu_notify.py --ip 192.168.1.100 --title "Discord Message" --body "Game night starting in 10 minutes!"

# Push a game inactivity reminder with App Jump button
python3 tools/wiiu_notify.py --ip 192.168.1.100 --title "Turf War Reminder" --body "Inkopolis is calling!" --app splatoon

# Fetch and broadcast live Pretendo Network incidents
python3 tools/wiiu_notify.py --ip 192.168.1.100 --pretendo
```

### 2. `test_app` (`NotifyTest.wuhb`)
A dedicated on-console testbed and TCP bridge receiver:
* Auto-injects Splatoon reminders on boot.
* Interactive keypad controls:
  - `[A]`: Inject Splatoon reminder with native App Jump button.
  - `[B]`: Inject Minecraft reminder.
  - `[X]`: Inject Pretendo community announcement.
  - `[+]` or `HOME`: Safe, non-blocking exit.
* Listens on TCP port `7331` for incoming desktop push notifications.

### 3. `CommunityNotifications.wps` (Aroma WUPS Plugin)
A lightweight background plugin that monitors `sd:/wiiu/notifications/feed.json` and renders native on-screen toast overlays via `NotificationModule`.

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

## 🗺️ Roadmap

- [x] **Milestone 1: Core Architecture & GamePad LED Control**
- [x] **Milestone 2: Official Notification Applet (`inf.rpx`) Reverse Engineering**
- [x] **Milestone 3: Dynamic `nn_boss` PostNews Injection Engine**
- [x] **Milestone 4: TCP Desktop Bridge & Pretendo Alert CLI**
- [ ] **Milestone 5: Multi-Profile Account Routing & Standalone Background Daemon**

---

## 📜 License
Licensed under the [GNU General Public License v3.0](LICENSE).
