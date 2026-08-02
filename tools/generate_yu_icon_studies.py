"""Render three 'Yu' icon studies without replacing the production icon."""

from __future__ import annotations

from functools import cache
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets" / "yu-icon-studies"
SCALE = 4

BACKGROUND = "#12383B"
HORSE = "#F5E9D0"
HAND = "#F05D55"
INK = "#152228"


def p(value: float) -> int:
    return round(value * SCALE)


def cubic(start, control_1, control_2, end, steps=28):
    result = []
    for index in range(steps + 1):
        t = index / steps
        inverse = 1.0 - t
        result.append(
            (
                inverse**3 * start[0]
                + 3 * inverse**2 * t * control_1[0]
                + 3 * inverse * t**2 * control_2[0]
                + t**3 * end[0],
                inverse**3 * start[1]
                + 3 * inverse**2 * t * control_1[1]
                + 3 * inverse * t**2 * control_2[1]
                + t**3 * end[1],
            )
        )
    return result


def joined(*segments):
    result = []
    for segment in segments:
        result.extend(segment if not result else segment[1:])
    return result


def icon_canvas():
    image = Image.new("RGBA", (512 * SCALE, 512 * SCALE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle(
        (p(28), p(28), p(484), p(484)), radius=p(104), fill=BACKGROUND
    )
    return image, draw


def stroke(draw, path, width, color):
    rendered = [(p(x), p(y)) for x, y in path]
    rendered_width = p(width)
    radius = rendered_width // 2
    draw.line(rendered, fill=color, width=rendered_width, joint="curve")
    for x, y in (rendered[0], rendered[-1]):
        draw.ellipse(
            (x - radius, y - radius, x + radius, y + radius), fill=color
        )


@cache
def concept_ancient():
    """Pictographic route: horse on the left, visible hand on the right."""
    image, draw = icon_canvas()

    horse_head = joined(
        cubic((180, 102), (139, 83), (105, 112), (111, 158)),
        cubic((111, 158), (116, 204), (142, 226), (178, 211)),
        cubic((178, 211), (207, 198), (211, 168), (192, 144)),
    )
    stroke(draw, horse_head, 38, HORSE)
    stroke(draw, cubic((124, 117), (147, 100), (178, 105), (204, 130)), 28, HORSE)
    stroke(draw, cubic((125, 171), (169, 177), (215, 196), (250, 219)), 36, HORSE)
    stroke(draw, [(250, 219), (184, 248)], 36, HORSE)
    stroke(draw, cubic((181, 232), (157, 281), (126, 333), (106, 385)), 31, HORSE)
    stroke(draw, cubic((202, 242), (189, 299), (183, 353), (185, 414)), 37, HORSE)
    stroke(
        draw,
        joined(
            cubic((205, 302), (238, 293), (258, 309), (257, 340)),
            cubic((257, 340), (254, 370), (257, 393), (267, 414)),
        ),
        34,
        HORSE,
    )

    # The right component is deliberately a hand: three fingers, palm and wrist.
    stroke(draw, cubic((300, 137), (319, 151), (331, 173), (341, 208)), 32, HAND)
    stroke(draw, cubic((352, 119), (352, 151), (349, 178), (341, 208)), 32, HAND)
    stroke(draw, cubic((402, 143), (386, 169), (367, 190), (341, 208)), 32, HAND)
    stroke(draw, cubic((341, 208), (330, 264), (345, 327), (367, 411)), 40, HAND)
    stroke(draw, cubic((341, 208), (314, 229), (285, 231), (250, 219)), 36, HAND)
    return image


@cache
def concept_seal():
    """Source-based seal form from CUHK's Multi-function Chinese database."""
    image, _ = icon_canvas()
    source_path = OUTPUT / "source-small-seal-yu.jpg"
    source = Image.open(source_path).convert("L")
    dark = source.point(lambda value: 255 if value < 165 else 0)
    bounds = dark.getbbox()
    if bounds is None:
        raise RuntimeError("The seal-script source image has no visible strokes")

    source = source.crop(bounds)
    dark = source.point(lambda value: max(0, min(255, (232 - value) * 3)))
    split = round(dark.width * 0.50)
    left_mask = Image.new("L", dark.size, 0)
    right_mask = Image.new("L", dark.size, 0)
    left_mask.paste(dark.crop((0, 0, split, dark.height)), (0, 0))
    right_mask.paste(
        dark.crop((split, 0, dark.width, dark.height)), (split, 0)
    )

    target = (p(300), p(390))
    expansion = p(7) + 1 if p(7) % 2 == 0 else p(7)
    left_mask = (
        left_mask.resize(target, Image.Resampling.LANCZOS)
        .filter(ImageFilter.MaxFilter(expansion))
        .filter(ImageFilter.GaussianBlur(p(2.8)))
        .point(lambda value: 255 if value > 94 else 0)
        .filter(ImageFilter.GaussianBlur(p(0.35)))
    )
    right_mask = (
        right_mask.resize(target, Image.Resampling.LANCZOS)
        .filter(ImageFilter.MaxFilter(expansion))
        .filter(ImageFilter.GaussianBlur(p(2.8)))
        .point(lambda value: 255 if value > 94 else 0)
        .filter(ImageFilter.GaussianBlur(p(0.35)))
    )
    left = Image.new("RGBA", target, HORSE)
    right = Image.new("RGBA", target, HAND)
    image.paste(left, (p(106), p(61)), left_mask)
    image.paste(right, (p(106), p(61)), right_mask)
    return image


@cache
def concept_recommended():
    """Recommended route: vectorize the seal form's horse, fingers and wrist."""
    image, draw = icon_canvas()

    stroke(
        draw,
        joined(
            cubic((198, 105), (148, 82), (111, 111), (116, 158)),
            cubic((116, 158), (121, 202), (158, 222), (196, 198)),
        ),
        40,
        HORSE,
    )
    stroke(draw, cubic((126, 128), (151, 111), (181, 116), (204, 139)), 30, HORSE)
    stroke(draw, cubic((125, 177), (151, 165), (179, 170), (203, 187)), 30, HORSE)
    stroke(draw, cubic((127, 231), (165, 215), (207, 219), (239, 241)), 35, HORSE)
    stroke(draw, cubic((169, 238), (145, 272), (119, 303), (94, 329)), 33, HORSE)
    stroke(draw, cubic((183, 249), (158, 301), (130, 350), (108, 395)), 35, HORSE)
    stroke(draw, cubic((201, 252), (195, 306), (190, 360), (190, 414)), 39, HORSE)
    stroke(draw, cubic((224, 253), (226, 307), (228, 361), (234, 414)), 39, HORSE)

    # Three fingers join the outer wrist; the inner stroke keeps the old "又" hand shape.
    outer_hand = joined(
        cubic((286, 126), (331, 110), (376, 114), (404, 136)),
        cubic((404, 136), (405, 222), (403, 314), (414, 411)),
    )
    stroke(draw, outer_hand, 39, HAND)
    stroke(draw, cubic((286, 187), (328, 173), (367, 176), (400, 195)), 37, HAND)
    stroke(draw, cubic((287, 246), (329, 228), (366, 230), (398, 248)), 37, HAND)
    stroke(draw, cubic((293, 248), (286, 302), (282, 358), (283, 414)), 41, HAND)
    return image


CONCEPTS = (
    ("A", "古文字图像感", "马与手最直观，但细节在 16px 较拥挤", concept_ancient),
    ("B", "小篆原形", "采用字库原形；依据最稳，但 16px 笔画会粘连", concept_seal),
    ("C", "小篆图标化", "从原形提炼马首、马足与手的三指、腕部", concept_recommended),
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
    board = Image.new("RGB", (1600, 1120), "#E8ECEE")
    draw = ImageDraw.Draw(board)
    draw.text((64, 42), "简驭系统图标 · ‘馭’字形路线评估", font=font(42, True), fill=INK)
    draw.text(
        (64, 104),
        "统一深青底、马为象牙白、又（手）为朱红；重点观察 16–24px 是否仍能分件",
        font=font(21),
        fill="#536269",
    )

    for index, (letter, name, description, concept) in enumerate(CONCEPTS):
        left = 64 + index * 506
        top = 174
        icon = render(concept, 330)
        board.paste(icon, (left, top), icon)

        if letter == "C":
            draw.rounded_rectangle((left, 530, left + 330, 574), radius=6, fill="#D8E6DF")
            draw.text((left + 92, 539), "推荐方向", font=font(18, True), fill="#185C48")
        draw.text((left, 596), f"{letter}  {name}", font=font(28, True), fill=INK)
        draw.text((left, 640), description, font=font(17), fill="#536269")

        for row, (label, background, foreground) in enumerate(
            (("浅色系统栏", "#F8F8F8", "#252A2E"), ("深色系统栏", "#202326", "#F6F6F6"))
        ):
            y = 704 + row * 156
            draw.rounded_rectangle((left, y, left + 450, y + 132), radius=8, fill=background)
            draw.text((left + 18, y + 14), label, font=font(15), fill=foreground)
            x = left + 20
            for size in (16, 20, 24, 32, 48):
                small_icon = render(concept, size)
                board.paste(small_icon, (x, y + 48 + (48 - size) // 2), small_icon)
                draw.text((x, y + 102), str(size), font=font(13), fill=foreground)
                x += 82
    return board


def make_tray_board():
    board = Image.new("RGB", (1500, 620), "#E8ECEE")
    draw = ImageDraw.Draw(board)
    draw.text((64, 40), "16px 托盘图标像素检查", font=font(40, True), fill=INK)
    draw.text(
        (64, 96),
        "本机 100% 缩放下的实际小图标标准；左侧为 1:1，右侧为 16 倍最近邻放大",
        font=font(20),
        fill="#536269",
    )
    verdicts = {
        "A": "分色清楚，但容易先看成人形",
        "B": "字形依据最稳，但内笔画粘连",
        "C": "马、手仍分件，轮廓最稳定",
    }
    for index, (letter, name, _, concept) in enumerate(CONCEPTS):
        left = 64 + index * 486
        icon = render(concept, 16)
        draw.rounded_rectangle((left, 160, left + 438, 538), radius=12, fill="#F8F8F8")
        draw.text((left + 20, 180), f"{letter}  {name}", font=font(24, True), fill=INK)
        draw.text((left + 20, 224), "实际 16×16", font=font(15), fill="#536269")
        board.paste(icon, (left + 34, 266), icon)
        zoom = icon.resize((256, 256), Image.Resampling.NEAREST)
        board.paste(zoom, (left + 150, 244), zoom)
        color = "#185C48" if letter == "C" else "#536269"
        draw.text((left + 20, 500), verdicts[letter], font=font(16, letter == "C"), fill=color)
    return board


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for letter, _, _, concept in CONCEPTS:
        render(concept, 512).save(OUTPUT / f"{letter.lower()}-yu-study.png")
        for size in (16, 20, 24, 32, 48):
            render(concept, size).save(OUTPUT / f"{letter.lower()}-yu-study-{size}.png")
    make_board().save(OUTPUT / "yu-icon-comparison.png")
    make_tray_board().save(OUTPUT / "yu-icon-16px-inspection.png")


if __name__ == "__main__":
    main()
