#!/usr/bin/env python3
"""
Pretendo Community Scraper & SpotPass Synchronizer
--------------------------------------------------
Fetches official Pretendo Network announcements, blog updates,
and service alerts, formatting them into native Wii U SpotPass notifications.

Sources:
  1. Discourse Announcements API (forum.pretendo.network/c/announcements/9.json)
  2. Pretendo Official Blog RSS (pretendo.network/blog/feed.xml)
"""

import argparse
import ftplib
import html
import io
import json
import os
import re
import socket
import sys
import time
import urllib.request
import xml.etree.ElementTree as ET

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.abspath(os.path.join(BASE_DIR, ".."))
SEEN_FILE = os.path.join(PROJECT_DIR, "data", "pretendo_seen.json")

# Verified Wii U Title IDs
TITLE_MAP = {
    "splatoon": "0005000010176A00",
    "minecraft": "00050000101D7500",
    "isaac": "00050000101C9B00",
    "fast racing": "00050000101D6000",
    "fast": "00050000101D6000",
    "star fox": "00050000101B0500",
    "mariokart": "000500001010EC00",
    "mario kart": "000500001010EC00",
    "smash": "0005000010145000",
    "miiverse": "0005001010040200",
    "juxtaposition": "0005001010040200",
    "wii u menu": "0005001010040200",
    "notification": "000500101004D200"
}

HTTP_HEADERS = {
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64; rv:120.0) Gecko/20100101 Firefox/120.0"
}

def clean_text(raw_html, max_len=450):
    """Strip HTML, markdown formatting, emoji shortcodes, and normalize whitespace."""
    if not raw_html:
        return ""
    # Strip HTML tags
    text = re.sub(r"<[^>]+>", " ", raw_html)
    text = html.unescape(text)
    # Strip Discourse custom emojis like :3ds: or :owner:
    text = re.sub(r":[a-zA-Z0-9_\+\-]+:", "", text)
    # Strip markdown links [text](url) -> text
    text = re.sub(r"\[([^\]]+)\]\([^\)]+\)", r"\1", text)
    # Strip markdown headers/bold/italics
    text = re.sub(r"[#*_`~>]+", "", text)
    # Normalize whitespace
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) > max_len:
        text = text[:max_len - 3] + "..."
    return text

def infer_game_and_feeling(title, body):
    combined = (title + " " + body).lower()
    app = "000500101004D200" # Default: Notification Menu
    for key, tid in TITLE_MAP.items():
        if key in combined:
            app = tid
            break

    # Determine notification category type, Mii feeling, and priority
    feeling = "HAPPY"
    priority = 1
    notif_type = "game"

    if any(k in combined for k in ["breach", "alert", "security", "exploit", "compromise"]):
        feeling = "SURPRISED"
        priority = 2
        notif_type = "maintenance"
    elif any(k in combined for k in ["maintenance", "down", "offline", "outage", "issue", "bug"]):
        feeling = "PUZZLED"
        priority = 2
        notif_type = "maintenance"
    elif any(k in combined for k in ["juxtaposition", "miiverse", "drawing", "yeah", "post"]):
        feeling = "LIKE"
        priority = 1
        notif_type = "community"
    elif any(k in combined for k in ["lobby", "invite", "friends", "join", "party"]):
        feeling = "HAPPY"
        priority = 1
        notif_type = "friend"
    elif any(k in combined for k in ["record", "cleared", "high score", "ranked", "rank"]):
        feeling = "LIKE"
        priority = 1
        notif_type = "achievement"
    elif any(k in combined for k in ["update", "release", "patch", "version", "firmware"]):
        feeling = "HAPPY"
        priority = 1
        notif_type = "system"
    elif any(k in combined for k in ["rule", "conduct", "terms", "policy"]):
        feeling = "NORMAL"
        priority = 1
        notif_type = "community"

    return app, feeling, priority, notif_type

