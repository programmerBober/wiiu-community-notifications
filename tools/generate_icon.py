import math
from PIL import Image, ImageDraw, ImageFilter

SIZE = 1024  # 4x supersampling for razor-sharp downscaling to 256 and 128
img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# 1. Background Rounded Squircle
margin = 48
radius = 180
rect = [margin, margin, SIZE - margin, SIZE - margin]

# Deep navy/slate backdrop with radial ambient lighting
bg_layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
bg_draw = ImageDraw.Draw(bg_layer)

# Fill base rounded rect
bg_draw.rounded_rectangle(rect, radius=radius, fill=(18, 22, 28, 255))

# Subtle gradient / spotlight from top
for r in range(400, 50, -10):
    alpha = int(25 * (1.0 - r / 400.0))
    bg_draw.ellipse([SIZE//2 - r, margin - 50, SIZE//2 + r, margin - 50 + 2*r], fill=(0, 200, 230, alpha))

# Mask bg to squircle
mask = Image.new("L", (SIZE, SIZE), 0)
ImageDraw.Draw(mask).rounded_rectangle(rect, radius=radius, fill=255)
bg_layer.putalpha(mask)
img = Image.alpha_composite(img, bg_layer)

# 2. Glowing GamePad LED Ring (bottom-right accent)
led_layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
led_draw = ImageDraw.Draw(led_layer)
cx, cy = 720, 720

# Ambient glow rings
for gr in [160, 120, 80]:
    led_draw.ellipse([cx - gr, cy - gr, cx + gr, cy + gr], fill=(0, 210, 255, 30))

# HOME button outer ring
led_draw.ellipse([cx - 95, cy - 95, cx + 95, cy + 95], outline=(0, 240, 255, 230), width=18)
led_draw.ellipse([cx - 80, cy - 80, cx + 80, cy + 80], outline=(255, 255, 255, 120), width=4)
# HOME house icon inside LED
led_draw.polygon([(cx, cy - 42), (cx - 38, cy - 6), (cx + 38, cy - 6)], fill=(0, 240, 255, 220))
led_draw.rectangle([cx - 28, cy - 6, cx + 28, cy + 34], fill=(0, 240, 255, 220))
led_draw.rectangle([cx - 10, cy + 12, cx + 10, cy + 34], fill=(18, 22, 28, 255)) # door

led_layer = led_layer.filter(ImageFilter.GaussianBlur(radius=2))
img = Image.alpha_composite(img, led_layer)

# 3. Wii U SpotPass Cyan/Teal Speech Bubble
bubble_layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
b_draw = ImageDraw.Draw(bubble_layer)

bx1, by1 = 160, 170
bx2, by2 = 720, 610
bradius = 130

# Shadow behind bubble
shadow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
s_draw = ImageDraw.Draw(shadow)
s_draw.rounded_rectangle([bx1 + 10, by1 + 25, bx2 + 10, by2 + 25], radius=bradius, fill=(0, 0, 0, 110))
# Tail shadow
s_draw.polygon([(bx1 + 80, by2 - 10), (bx1 + 30, by2 + 120), (bx1 + 170, by2)], fill=(0, 0, 0, 110))
shadow = shadow.filter(ImageFilter.GaussianBlur(radius=16))
img = Image.alpha_composite(img, shadow)

# Main bubble shape
# We create a mask for a gradient
bubble_mask = Image.new("L", (SIZE, SIZE), 0)
bm_draw = ImageDraw.Draw(bubble_mask)
bm_draw.rounded_rectangle([bx1, by1, bx2, by2], radius=bradius, fill=255)
# Speech bubble tail pointing bottom-left
bm_draw.polygon([(bx1 + 70, by2 - 30), (bx1 + 20, by2 + 100), (bx1 + 160, by2 - 10)], fill=255)
# Smooth tail curve
bm_draw.ellipse([bx1 + 10, by2 + 80, bx1 + 35, by2 + 105], fill=255)

# Gradient fill: SpotPass Teal -> Cyan
grad = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
g_draw = ImageDraw.Draw(grad)
for y in range(by1, by2 + 120):
    factor = (y - by1) / float(by2 + 120 - by1)
    # 0, 163, 137 (Teal) to 0, 220, 255 (Vibrant Cyan)
    r = int(0 * (1 - factor) + 0 * factor)
    g = int(165 * (1 - factor) + 215 * factor)
    b = int(145 * (1 - factor) + 255 * factor)
    g_draw.line([(0, y), (SIZE, y)], fill=(r, g, b, 255), width=1)

grad.putalpha(bubble_mask)
img = Image.alpha_composite(img, grad)

# Bubble glossy highlight on top
hl_mask = Image.new("L", (SIZE, SIZE), 0)
ImageDraw.Draw(hl_mask).rounded_rectangle([bx1 + 6, by1 + 6, bx2 - 6, by1 + 180], radius=bradius-6, fill=255)
hl = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 45))
hl.putalpha(hl_mask)
img = Image.alpha_composite(img, hl)

