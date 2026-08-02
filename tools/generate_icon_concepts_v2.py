"""Render non-magical Simpilot icon concepts and a system-size comparison board."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from generate_icon_concepts import SCALE, icon_canvas, p, points, rounded_line


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets" / "icon-concepts-v2"


def concept_a():
    image, draw = icon_canvas("#16353B")
    rounded_line(draw, (144, 367), (368, 143), 24, "#2C4B50")
    tiles = (
        ((91, 317, 195, 421), "#FF6B57"),
        ((204, 204, 308, 308), "#F6F1E7"),
        ((317, 91, 421, 195), "#FFD15A"),
    )
    for box, color in tiles:
        draw.rounded_rectangle(tuple(p(value) for value in box), radius=p(25), fill=color)
    return image


def concept_b():
    image, draw = icon_canvas("#D95750")
    draw.rounded_rectangle((p(184), p(96), p(416), p(328)), radius=p(62), fill="#42D7B3")
    draw.rounded_rectangle((p(96), p(184), p(328), p(416)), radius=p(62), fill="#F7F2E8")
    draw.rounded_rectangle((p(166), p(254), p(258), p(346)), radius=p(25), fill="#17242C")
    draw.rounded_rectangle((p(226), p(156), p(374), p(190)), radius=p(17), fill="#17242C")
    return image


def concept_c():
    image, draw = icon_canvas("#18242B")
    center = (256, 262)
    nodes = (
        ((256, 112), "#42D7B3"),
        ((126, 344), "#FF6B57"),
        ((386, 344), "#F7F2E8"),
    )
    for node, _ in nodes:
        rounded_line(draw, center, node, 34, "#45545A")
    for (x, y), color in nodes:
        draw.ellipse((p(x - 47), p(y - 47), p(x + 47), p(y + 47)), fill=color)
    draw.ellipse((p(191), p(197), p(321), p(327)), fill="#FFD15A")
    draw.ellipse((p(230), p(236), p(282), p(288)), fill="#18242B")
    return image


def concept_d():
    image, draw = icon_canvas("#D95750")
    for y, end in ((166, 273), (256, 313), (346, 273)):
        rounded_line(draw, (112, y), (end, y), 44, "#F7F2E8")
    draw.polygon(
        points(((282, 112), (424, 256), (282, 400),
                (282, 327), (352, 256), (282, 185))),
        fill="#FFD15A",
    )
    return image


CONCEPTS = (
    ("A", "快捷阶梯", "三个清晰阶段，表达更快抵达目标", concept_a),
    ("B", "热键叠片", "两个键帽叠合，强调全局快捷键", concept_b),
    ("C", "指令枢纽", "一个入口连接多个动作与目标", concept_c),
    ("D", "菜单前进", "菜单与启动合并，功能表达最直接", concept_d),
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
    draw.text((60, 42), "简驭系统图标 · 从产品重新出发", font=font(42, True), fill="#17212B")
    draw.text(
        (60, 102),
        "不使用字母、仪表盘或魔法符号，只表达快捷启动、热键与集中控制",
        font=font(22),
        fill="#52606D",
    )

    for index, (letter, name, description, concept) in enumerate(CONCEPTS):
        column = index % 2
        row = index // 2
        left = 60 + column * 720
        top = 170 + row * 440
        icon = render(concept, 260)
        board.paste(icon, (left, top), icon)
        draw.text((left + 295, top + 8), f"{letter}  {name}", font=font(28, True), fill="#17212B")
        draw.text((left + 295, top + 54), description, font=font(18), fill="#52606D")

        bands = (("浅色系统栏", "#F8F8F8", "#24292F"), ("深色系统栏", "#202124", "#F7F7F7"))
        for band_index, (label, background, foreground) in enumerate(bands):
            y = top + 105 + band_index * 100
            draw.rounded_rectangle((left + 295, y, left + 675, y + 84), radius=8, fill=background)
            draw.text((left + 315, y + 12), label, font=font(15), fill=foreground)
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
    make_board().save(OUTPUT / "simpilot-icon-concepts-v2.png")


if __name__ == "__main__":
    main()
