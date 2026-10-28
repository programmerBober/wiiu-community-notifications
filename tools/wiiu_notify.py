#!/usr/bin/env python3
"""
Wii U Desktop Notification Bridge & Pretendo Community Notifier
---------------------------------------------------------------
Sends desktop alerts, game event bulletins, and community notices
directly to the Wii U console for display in the official Nintendo
Notification Menu and on-screen toast overlay.
"""

import sys
import os
import json
import socket
import ftplib
import argparse
import urllib.request

DEFAULT_WIIU_IP = os.getenv("WIIU_IP", "192.168.1.100")
DEFAULT_PORT = 7331
FTP_FEED_PATH = "/fs/vol/external01/wiiu/notifications/feed.json"

# Verified Wii U Title IDs (Installed on console)
GAME_TITLE_IDS = {
    "splatoon": "0005000010176A00",
    "minecraft": "00050000101D7500",
    "mariokart8": "0005000010149100",
    "mario kart": "0005000010149100",
    "smash": "00050000101C6300",
    "smm": "00050000101B4300",
    "mario maker": "00050000101B4300",
    "zelda": "0005000010142700",
    "wind waker": "0005000010142700",
    "nsmbu": "0005000010105700",
    "starfox": "00050000101B0500",
    "fastracing": "00050000101D6000",
    "isaac": "00050000101C9B00",
    "juxtaposition": "0005001010040200",
    "miiverse": "0005001010040200",
    "notification": "000500101004D200"
}

PRESETS = {
    "splatfest": {
        "title": "Splatoon - Inkopolis News",
        "body": "Splatfest announcement: Team Cats vs Team Dogs is live! Head to Inkopolis Plaza to claim your tee!",
        "app": "splatoon",
        "feeling": "HAPPY",
        "type": "game"
    },
    "mariokart": {
        "title": "150cc Mushroom Cup Tournament",
        "body": "Pretendo Weekly Grand Prix is open! Hit the track with 150cc karts and set your best time trial records!",
        "app": "mariokart8",
        "feeling": "HAPPY",
        "type": "game"
    },
    "smash": {
        "title": "For Glory - 1-on-1 Tournament",
        "body": "Challengers approaching! Online For Glory bracket has commenced on Final Destination. Settle it in Smash!",
        "app": "smash",
        "feeling": "SURPRISED",
        "type": "game"
    },
    "smm": {
        "title": "Trending Course: Bowser's Keep",
        "body": "A new Expert course by @BuilderMario is trending in the Course World with 450+ clears! Jump in and try it!",
        "app": "smm",
        "feeling": "LIKE",
        "type": "achievement"
    },
    "zelda": {
        "title": "Letter from the Great Sea",
        "body": "You washed a Tingle Bottle ashore containing a sea photograph and Miiverse message from a fellow voyager!",
        "app": "zelda",
        "feeling": "LIKE",
        "type": "community"
    },
    "nsmbu": {
        "title": "Coin Battle Challenge",
        "body": "Boost Mode challenges and 4-player Coin Battle records are open across Acorn Plains! Grab your GamePad!",
        "app": "nsmbu",
        "feeling": "HAPPY",
        "type": "game"
    },
    "minecraft": {
        "title": "Battle Mini Game - Arena Open!",
        "body": "Join your friends on Pretendo Network in the Medieval arena! Battle and Glide lobbies are open.",
        "app": "minecraft",
        "feeling": "HAPPY",
        "type": "friend"
    },
    "fastracing": {
        "title": "Subsonic League Cup Open",
        "body": "Switch your phase colors, hit supersonic boost pads, and set a new lap record on the tracks!",
        "app": "fastracing",
        "feeling": "INSPIRED",
        "type": "achievement"
    },
    "starfox": {
        "title": "Corneria Defense Alert",
        "body": "General Pepper to Star Fox: Enemy forces detected near Sector Beta! Scramble your Arwings immediately!",
        "app": "starfox",
        "feeling": "SURPRISED",
        "type": "game"
    },
    "isaac": {
        "title": "Daily Seeded Run Available",
        "body": "Today's seeded challenge is live! Descend into the Basement and fight for the top of the leaderboards!",
        "app": "isaac",
        "feeling": "SURPRISED",
        "type": "game"
    },
    "maintenance": {
        "title": "Pretendo Network Maintenance",
        "body": "Scheduled backend server maintenance is currently underway. Online matchmaking will resume in 30 minutes.",
        "app": "notification",
        "feeling": "PUZZLED",
        "type": "maintenance"
    },
    "friend_invite": {
        "title": "Friend Lobby Invitation",
        "body": "Your friend @Link has opened an online multiplayer lobby! Tap here or press HOME to jump in.",
        "app": "mariokart8",
        "feeling": "HAPPY",
        "type": "friend"
    },
    "achievement": {
        "title": "Course World Record Broken!",
        "body": "Congratulations! Your clear time of 0:34.120 took the world record on 'Galactic Starway'!",
        "app": "smm",
        "feeling": "LIKE",
        "type": "achievement"
    },
    "system_update": {
        "title": "Aroma Environment Update",
        "body": "CommunityNotifications v0.1.0 loaded successfully! GamePad LED input dismissal and toast styles active.",
        "app": "notification",
        "feeling": "HAPPY",
        "type": "system"
    }
}