def fetch_forum_announcements(limit=5):
    """Fetch recent topics from Discourse Announcements category (id: 9)."""
    items = []
    url = "https://forum.pretendo.network/c/announcements/9.json"
    try:
        req = urllib.request.Request(url, headers=HTTP_HEADERS)
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            topics = data.get("topic_list", {}).get("topics", [])

        # Sort topics: ignore pinned topics if old, sort by created_at descending
        sorted_topics = sorted(topics, key=lambda x: x.get("created_at", ""), reverse=True)

        for topic in sorted_topics[:limit]:
            tid = topic.get("id")
            title = topic.get("title", "")
            if not tid or not title:
                continue

            # Fetch post content for body preview
            body = ""
            topic_url = f"https://forum.pretendo.network/t/{tid}.json"
            try:
                t_req = urllib.request.Request(topic_url, headers=HTTP_HEADERS)
                with urllib.request.urlopen(t_req, timeout=8) as t_resp:
                    t_data = json.loads(t_resp.read().decode("utf-8"))
                    posts = t_data.get("post_stream", {}).get("posts", [])
                    if posts:
                        body = clean_text(posts[0].get("cooked", ""))
            except Exception as e:
                body = clean_text(title)

            app, feeling, priority, notif_type = infer_game_and_feeling(title, body)

            clean_title = clean_text(title, max_len=60)
            items.append({
                "id": f"pretendo_forum_{tid}",
                "sender": "Pretendo Network",
                "title": clean_title,
                "body": body,
                "app": app,
                "feeling": feeling,
                "priority": priority,
                "type": notif_type,
                "url": f"https://forum.pretendo.network/t/{topic.get('slug', tid)}/{tid}",
                "timestamp": topic.get("created_at", "")
            })
    except Exception as e:
        print(f"[!] Error fetching Pretendo forum: {e}", file=sys.stderr)

    return items

def fetch_blog_feed(limit=3):
    """Fetch recent posts from Pretendo blog RSS feed."""
    items = []
    url = "https://pretendo.network/blog/feed.xml"
    try:
        req = urllib.request.Request(url, headers=HTTP_HEADERS)
        with urllib.request.urlopen(req, timeout=10) as resp:
            xml_data = resp.read()
            root = ET.fromstring(xml_data)

        channel = root.find("channel")
        if channel is not None:
            for item in channel.findall("item")[:limit]:
                title = item.findtext("title", "")
                link = item.findtext("link", "")
                desc = item.findtext("description", "")
                guid = item.findtext("guid", link)
                clean_title = clean_text(title, max_len=60)
                clean_body = clean_text(desc, max_len=450)
                app, feeling, priority, notif_type = infer_game_and_feeling(clean_title, clean_body)

                # Generate a stable deterministic ID
                safe_guid = re.sub(r"[^a-zA-Z0-9_]", "_", guid).strip("_")
                items.append({
                    "id": f"pretendo_blog_{safe_guid}",
                    "sender": "Pretendo Blog",
                    "title": clean_title,
                    "body": clean_body,
                    "app": app,
                    "feeling": feeling,
                    "priority": priority,
                    "type": notif_type,
                    "url": link,
                    "timestamp": item.findtext("pubDate", "")
                })
    except Exception as e:
        print(f"[!] Error fetching Pretendo blog RSS: {e}", file=sys.stderr)

    return items

def load_seen():
    if os.path.exists(SEEN_FILE):
        try:
            with open(SEEN_FILE, "r") as f:
                return set(json.load(f))
        except Exception:
            pass
    return set()

def save_seen(seen_set):
    os.makedirs(os.path.dirname(SEEN_FILE), exist_ok=True)
    with open(SEEN_FILE, "w") as f:
        json.dump(sorted(list(seen_set)), f, indent=2)

def inject_to_console(ip, item):
    """Inject notification straight into BOSS storage on Wii U via wiiu_news_inject.py."""
    import importlib.util
    inject_path = os.path.join(BASE_DIR, "wiiu_news_inject.py")
    spec = importlib.util.spec_from_file_location("wiiu_news_inject", inject_path)
    inj_mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inj_mod)

    # Injects for all detected profiles
    inj_mod.inject(
        ip=ip,
        pid=0, # Auto-injects for all registered profiles
        title=item["title"],
        body=item["body"],
        feeling=item["feeling"],
        app=item["app"]
    )

