#!/usr/bin/env python3
"""
generate_assets.py - High-fidelity Wii U & Aroma assets
Pixel-perfect alignment, clean vector curves, zero clipping.
"""

import os
import math
from PIL import Image, ImageDraw, ImageFilter, ImageFont

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(PROJECT_DIR, "assets")
os.makedirs(ASSETS_DIR, exist_ok=True)

def get_font(size, bold=False):
    paths = [
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    ]
    for p in paths:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                pass
    return ImageFont.load_default()

def draw_spotpass_emblem(target_size, cx, cy, scale=1.0):
    """
    Renders an authentic SpotPass speech bubble with exclamation mark
    and Wi-Fi broadcast waves onto an RGBA Image.
    """
    layer = Image.new("RGBA", target_size, (0, 0, 0, 0))

    # Proportions: 1.42:1 aspect ratio
    bw = int(105 * scale)
    bh = int(74 * scale)
    bx1 = cx - bw
    by1 = cy - bh
    bx2 = cx + bw
    by2 = cy + bh
    rad = int(36 * scale)

    # 1. Soft Drop Shadow
    shadow = Image.new("RGBA", target_size, (0, 0, 0, 0))
    s_draw = ImageDraw.Draw(shadow)
    soff = int(8 * scale)
    s_draw.rounded_rectangle([bx1, by1 + soff, bx2, by2 + soff], radius=rad, fill=(0, 0, 0, 115))
    s_draw.polygon([
        (bx1 + int(24 * scale), by2 - int(4 * scale)),
        (bx1 - int(6 * scale), by2 + int(18 * scale)),
        (bx1 + int(56 * scale), by2 + soff)
    ], fill=(0, 0, 0, 115))
    shadow = shadow.filter(ImageFilter.GaussianBlur(radius=int(10 * scale)))
    layer = Image.alpha_composite(layer, shadow)

    # 2. Bubble Body Mask with Smooth Triangle Tail
    b_mask = Image.new("L", target_size, 0)
    bm_draw = ImageDraw.Draw(b_mask)
    bm_draw.rounded_rectangle([bx1, by1, bx2, by2], radius=rad, fill=255)
    # Clean, organic speech bubble tail (smoothly merges with the rounded corner)
    bm_draw.polygon([
        (bx1 + int(22 * scale), by2 - int(10 * scale)),
        (bx1 - int(6 * scale), by2 + int(18 * scale)),
        (bx1 + int(54 * scale), by2)
    ], fill=255)

    # 3. SpotPass Gradient: Electric Cyan (#00E5FF) -> Cerulean (#0088EA) -> Royal Blue (#0066CC)
    grad = Image.new("RGBA", target_size, (0, 0, 0, 0))
    g_draw = ImageDraw.Draw(grad)
    total_h = by2 + int(20 * scale) - by1
    for y in range(by1, by2 + int(20 * scale)):
        t = (y - by1) / float(total_h)
        if t < 0.5:
            st = t / 0.5
            r = int(0 * (1 - st) + 0 * st)
            g = int(225 * (1 - st) + 140 * st)
            b = int(255 * (1 - st) + 235 * st)
        else:
            st = (t - 0.5) / 0.5
            r = int(0 * (1 - st) + 0 * st)
            g = int(140 * (1 - st) + 95 * st)
            b = int(235 * (1 - st) + 210 * st)
        g_draw.line([(0, y), (target_size[0], y)], fill=(r, g, b, 255), width=1)
    grad.putalpha(b_mask)
    layer = Image.alpha_composite(layer, grad)

    # 4. Subtle Inner Rim Highlight
    rim = Image.new("RGBA", target_size, (0, 0, 0, 0))
    r_draw = ImageDraw.Draw(rim)
    r_draw.rounded_rectangle([bx1, by1, bx2, by2], radius=rad, outline=(255, 255, 255, 95), width=max(1, int(2 * scale)))
    layer = Image.alpha_composite(layer, rim)

    # 5. Crisp White Exclamation Mark "!"
    ex = Image.new("RGBA", target_size, (0, 0, 0, 0))
    ex_draw = ImageDraw.Draw(ex)
    ex_top = cy - int(36 * scale)
    ex_bot = cy + int(10 * scale)
    wt = int(9 * scale)
    wb = int(7 * scale)

    # Tapered pill shape
    ex_draw.polygon([
        (cx - wt, ex_top + int(6 * scale)),
        (cx + wt, ex_top + int(6 * scale)),
        (cx + wb, ex_bot),
        (cx - wb, ex_bot)
    ], fill=(255, 255, 255, 255))
    ex_draw.ellipse([cx - wt, ex_top, cx + wt, ex_top + int(14 * scale)], fill=(255, 255, 255, 255))
    ex_draw.ellipse([cx - wb, ex_bot - int(4 * scale), cx + wb, ex_bot + int(4 * scale)], fill=(255, 255, 255, 255))

    # Dot
    dot_y = cy + int(30 * scale)
    dot_r = int(7 * scale)
    ex_draw.ellipse([cx - dot_r, dot_y - dot_r, cx + dot_r, dot_y + dot_r], fill=(255, 255, 255, 255))
    layer = Image.alpha_composite(layer, ex)

    # 6. SpotPass Wi-Fi Broadcast Arcs (Smooth radiating waves)
    waves = Image.new("RGBA", target_size, (0, 0, 0, 0))
    w_draw = ImageDraw.Draw(waves)
    wx = bx2 - int(10 * scale)
    wy = by1 + int(10 * scale)

    for rad_offset, w_width, w_alpha in [
        (int(14 * scale), max(1, int(2.5 * scale)), 160),
        (int(28 * scale), max(2, int(3.5 * scale)), 210),
        (int(42 * scale), max(2, int(4.5 * scale)), 255)
    ]:
        box = [wx - rad_offset, wy - rad_offset, wx + rad_offset, wy + rad_offset]
        w_draw.arc(box, start=280, end=350, fill=(0, 235, 255, w_alpha), width=w_width)
    layer = Image.alpha_composite(layer, waves)

    return layer

