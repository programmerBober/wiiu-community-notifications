#!/usr/bin/env python3
"""Push a notification to the Wii U Notifications applet over FTP.

DEFAULT (safe): writes to the SD card only:
  sd:/wiiu/notifications/news/<serial %08x>.bin   zlib-compressed <Message> XML
The CommunityNotifications plugin hooks nn_boss so inf.rpx reads these straight
from SD. Zero writes to internal MLC/NAND.

--mlc-legacy (DANGEROUS, deprecated): the old direct BOSS-storage injection below.
Rewrites MLC databases (NAND wear + corruption risk on power loss). Avoid.

Legacy MLC layout (reverse-engineered from on-console BOSS storages):
  data/news/<serial %08x>        zlib-compressed UTF-8 <Message> XML (inf.rpx inflateInit2(47))
  data/news/ref_count.db         8-byte header + 512 x 0x24 entries
                                 {u32 serial, u32 0x01000000, u32 size, 16 x 0, u64 time_us_since_2000}
  <pid>/news/fad.db              8-byte header + 512 x 0x38 entries
                                 {char name[32], u32 serial, 16 x 0 (flags/read), u32 secs_since_2000}
  <pid>/news/didx.db             1 Mbit bitmap, bit (serial-1) set MSB-first
"""
import argparse, ftplib, io, os, struct, time, zlib
from xml.sax.saxutils import escape

BASE = "/storage_mlc/usr/boss/00050010/1004d200/user/common"   # legacy only
SD_NEWS = "/fs/vol/external01/wiiu/notifications/news"
EPOCH2000 = 946684800
SERIAL_BASE = 0x000F0000  # far above BOSS's global counter (~0xc4xx) to avoid clashes


def get(ftp, path):
    b = io.BytesIO(); ftp.retrbinary("RETR " + path, b.write); return bytearray(b.getvalue())


def put(ftp, path, data):
    ftp.storbinary("STOR " + path, io.BytesIO(bytes(data)))


def find_all_pids(ftp):
    lines = []
    ftp.retrlines("LIST " + BASE, lines.append)
    pids = []
    for l in lines:
        name = l.split()[-1]
        if len(name) == 8 and name.startswith("8000"):
            try:
                pids.append(int(name, 16))
            except ValueError:
                pass
    return pids if pids else [0x80000001]


def build_xml(title, body, feeling="HAPPY", app="", tid=None, minimal=False):
    t, b = escape(title), escape(body)
    tid = tid or (app.upper() if len(app) >= 16 else "0005001010040200")
    if minimal:
        return (f'<?xml version="1.0" encoding="utf-8"?>\n<Message>\n  <Subject>{t}</Subject>\n  <Body>{b}</Body>\n'
                f"  <TitleId>{tid}</TitleId>\n</Message>\n").encode()
    link = (f"  <LinkType>APP</LinkType>\n  <Links>\n    <AppLink>\n      <TitleId>{tid}</TitleId>\n"
            f"      <Parameter></Parameter>\n    </AppLink>\n  </Links>\n") if len(app) >= 16 else "  <LinkType>NONE</LinkType>\n"
    return (f'<?xml version="1.0" encoding="utf-8"?>\n<Message>\n  <Subject>{t}</Subject>\n  <Body>{b}</Body>\n'
            f"  <TitleId>{tid}</TitleId>\n  <HighPriority>true</HighPriority>\n  <ForceNotify>true</ForceNotify>\n"
            f"  <Feeling>{feeling}</Feeling>\n  <OptoutType>NEVER</OptoutType>\n{link}</Message>\n").encode()


def ensure_dir(ftp, path):
    cur = ""
    for part in path.strip("/").split("/"):
        cur += "/" + part
        try:
            ftp.mkd(cur)
        except ftplib.error_perm:
            pass


