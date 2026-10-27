#!/usr/bin/env python3
"""
Wii U Desktop Notification Bridge & Pretendo Community Notifier
---------------------------------------------------------------
Sends desktop alerts and community announcements directly to the
Wii U console for display in the official Nintendo Notification Menu.
"""

import sys
import json
import socket
import ftplib
import argparse
import urllib.request

DEFAULT_WIIU_IP = "192.168.1.100"
DEFAULT_PORT = 7331
FTP_FEED_PATH = "/fs/vol/external01/wiiu/notifications/feed.json"

GAME_TITLE_IDS = {
    "splatoon": "0005000010176A00",
    "minecraft": "00050000101D7500",
    "mariokart8": "000500001010EC00",
    "starfox": "00050000101B0500",
    "fastracing": "00050000101D6000",
    "isaac": "00050000101C9B00",
}

def send_tcp(ip, port, payload):
    """Attempt direct live delivery via TCP to Wii U."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3.0)
        s.connect((ip, port))
        msg = json.dumps(payload).encode("utf-8")
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

        new_entry = {
            "id": f"desktop-{abs(hash(payload.get('body', '')))}",
            "sender": payload.get("sender", "Desktop Bridge"),
            "title": payload.get("title", "Desktop Alert"),
            "message": payload.get("body", ""),
            "timestamp": int(urllib.request.time.time()) if hasattr(urllib.request, "time") else 1793150000,
            "priority": 1,
            "duration": 7.0
        }
        feed.setdefault("notifications", []).append(new_entry)

        updated_bytes = json.dumps(feed, indent=2).encode("utf-8")
        import io
        ftp.storbinary(f"STOR {FTP_FEED_PATH}", io.BytesIO(updated_bytes))
        ftp.quit()
        return True, "Appended to SD feed.json (will sync on next launch)"
    except Exception as e:
        return False, str(e)

def fetch_pretendo_announcements():
    """Fetch recent Pretendo announcements / news."""
    url = "https://status.pretendo.network/api/v1/incidents"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "WiiU-Notifier/1.0"})
        with urllib.request.urlopen(req, timeout=5) as response:
            data = json.loads(response.read().decode("utf-8"))
            if data and "data" in data and len(data["data"]) > 0:
                latest = data["data"][0]
                return {
                    "title": latest.get("name", "Pretendo Network Update"),
                    "body": latest.get("message", "Pretendo servers are operating normally. Welcome to the open network!"),
                    "app": "000500101004D200",
                    "feeling": "HAPPY"
                }
    except Exception as e:
        pass
    # Fallback default community message
    return {
        "title": "Pretendo Splatfest",
        "body": "Inkopolis Plaza is active! Join your friends on Pretendo Network for custom online events.",
        "app": GAME_TITLE_IDS["splatoon"],
        "feeling": "HAPPY"
    }

def main():
    parser = argparse.ArgumentParser(description="Wii U Desktop & Community Notification Bridge")
    parser.add_argument("--ip", default=DEFAULT_WIIU_IP, help=f"Wii U IP address (default: {DEFAULT_WIIU_IP})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"TCP port (default: {DEFAULT_PORT})")
    parser.add_argument("--title", default="Desktop Notification", help="Notification title")
    parser.add_argument("--body", default="Hello from Linux desktop!", help="Notification body text")
    parser.add_argument("--app", default="", help="Target game (splatoon, minecraft, mariokart8, or Title ID)")
    parser.add_argument("--feeling", default="HAPPY", choices=["HAPPY", "PUZZLED", "SURPRISED", "LIKE", "FRUSTRATED", "NORMAL"], help="Mii expression")
    parser.add_argument("--pretendo", action="store_true", help="Fetch and push latest Pretendo community announcement")

    args = parser.parse_args()

    if args.pretendo:
        payload = fetch_pretendo_announcements()
        print(f"[*] Fetched Pretendo notice: '{payload['title']}'")
    else:
        title_id = GAME_TITLE_IDS.get(args.app.lower(), args.app)
        payload = {
            "title": args.title,
            "body": args.body,
            "app": title_id,
            "feeling": args.feeling
        }

    print(f"[*] Dispatching notification to Wii U ({args.ip}):")
    print(f"    Title  : {payload['title']}")
    print(f"    Body   : {payload['body']}")
    if payload.get("app"):
        print(f"    App ID : {payload['app']}")

    # 1. Try TCP live delivery
    ok, resp = send_tcp(args.ip, args.port, payload)
    if ok:
        print(f"[✓] Live delivery successful over TCP! (Response: {resp})")
        print("[*] GamePad HOME button LED should be breathing blue on your console!")
        return 0

    print(f"[-] Live TCP delivery not connected ({resp}).")
    print("[*] Falling back to asynchronous FTP drop into SD feed.json...")

    # 2. Fallback to FTP feed.json
    ok, resp = send_ftp_feed(args.ip, payload)
    if ok:
        print(f"[✓] {resp}")
        print("[*] It will appear inside Nintendo's Notification Menu when NotifyTest or the plugin next loads!")
        return 0
    else:
        print(f"[!] FTP fallback failed: {resp}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