# Bubble border outline (crisp subtle cyan border)
outline_layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
ol_draw = ImageDraw.Draw(outline_layer)
ol_draw.rounded_rectangle([bx1, by1, bx2, by2], radius=bradius, outline=(255, 255, 255, 140), width=8)
img = Image.alpha_composite(img, outline_layer)

# 4. Exclamation Point "!" (Pure crisp white with subtle 3D drop)
ex_layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
ex_draw = ImageDraw.Draw(ex_layer)
mx = (bx1 + bx2) // 2 - 15  # 425

# Upper bar of ! (pill shape tapered)
top_y = by1 + 80
bot_y = by1 + 250
w_top = 28
w_bot = 22
ex_draw.polygon([
    (mx - w_top, top_y + 20), (mx + w_top, top_y + 20),
    (mx + w_bot, bot_y), (mx - w_bot, bot_y)
], fill=(255, 255, 255, 255))
ex_draw.ellipse([mx - w_top, top_y, mx + w_top, top_y + 40], fill=(255, 255, 255, 255))
ex_draw.ellipse([mx - w_bot, bot_y - 15, mx + w_bot, bot_y + 15], fill=(255, 255, 255, 255))

# Dot of !
dot_y = by1 + 310
dot_r = 25
ex_draw.ellipse([mx - dot_r, dot_y - dot_r, mx + dot_r, dot_y + dot_r], fill=(255, 255, 255, 255))

img = Image.alpha_composite(img, ex_layer)

# 5. SpotPass Wi-Fi Antenna Wave Arcs (top right of bubble)
wave_layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
w_draw = ImageDraw.Draw(wave_layer)
wx, wy = 710, 240
for rad, width, alpha in [(70, 10, 160), (115, 12, 210), (160, 14, 255)]:
    box = [wx - rad, wy - rad, wx + rad, wy + rad]
    w_draw.arc(box, start=280, end=350, fill=(0, 240, 255, alpha), width=width)

# A glowing dot emitter
w_draw.ellipse([wx - 12, wy - 12, wx + 12, wy + 12], fill=(255, 255, 255, 255))
w_draw.ellipse([wx - 20, wy - 20, wx + 20, wy + 20], outline=(0, 240, 255, 180), width=4)

img = Image.alpha_composite(img, wave_layer)

# 6. Outer Bezel / Border for the entire icon tile
bezel = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
b_draw = ImageDraw.Draw(bezel)
b_draw.rounded_rectangle(rect, radius=radius, outline=(255, 255, 255, 45), width=6)
img = Image.alpha_composite(img, bezel)

# 7. Export at 256x256 (High-Res HB App Store & WUHB standard) and 128x128
icon_256 = img.resize((256, 256), Image.Resampling.LANCZOS)
icon_128 = img.resize((128, 128), Image.Resampling.LANCZOS)
icon_512 = img.resize((512, 512), Image.Resampling.LANCZOS)

icon_256.save("/home/user/Projecten/wiiu-community-notifications/icon.png")
icon_128.save("/home/user/Projecten/wiiu-community-notifications/icon_128.png")
icon_512.save("/home/user/Projecten/wiiu-community-notifications/icon_512.png")
print("Icons generated successfully!")
