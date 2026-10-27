# 🔬 Wii U Notification Menu (InfProduct) Reverse Engineering Log

**Date**: 2026-10-27  
**Target Title ID**: `00050010-1004D200` (Notifications EUR)  
**Binary**: `inf.rpx` -> `inf.elf` (4.9 MB PowerPC ELF)  
**Layout Archive**: `Inf.pack` (SARC container, 6.0 MB)  
**Sound Archive**: `cafe_barista_inf.bfsar`  
**Internal Project Name**: `Cafe/Barista/Application/Exe/Cafe/InfProduct/inf.rpx`  

---

## 🏛️ System Architecture

### 1. Framework & Engine
* Built with Nintendo's **sead** cross-platform engine + **agl** (rendering) + **nw::lyt** (layout runtime).
* Graphics pipeline: Full GX2 with custom shaders (`gsys_model_shader_cafe.szs`, `primitive_renderer_cafe.gsh`).

### 2. UI Layout Components (Extracted from `Inf.pack`)
* `InformationBoard_00.szs`:
  - `blyt/InformationBoard_00.bflyt`: Main notification board layout.
  - `anim/InformationBoard_00_In.bflan`: Entrance slide animation.
  - `anim/InformationBoard_00_Out.bflan`: Exit slide animation.
  - `timg/WindowBasic_11^s.bflim`: Iconic Wii U rounded translucent glass window texture.
* `BtnInfList.szs`: Notification list card elements (`N_BtnInfList_%02d`).
* `InfDetail.szs` & `InfDetailBar.szs`: Expanded full-text reader view.
* `BtnInfAppJumpIcon.szs`: Launch button to jump directly into a game (e.g., Splatoon, Mario Kart).

### 3. Backend Data Flow (`nn::boss`)
* Applet initializes:
  ```cpp
  nn::boss::Initialize();
  nn::boss::Storage storage;
  storage.Initialize("news", ...);
  storage.GetDataList(&dataNames, maxCount, &actualCount, ...);
  ```
* For each item:
  ```cpp
  nn::boss::NsData item(storage, name);
  bool isRead = item.GetReadFlag();
  uint32_t sz = item.GetSize();
  item.Read(buffer, sz);
  ```

---

## 🔍 Data Payload & XML Schema

### 4. Compression Pipeline
* After reading raw bytes via `nn::boss::NsData::Read()`, `inf.rpx` invokes `inflateInit2_` at `0x0209e8c8` with `windowBits = 47` (`15 + 32`, auto-detect standard zlib and gzip streams).
* Payload is decompressed into an 864 KB scratch buffer.

### 5. XML Document Structure
* The parser (`nn::nlib::exi::text::XmlTextStreamReader`) reads UTF-16 encoded XML element tags:
  - `<Message>`: Document root.
  - `<Subject>`: Title displayed on the notification card and header.
  - `<Body>`: Detailed description displayed in reader view.
  - `<TitleId>`: 16-character hexadecimal Title ID of target game/app.
  - `<HighPriority>`: `true` / `false`.
  - `<ForceNotify>`: `true` / `false` (wakes GamePad HOME button LED).
  - `<Feeling>`: Mii emotion icon (`HAPPY`, `PUZZLED`, `SURPRISED`, `LIKE`, `FRUSTRATED`, `NORMAL`).
  - `<OptoutType>`: `NEVER`, `BOSS`, `LOCAL`.
  - `<LinkType>`: `APP`, `SURF`, `WOOD` (eShop), `VINO` (Wii U Chat), `OLIVE` (Miiverse), `NONE`.
  - `<Links><AppLink><TitleId>...</TitleId><Parameter>...</Parameter></AppLink></Links>`: Enables "Start Application" App Jump icon.

### Example SpotPass Payload:
```xml
<?xml version="1.0" encoding="utf-8"?>
<Message>
  <Subject>Splatoon Reminder</Subject>
  <Body>Inkopolis is calling! It has been a while since your last Turf War. Jump back into the lobby!</Body>
  <TitleId>0005000010176A00</TitleId>
  <HighPriority>true</HighPriority>
  <ForceNotify>true</ForceNotify>
  <Feeling>HAPPY</Feeling>
  <OptoutType>NEVER</OptoutType>
  <LinkType>APP</LinkType>
  <Links>
    <AppLink>
      <TitleId>0005000010176A00</TitleId>
      <Parameter></Parameter>
    </AppLink>
  </Links>
</Message>
```

---

## 💉 Direct Injection Engine (`PostNews`)

### 6. Dynamic Entry Point in `nn_boss.rpl`
* Nintendo's Background Online Service System exposes a direct C++ export:
  `nn::boss::News::PostNews(const uint8_t* buffer, uint32_t size)`
  (Symbol `PostNews__Q3_2nn4boss4NewsSFPUcUi`).
* Lifecycle:
  1. `OSDynLoad_Acquire("nn_boss.rpl", &mod)`
  2. `Initialize__Q2_2nn4bossFv()`
  3. `PostNews__Q3_2nn4boss4NewsSFPUcUi(compressed_zlib_data, compressed_size)`
  4. `Finalize__Q2_2nn4bossFv()`
* This immediately inserts the news item into `/storage_mlc/usr/boss/00050010/1004d200/user/common/<UserID>/news/` and updates the native unread database.

---

## 🛠️ Freezing Bug Fix (Profile 1 vs Profile 2)

### 7. Root Cause
* Profile 1 (`80000012`, original console NNID) had:
  - `NeedsToDownloadMiiImage = 1` pointing to shutdown `https://mii-secure.account.nintendo.net/` in `account.dat`.
  - Dead endpoints in `task.db` (`https://npvk.app.nintendo.net/reports`).
* Synchronous connection calls blocked `inf.rpx` before the main rendering loop started.
* Profile 2 (`80000014`, Pretendo linked) did not freeze because its endpoints resolve to Pretendo CDN.

### 8. Resolution
* Patched `account.dat` setting `NeedsToDownloadMiiImage = 0`.
* Patched `task.db` replacing dead Nintendo reporting URL with `https://npvk.app.pretendo.cc/reports`.
* Profile 1 now launches the notification applet cleanly without hanging.

---

## 🌉 Desktop Bridge & Pretendo Community Notifier

### 9. Python Desktop Client (`tools/wiiu_notify.py`)
* **Real-Time Mode**: Connects to Wii U over TCP port `7331` to deliver desktop alerts (Discord, Steam, system events).
* **Asynchronous Fallback**: Drops notifications into `sd:/wiiu/notifications/feed.json` via FTPiiU when the console is in other software.
* **Pretendo Feed Fetcher**: Queries Pretendo community APIs to inject network maintenance and Splatfest announcements.
