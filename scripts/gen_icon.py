# -*- coding: utf-8 -*-
"""轻拼输入法图标生成: 圆角方块 + 蓝绿渐变 + 白色"拼"字, 输出多尺寸 .ico"""
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/chinese/NotoSansSC-Bold.ttf"
OUT = "/home/z/my-project/download/QingPinIME/assets/qingpin.ico"
S = 1024  # 大画布, 最后缩到各尺寸

# 垂直渐变: 蓝 #2E7CF6 -> 青绿 #22C1DC
grad = Image.new("RGB", (1, S))
for y in range(S):
    t = y / (S - 1)
    c0, c1 = (46, 124, 246), (34, 193, 220)
    grad.putpixel((0, y), tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(3)))
grad = grad.resize((S, S))

# 圆角蒙版
mask = Image.new("L", (S, S), 0)
d = ImageDraw.Draw(mask)
d.rounded_rectangle([8, 8, S - 8, S - 8], radius=int(S * 0.22), fill=255)

img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
img.paste(grad, (0, 0), mask)

# 白色"拼"字
font = ImageFont.truetype(FONT, int(S * 0.62))
d = ImageDraw.Draw(img)
bb = d.textbbox((0, 0), "拼", font=font)
w, h = bb[2] - bb[0], bb[3] - bb[1]
d.text(((S - w) / 2 - bb[0], (S - h) / 2 - bb[1]), "拼",
       font=font, fill=(255, 255, 255, 255))

img.save(OUT, sizes=[(16, 16), (24, 24), (32, 32), (48, 48),
                     (64, 64), (128, 128), (256, 256)])
img.resize((256, 256), Image.LANCZOS).save(OUT.replace(".ico", "_256.png"))
print("icon ->", OUT)