# -----------------------------------------------------------------------------
# 1. ICON (1024x1024 -> 256x256 & 128x128)
# -----------------------------------------------------------------------------
def generate_icon():
    SIZE = 1024
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))

    margin = 52
    radius = 210
    rect = [margin, margin, SIZE - margin, SIZE - margin]

    # Background Squircle: Soft vertical dark gradient
    bg = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    bg_draw = ImageDraw.Draw(bg)
    for y in range(margin, SIZE - margin):
        t = (y - margin) / float(SIZE - 2 * margin)
        r = int(13 * (1 - t) + 20 * t)
        g = int(17 * (1 - t) + 26 * t)
        b = int(27 * (1 - t) + 38 * t)
        bg_draw.line([(margin, y), (SIZE - margin, y)], fill=(r, g, b, 255))

    # Mask base to squircle
    s_mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(s_mask).rounded_rectangle(rect, radius=radius, fill=255)
    bg.putalpha(s_mask)
    img = Image.alpha_composite(img, bg)

    # Ambient radial cyan glow behind emblem
    glow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    g_draw = ImageDraw.Draw(glow)
    g_draw.ellipse([230, 200, 794, 764], fill=(0, 205, 245, 30))
    glow = glow.filter(ImageFilter.GaussianBlur(radius=70))
    img = Image.alpha_composite(img, glow)

    # SpotPass Emblem
    emblem = draw_spotpass_emblem((SIZE, SIZE), cx=512, cy=480, scale=3.1)
    img = Image.alpha_composite(img, emblem)

    # GamePad HOME LED Ring (bottom-right corner)
    home = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    h_draw = ImageDraw.Draw(home)
    hx, hy = 760, 760

    # Ambient cyan aura
    h_draw.ellipse([hx - 80, hy - 80, hx + 80, hy + 80], fill=(0, 215, 255, 35))
    home = home.filter(ImageFilter.GaussianBlur(radius=15))
    h_draw = ImageDraw.Draw(home)

    # Crisp neon rings
    h_draw.ellipse([hx - 48, hy - 48, hx + 48, hy + 48], outline=(0, 235, 255, 245), width=9)
    h_draw.ellipse([hx - 40, hy - 40, hx + 40, hy + 40], outline=(255, 255, 255, 150), width=2)

    # HOME house icon
    h_draw.polygon([(hx, hy - 20), (hx - 18, hy - 2), (hx + 18, hy - 2)], fill=(0, 235, 255, 245))
    h_draw.rectangle([hx - 14, hy - 2, hx + 14, hy + 17], fill=(0, 235, 255, 245))
    h_draw.rectangle([hx - 5, hy + 5, hx + 5, hy + 17], fill=(18, 24, 34, 255))
    img = Image.alpha_composite(img, home)

    # Squircle border highlight
    bezel = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    b_draw = ImageDraw.Draw(bezel)
    b_draw.rounded_rectangle(rect, radius=radius, outline=(255, 255, 255, 45), width=5)
    img = Image.alpha_composite(img, bezel)

    # Final mask
    final_img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    final_img.paste(img, (0, 0), mask=s_mask)

    # Export
    icon_128 = final_img.resize((128, 128), Image.Resampling.LANCZOS)
    icon_256 = final_img.resize((256, 256), Image.Resampling.LANCZOS)
    icon_128.save(os.path.join(ASSETS_DIR, "icon.png"))
    icon_256.save(os.path.join(ASSETS_DIR, "icon_256.png"))
    icon_128.save(os.path.join(PROJECT_DIR, "test_app", "icon.png"))
    print("  [✓] Icon generated (128x128 & 256x256)")