def send_tcp(ip, port, payload):
    """Attempt direct live delivery via TCP to Wii U."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3.5)
        s.connect((ip, port))
        msg = json.dumps(payload).encode("utf-8") + b"\n"
        s.sendall(msg)
        resp = s.recv(1024)
        s.close()
        return True, resp.decode("utf-8", errors="ignore").strip()
    except Exception as e:
        return False, str(e)

def send_ftp_feed(ip, payload):
    """Fallback: append notification to feed.json on Wii U SD card via FTP."""
    try:
        ftp = ftplib.FTP(ip, timeout=5)
        ftp.login()
        feed_data = bytearray()
        try:
            ftp.retrbinary(f"RETR {FTP_FEED_PATH}", feed_data.extend)
            feed = json.loads(feed_data.decode("utf-8"))
        except Exception:
            feed = {"version": 1, "notifications": []}

        import time
        ts = int(time.time())
        new_entry = {
            "id": f"desktop-{ts}",
            "sender": payload.get("sender", "Desktop Bridge"),
            "title": payload.get("title", "Desktop Alert"),
            "message": payload.get("body", ""),
            "app": payload.get("app", ""),
            "feeling": payload.get("feeling", "HAPPY"),
            "type": payload.get("type", "game"),
            "timestamp": ts,
            "priority": 1 if payload.get("type") != "maintenance" else 2,
            "duration": 7.0
        }
        feed.setdefault("notifications", []).append(new_entry)

        updated_bytes = json.dumps(feed, indent=2).encode("utf-8")
        import io
        ftp.storbinary(f"STOR {FTP_FEED_PATH}", io.BytesIO(updated_bytes))
        ftp.quit()
        return True, "Appended to SD feed.json (will sync on next title launch)"
    except Exception as e:
        return False, str(e)

def fetch_pretendo_announcements():
    """Fetch live Pretendo announcements via pretendo_scraper."""
    try:
        from pretendo_scraper import fetch_forum_announcements, fetch_blog_feed
        items = fetch_forum_announcements(limit=3) + fetch_blog_feed(limit=2)
        if items:
            latest = items[0]
            return {
                "title": latest["title"],
                "body": latest["body"],
                "app": latest["app"],
                "feeling": latest["feeling"],
                "type": "community" if "blog" in latest["id"] else "maintenance"
            }
    except Exception as e:
        print(f"[!] Scraper import warning: {e}", file=sys.stderr)

    return {
        "title": "Pretendo Network Active",
        "body": "Connected to Pretendo Network! Juxtaposition, game matchmaking, and friends services are online.",
        "app": "000500101004D200",
        "feeling": "HAPPY",
        "type": "community"
    }

def main():
    parser = argparse.ArgumentParser(description="Wii U Desktop & Community Notification Bridge")
    parser.add_argument("--ip", default=DEFAULT_WIIU_IP, help=f"Wii U IP address (default: {DEFAULT_WIIU_IP})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"TCP port (default: {DEFAULT_PORT})")
    parser.add_argument("--cmd", choices=["led_off", "led_on", "clear"], help="Execute fast-path control command")
    parser.add_argument("--preset", choices=list(PRESETS.keys()), help="Dispatch a rich preset community notification")
    parser.add_argument("--type", choices=["game", "maintenance", "community", "friend", "achievement", "system"],
                        default="game", help="Notification category type")
    parser.add_argument("--title", default="Desktop Notification", help="Notification title")
    parser.add_argument("--body", default="Hello from Linux desktop!", help="Notification body text")
    parser.add_argument("--app", default="", help="Target game (splatoon, minecraft, mariokart8, smm, zelda, etc.)")
    parser.add_argument("--feeling", default="HAPPY", choices=["HAPPY", "PUZZLED", "SURPRISED", "LIKE", "FRUSTRATED", "NORMAL", "INSPIRED"], help="Mii expression")
    parser.add_argument("--pretendo", action="store_true", help="Fetch and push latest Pretendo community announcement")

    args = parser.parse_args()

    # Fast-path commands
    if args.cmd:
        print(f"[*] Sending '{args.cmd}' command to Wii U ({args.ip}:{args.port})...")
        ok, resp = send_tcp(args.ip, args.port, {"cmd": args.cmd})
        if ok:
            print(f"[✓] Executed: {resp}")
            return 0
        else:
            print(f"[!] Failed to execute '{args.cmd}': {resp}")
            return 1

    if args.preset:
        p = PRESETS[args.preset]
        title_id = GAME_TITLE_IDS.get(p["app"].lower(), p["app"])
        payload = {
            "title": p["title"],
            "body": p["body"],
            "app": title_id,
            "feeling": p["feeling"],
            "type": p["type"]
        }
    elif args.pretendo:
        payload = fetch_pretendo_announcements()
        print(f"[*] Fetched live Pretendo notice: '{payload['title']}'")
    else:
        title_id = GAME_TITLE_IDS.get(args.app.lower(), args.app)
        payload = {
            "title": args.title,
            "body": args.body,
            "app": title_id,
            "feeling": args.feeling,
            "type": args.type
        }

    print(f"[*] Dispatching notification to Wii U ({args.ip}):")
    print(f"    Type   : {payload.get('type', 'game').upper()}")
    print(f"    Title  : {payload['title']}")
    print(f"    Body   : {payload['body']}")
    print(f"    Feeling: {payload.get('feeling', 'HAPPY')}")
    if payload.get("app"):
        print(f"    App ID : {payload['app']}")

    # 1. Try TCP live delivery
    ok, resp = send_tcp(args.ip, args.port, payload)
    if ok:
        print(f"[✓] Live delivery successful over TCP! (Response: {resp})")
        print("[*] GamePad HOME button LED alerted with automatic timeout/input dismissal!")
        return 0

    print(f"[-] Live TCP delivery not connected ({resp}).")
    print("[*] Falling back to asynchronous FTP drop into SD feed.json...")

    # 2. Fallback to FTP feed.json
    ok, resp = send_ftp_feed(args.ip, payload)
    if ok:
        print(f"[✓] {resp}")
        return 0
    else:
        print(f"[!] FTP fallback failed: {resp}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