def inject_sd(ip, title, body, feeling, app, tid=None, minimal=False):
    ftp = ftplib.FTP(ip, timeout=20); ftp.login()
    ensure_dir(ftp, SD_NEWS)
    serials = []
    for name in ftp.nlst(SD_NEWS):
        name = name.rsplit("/", 1)[-1]
        if name.endswith(".bin") and len(name) == 12:
            try:
                serials.append(int(name[:8], 16))
            except ValueError:
                pass
    serial = max(serials, default=0) + 1
    xml = zlib.compress(build_xml(title, body, feeling, app, tid, minimal))
    tmp, dst = f"{SD_NEWS}/{serial:08x}.tmp", f"{SD_NEWS}/{serial:08x}.bin"
    put(ftp, tmp, xml)
    ftp.rename(tmp, dst)  # atomic: the plugin never sees a half-written payload
    ftp.quit()
    print(f"OK: saved serial 0x{serial:08x} ({len(xml)} bytes) to SD -> {dst}")


def inject(ip, pid, title, body, feeling, app, tid=None, minimal=False):
    ftp = ftplib.FTP(ip, timeout=20); ftp.login()
    targets = [pid] if pid else find_all_pids(ftp)
    rc_p = f"{BASE}/data/news/ref_count.db"
    rc = get(ftp, rc_p)

    used = [struct.unpack(">I", rc[8 + i*0x24: 12 + i*0x24])[0] for i in range(512)]
    used = [s for s in used if s]
    serial = max([SERIAL_BASE] + used) + 1
    rc_slot = next(i for i in range(512) if not struct.unpack(">I", rc[8+i*0x24:12+i*0x24])[0])

    xml = zlib.compress(build_xml(title, body, feeling, app, tid, minimal))  # inf.rpx inflate()s every entry
    now = time.time() - EPOCH2000
    put(ftp, f"{BASE}/data/news/{serial:08x}", xml)

    ref_count_val = len(targets) << 24
    o = 8 + rc_slot*0x24
    rc[o:o+0x24] = struct.pack(">III16xQ", serial, ref_count_val, len(xml), int(now * 1_000_000))
    put(ftp, rc_p, rc)

    for p in targets:
        fad_p = f"{BASE}/{p:08x}/news/fad.db"
        didx_p = f"{BASE}/{p:08x}/news/didx.db"
        fad, didx = get(ftp, fad_p), get(ftp, didx_p)
        fad_slot = next(i for i in range(512) if not any(fad[8+i*0x38:8+i*0x38+0x38]))

        fo = 8 + fad_slot*0x38
        fad[fo:fo+0x38] = struct.pack(">32sI16xI", f"cn{serial:x}.xml".encode(), serial, int(now))
        didx[(serial-1) // 8] |= 0x80 >> ((serial-1) % 8)
        put(ftp, fad_p, fad); put(ftp, didx_p, didx)
        print(f"OK: registered serial 0x{serial:08x} for profile PID 0x{p:08x}")

    ftp.quit()
    print(f"OK: successfully injected serial 0x{serial:08x} ({len(xml)} bytes) for {len(targets)} profile(s)")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default=os.getenv("WIIU_IP", "192.168.1.100"))
    ap.add_argument("--pid", type=lambda x: int(x, 0), default=0, help="Target PID (default: all profiles)")
    ap.add_argument("--title", required=True)
    ap.add_argument("--body", required=True)
    ap.add_argument("--feeling", default="HAPPY")
    ap.add_argument("--app", default="")
    ap.add_argument("--tid", default=None)
    ap.add_argument("--minimal", action="store_true")
    ap.add_argument("--mlc-legacy", action="store_true",
                    help="DANGEROUS: old direct MLC BOSS-database injection (NAND wear/corruption risk)")
    a = ap.parse_args()
    if a.mlc_legacy:
        print("WARNING: --mlc-legacy writes Wii U internal NAND (MLC) databases directly.")
        if input("Type 'I understand' to continue: ").strip() != "I understand":
            raise SystemExit("Aborted.")
        inject(a.ip, a.pid, a.title, a.body, a.feeling, a.app, a.tid, a.minimal)
    else:
        inject_sd(a.ip, a.title, a.body, a.feeling, a.app, a.tid, a.minimal)