# -----------------------------------------------------------------------------
# 2. TV BOOT BANNER (1280x720)
# -----------------------------------------------------------------------------
def generate_tv_banner():
    W, H = 1280, 720
    img = Image.new("RGBA", (W, H), (12, 16, 24, 255))
    draw = ImageDraw.Draw(img)

    for y in range(H):
        t = y / float(H)
        r = int(11 * (1 - t) + 16 * t)
        g = int(15 * (1 - t) + 21 * t)
        b = int(23 * (1 - t) + 33 * t)
        draw.line([(0, y), (W, y)], fill=(r, g, b, 255))

    cx, cy = W // 2, 215
    spot = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    s_draw = ImageDraw.Draw(spot)
    s_draw.ellipse([cx - 280, cy - 280, cx + 280, cy + 280], fill=(0, 195, 240, 22))
    spot = spot.filter(ImageFilter.GaussianBlur(radius=60))
    s_draw = ImageDraw.Draw(spot)
    for rad, alpha in [(160, 24), (250, 16), (340, 10)]:
        s_draw.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], outline=(0, 220, 255, alpha), width=2)
    img = Image.alpha_composite(img, spot)

    # Centered SpotPass Emblem
    emblem = draw_spotpass_emblem((W, H), cx=cx, cy=cy, scale=1.3)
    img = Image.alpha_composite(img, emblem)

    # Typography
    txt = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    t_draw = ImageDraw.Draw(txt)

    font_title = get_font(36, bold=True)
    font_sub = get_font(19, bold=True)
    font_tags = get_font(15, bold=False)
    font_foot = get_font(13, bold=False)

    title = "Wii U Community Notifications"
    tw = font_title.getbbox(title)[2] - font_title.getbbox(title)[0]
    t_draw.text(((W - tw) // 2, 375), title, font=font_title, fill=(255, 255, 255, 255))

    sub = "SpotPass Revived"
    sw = font_sub.getbbox(sub)[2] - font_sub.getbbox(sub)[0]
    t_draw.text(((W - sw) // 2, 432), sub, font=font_sub, fill=(0, 225, 255, 255))

    tags = "Aroma Environment   •   Pretendo Network   •   Hardware LED Alerts"
    gw = font_tags.getbbox(tags)[2] - font_tags.getbbox(tags)[0]
    t_draw.text(((W - gw) // 2, 480), tags, font=font_tags, fill=(160, 180, 205, 230))

    foot = "Wii U™ Homebrew"
    fw = font_foot.getbbox(foot)[2] - font_foot.getbbox(foot)[0]
    t_draw.text(((W - fw) // 2, 650), foot, font=font_foot, fill=(110, 130, 150, 200))

    img = Image.alpha_composite(img, txt)
    img.save(os.path.join(ASSETS_DIR, "tv_banner.png"))
    print("  [✓] TV banner generated (1280x720)")

# -----------------------------------------------------------------------------
# 3. GAMEPAD DRC BOOT BANNER (854x480)
# -----------------------------------------------------------------------------
def generate_drc_banner():
    W, H = 854, 480
    img = Image.new("RGBA", (W, H), (12, 16, 24, 255))
    draw = ImageDraw.Draw(img)

    for y in range(H):
        t = y / float(H)
        r = int(11 * (1 - t) + 16 * t)
        g = int(15 * (1 - t) + 21 * t)
        b = int(23 * (1 - t) + 33 * t)
        draw.line([(0, y), (W, y)], fill=(r, g, b, 255))

    cx, cy = W // 2, 145
    spot = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    s_draw = ImageDraw.Draw(spot)
    s_draw.ellipse([cx - 180, cy - 180, cx + 180, cy + 180], fill=(0, 195, 240, 20))
    spot = spot.filter(ImageFilter.GaussianBlur(radius=40))
    s_draw = ImageDraw.Draw(spot)
    for rad, alpha in [(110, 22), (170, 15)]:
        s_draw.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], outline=(0, 220, 255, alpha), width=2)
    img = Image.alpha_composite(img, spot)

    # Emblem
    emblem = draw_spotpass_emblem((W, H), cx=cx, cy=cy, scale=0.88)
    img = Image.alpha_composite(img, emblem)

    # Typography
    txt = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    t_draw = ImageDraw.Draw(txt)

    font_title = get_font(25, bold=True)
    font_sub = get_font(15, bold=True)
    font_tags = get_font(13, bold=False)
    font_foot = get_font(12, bold=False)

    title = "Wii U Community Notifications"
    tw = font_title.getbbox(title)[2] - font_title.getbbox(title)[0]
    t_draw.text(((W - tw) // 2, 250), title, font=font_title, fill=(255, 255, 255, 255))

    sub = "SpotPass Revived"
    sw = font_sub.getbbox(sub)[2] - font_sub.getbbox(sub)[0]
    t_draw.text(((W - sw) // 2, 292), sub, font=font_sub, fill=(0, 225, 255, 255))

    tags = "Aroma Environment   •   Pretendo Network"
    gw = font_tags.getbbox(tags)[2] - font_tags.getbbox(tags)[0]
    t_draw.text(((W - gw) // 2, 328), tags, font=font_tags, fill=(160, 180, 205, 230))

    foot = "Press [+] or HOME to return to Wii U Menu"
    fw = font_foot.getbbox(foot)[2] - font_foot.getbbox(foot)[0]
    t_draw.text(((W - fw) // 2, 430), foot, font=font_foot, fill=(110, 130, 150, 200))

    img = Image.alpha_composite(img, txt)
    img.save(os.path.join(ASSETS_DIR, "drc_banner.png"))
    print("  [✓] GamePad DRC banner generated (854x480)")

if __name__ == "__main__":
    generate_icon()
    generate_tv_banner()
    generate_drc_banner()
    print("[✓] All assets generated successfully!")