def push_tcp(ip, port, item):
    """Deliver via TCP to CommunityNotifications.wps on console."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(4.0)
    s.connect((ip, port))
    payload = {
        "title": item["title"],
        "body": item["body"],
        "app": item["app"],
        "feeling": item["feeling"],
        "type": item.get("type", "game")
    }
    s.sendall(json.dumps(payload).encode("utf-8") + b"\n")
    resp = s.recv(512).decode("utf-8", errors="ignore").strip()
    s.close()
    return resp

def push_sd_feed(ip, item):
    """Append to /fs/vol/external01/wiiu/notifications/feed.json on SD card."""
    feed_path = "/fs/vol/external01/wiiu/notifications/feed.json"
    ftp = ftplib.FTP(ip, timeout=5)
    ftp.login()
    buf = io.BytesIO()
    try:
        ftp.retrbinary("RETR " + feed_path, buf.write)
        feed = json.loads(buf.getvalue().decode("utf-8"))
    except Exception:
        feed = {"version": 1, "notifications": []}

    feed.setdefault("notifications", []).append({
        "id": item["id"],
        "sender": item["sender"],
        "title": item["title"],
        "message": item["body"],
        "app": item["app"],
        "feeling": item["feeling"],
        "type": item.get("type", "game"),
        "timestamp": int(time.time()),
        "priority": item["priority"],
        "duration": 7.0
    })

    out = io.BytesIO(json.dumps(feed, indent=2).encode("utf-8"))
    ftp.storbinary("STOR " + feed_path, out)
    ftp.quit()

def sync_pretendo(ip=None, port=7331, method="inject", dry_run=False):
    """Fetch latest Pretendo items, filter seen, and dispatch."""
    if not ip:
        ip = os.getenv("WIIU_IP", "192.168.1.100")
    print("[*] Fetching Pretendo Network community announcements & blog feeds...")
    forum_items = fetch_forum_announcements(limit=5)
    blog_items = fetch_blog_feed(limit=3)

    all_items = forum_items + blog_items
    print(f"[*] Retrieved {len(all_items)} total feed items.")

    seen = load_seen()
    new_items = [it for it in all_items if it["id"] not in seen]
    print(f"[*] Found {len(new_items)} new unread announcement(s).")

    for item in new_items:
        print(f"\n--- [{item['sender']}] {item['title']} ---")
        print(f"    Type: {item.get('type', 'game').upper()} | App: {item['app']} | Feeling: {item['feeling']} | Priority: {item['priority']}")
        print(f"    Body: {item['body'][:120]}...")

        if dry_run:
            print("    [Dry Run] Skipped delivery.")
            continue

        success = False
        if method == "inject":
            try:
                inject_to_console(ip, item)
                success = True
            except Exception as e:
                print(f"    [!] Direct inject failed: {e}")
        elif method == "tcp":
            try:
                resp = push_tcp(ip, port, item)
                print(f"    [TCP] Delivered: {resp}")
                success = True
            except Exception as e:
                print(f"    [!] TCP push failed: {e}")
        elif method == "feed":
            try:
                push_sd_feed(ip, item)
                print("    [SD Feed] Appended to feed.json")
                success = True
            except Exception as e:
                print(f"    [!] SD feed update failed: {e}")

        if success:
            seen.add(item["id"])
            save_seen(seen)

    return len(new_items)

def main():
    parser = argparse.ArgumentParser(description="Pretendo Network Community Scraper for Wii U")
    parser.add_argument("--ip", default=os.getenv("WIIU_IP", "192.168.1.100"), help="Wii U IP address")
    parser.add_argument("--port", type=int, default=7331, help="Wii U plugin TCP port")
    parser.add_argument("--method", choices=["inject", "tcp", "feed"], default="inject",
                        help="Delivery method: 'inject' (BOSS storage over FTP), 'tcp' (live listener), 'feed' (SD JSON)")
    parser.add_argument("--dry-run", action="store_true", help="Print announcements without delivering")
    parser.add_argument("--daemon", action="store_true", help="Run continuously in background")
    parser.add_argument("--interval", type=int, default=300, help="Daemon poll interval in seconds (default: 300)")
    parser.add_argument("--reset-seen", action="store_true", help="Clear seen history cache")

    args = parser.parse_args()

    if args.reset_seen:
        if os.path.exists(SEEN_FILE):
            os.remove(SEEN_FILE)
            print("[*] Cleared pretendo_seen.json")

    if args.daemon:
        print(f"[*] Starting Pretendo daemon (method={args.method}, interval={args.interval}s, IP={args.ip})...")
        while True:
            try:
                sync_pretendo(ip=args.ip, port=args.port, method=args.method, dry_run=args.dry_run)
            except Exception as e:
                print(f"[!] Daemon sync cycle error: {e}")
            time.sleep(args.interval)
    else:
        sync_pretendo(ip=args.ip, port=args.port, method=args.method, dry_run=args.dry_run)

if __name__ == "__main__":
    main()
