"""Render four alternative Simpilot icon concepts and a comparison board."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets" / "icon-concepts"
SCALE = 4


def p(value):
    return round(value * SCALE)


def points(values):
    return [(p(x), p(y)) for x, y in values]


def rounded_line(draw, start, end, width, fill):
    start = (p(start[0]), p(start[1]))
    end = (p(end[0]), p(end[1]))
    width = p(width)
    radius = width // 2
    draw.line((start, end), fill=fill, width=width)
    for x, y in (start, end):
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=fill)


def star4(draw, center, outer_x, outer_y, inner, fill):
    cx, cy = center
    shape = (
        (cx, cy - outer_y),
        (cx + inner, cy - inner),
        (cx + outer_x, cy),
        (cx + inner, cy + inner),
        (cx, cy + outer_y),
        (cx - inner, cy + inner),
        (cx - outer_x, cy),
        (cx - inner, cy - inner),
    )
    draw.polygon(points(shape), fill=fill)


def icon_canvas(background):
    image = Image.new("RGBA", (512 * SCALE, 512 * SCALE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((p(32), p(32), p(480), p(480)), radius=p(112), fill=background)
    return image, draw


def concept_a():
    image, draw = icon_canvas("#D9544D")
    rounded_line(draw, (132, 378), (317, 193), 42, "#F7F2E8")
    rounded_line(draw, (132, 378), (205, 305), 58, "#17252C")
    star4(draw, (358, 152), 88, 88, 25, "#FFD35A")
    star4(draw, (240, 96), 24, 24, 7, "#F7F2E8")
    star4(draw, (423, 260), 20, 20, 6, "#42D7B3")
    return image


def concept_b():
    background = "#124852"
    image, _ = icon_canvas(background)
    key = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(key)
    cream = "#F7F2E8"
    draw.ellipse((p(92), p(209), p(206), p(323)), fill=cream)
    draw.ellipse((p(128), p(245), p(170), p(287)), fill=background)
    rounded_line(draw, (184, 266), (389, 266), 46, cream)
    draw.polygon(
        points(((326, 243), (406, 243), (406, 287), (382, 287),
                (382, 326), (348, 326), (348, 287), (326, 287))),
        fill=cream,
    )
    star4(draw, (149, 266), 29, 29, 9, "#FFD35A")
    key = key.rotate(40, resample=Image.Resampling.BICUBIC, center=(p(256), p(256)))
    image.alpha_composite(key)
    return image


def concept_c():
    image, draw = icon_canvas("#25293A")
    portal = [(154, 372), (154, 225)]
    for angle in range(180, 361, 3):
        radians = math.radians(angle)
        portal.append((255 + 101 * math.cos(radians), 225 + 101 * math.sin(radians)))
    portal.append((356, 372))
    portal = points(portal)
    width = p(48)
    radius = width // 2
    draw.line(portal, fill="#F7F2E8", width=width, joint="curve")
    for x, y in (portal[0], portal[-1]):
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill="#F7F2E8")
    rounded_line(draw, (112, 391), (270, 233), 34, "#42D7B3")
    rounded_line(draw, (112, 391), (174, 329), 46, "#FF6B57")
    star4(draw, (309, 191), 62, 66, 19, "#FFD35A")
    return image


def concept_d():
    image, draw = icon_canvas("#171C22")
    tiles = (
        ((105, 112, 190, 197), "#FF6B57"),
        ((322, 109, 408, 195), "#42D7B3"),
        ((107, 319, 193, 405), "#F7F2E8"),
        ((319, 317, 407, 405), "#4A78E8"),
    )
    for box, color in tiles:
        draw.rounded_rectangle(tuple(p(value) for value in box), radius=p(22), fill=color)
    star4(draw, (256, 256), 111, 111, 34, "#FFD35A")
    draw.ellipse((p(238), p(238), p(274), p(274)), fill="#171C22")
    return image


CONCEPTS = (
    ("A", "星轨魔杖", "轻巧、直接，保留魔术棒本体", concept_a),
    ("B", "万能钥匙", "“打开一切”的语义最直接", concept_b),
    ("C", "传送门", "强调从系统栏进入任意目标", concept_c),
    ("D", "万物绽放", "抽象表达一次唤出多个入口", concept_d),
)


def render(concept, size):
    return concept().resize((size, size), Image.Resampling.LANCZOS)


def font(size, bold=False):
    candidates = (
        Path("C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/segoeuib.ttf" if bold else "C:/Windows/Fonts/segoeui.ttf"),
    )
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def make_board():
    board = Image.new("RGB", (1500, 1080), "#E9EDF1")
    draw = ImageDraw.Draw(board)
    title = font(42, True)
    subtitle = font(22)
    name_font = font(28, True)
    body = font(18)
    small = font(15)
    draw.text((60, 42), "简驭系统图标 · 四个新方向", font=title, fill="#17212B")
    draw.text((60, 102), "共同目标：一眼识别、16px 清楚、表达“快速打开需要的一切”", font=subtitle, fill="#52606D")

    for index, (letter, name, description, concept) in enumerate(CONCEPTS):
        column = index % 2
        row = index // 2
        left = 60 + column * 720
        top = 170 + row * 440
        icon = render(concept, 260)
        board.paste(icon, (left, top), icon)
        draw.text((left + 295, top + 8), f"{letter}  {name}", font=name_font, fill="#17212B")
        draw.text((left + 295, top + 54), description, font=body, fill="#52606D")

        bands = (("浅色系统栏", "#F8F8F8", "#24292F"), ("深色系统栏", "#202124", "#F7F7F7"))
        for band_index, (label, background, foreground) in enumerate(bands):
            y = top + 105 + band_index * 100
            draw.rounded_rectangle((left + 295, y, left + 675, y + 84), radius=8, fill=background)
            draw.text((left + 315, y + 12), label, font=small, fill=foreground)
            x = left + 320
            for size in (16, 24, 32, 48):
                small_icon = render(concept, size)
                board.paste(small_icon, (x, y + 34 + (48 - size) // 2), small_icon)
                x += 78

        if row == 0:
            draw.line((left, top + 405, left + 675, top + 405), fill="#CAD2D9", width=1)
    return board


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for letter, name, _, concept in CONCEPTS:
        render(concept, 512).save(OUTPUT / f"{letter.lower()}-{name}.png")
    make_board().save(OUTPUT / "simpilot-icon-concepts.png")


if __name__ == "__main__":
    main()
