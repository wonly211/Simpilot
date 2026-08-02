"""Build raster-only Simpilot icon studies from the supplied ancient glyph images."""

from __future__ import annotations

from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "jian-yu-icon-studies"
JIAN_SOURCE = OUT / "reference-jian.png"
YU_SOURCE = OUT / "reference-yu.jpg"

INK = (20, 49, 51, 255)
PAPER = (246, 239, 221, 255)
VERMILION = (210, 65, 50, 255)
CINNABAR = (227, 66, 52, 255)
JADE = (38, 105, 100, 255)
GOLD = (237, 199, 119, 255)
WHITE = (255, 255, 255, 255)


def connected_components(mask: np.ndarray, minimum_area: int = 7) -> np.ndarray:
    """Remove isolated scan noise while preserving every connected brush fragment."""
    height, width = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    clean = np.zeros_like(mask, dtype=bool)
    for y in range(height):
        for x in range(width):
            if not mask[y, x] or seen[y, x]:
                continue
            queue = deque([(x, y)])
            seen[y, x] = True
            points: list[tuple[int, int]] = []
            while queue:
                px, py = queue.popleft()
                points.append((px, py))
                for ny in range(max(0, py - 1), min(height, py + 2)):
                    for nx in range(max(0, px - 1), min(width, px + 2)):
                        if mask[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True
                            queue.append((nx, ny))
            if len(points) >= minimum_area:
                for px, py in points:
                    clean[py, px] = True
    return clean


def extract_ink(path: Path) -> Image.Image:
    """Extract the red brush pixels from the paper without tracing or redrawing."""
    image = Image.open(path).convert("RGB")
    pixels = np.asarray(image, dtype=np.float32)
    red, green, blue = pixels[..., 0], pixels[..., 1], pixels[..., 2]
    redness = red - np.maximum(green, blue)
    seed = connected_components(redness > 34, minimum_area=7)

    # Retain soft scanned edges only around confirmed ink regions.
    seed_image = Image.fromarray((seed * 255).astype(np.uint8), "L")
    neighborhood = np.asarray(seed_image.filter(ImageFilter.MaxFilter(5))) > 0
    alpha = np.clip((redness - 18) * 4.8, 0, 255)
    alpha = np.where(neighborhood, alpha, 0).astype(np.uint8)
    alpha_image = Image.fromarray(alpha, "L").filter(ImageFilter.GaussianBlur(0.35))

    bbox = alpha_image.getbbox()
    if bbox is None:
        raise RuntimeError(f"No red ink detected in {path}")
    margin = 4
    left = max(0, bbox[0] - margin)
    top = max(0, bbox[1] - margin)
    right = min(image.width, bbox[2] + margin)
    bottom = min(image.height, bbox[3] + margin)
    return alpha_image.crop((left, top, right, bottom))


def colored_glyph(mask: Image.Image, color: tuple[int, int, int, int]) -> Image.Image:
    layer = Image.new("RGBA", mask.size, color)
    layer.putalpha(mask)
    return layer


def fit_glyph(mask: Image.Image, box: tuple[int, int, int, int], color) -> Image.Image:
    left, top, right, bottom = box
    width, height = right - left, bottom - top
    scale = min(width / mask.width, height / mask.height)
    size = (max(1, round(mask.width * scale)), max(1, round(mask.height * scale)))
    resized = mask.resize(size, Image.Resampling.LANCZOS)
    glyph = colored_glyph(resized, color)
    layer = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    x = left + (width - size[0]) // 2
    y = top + (height - size[1]) // 2
    layer.alpha_composite(glyph, (x, y))
    return layer


def rounded_background(color, radius=190) -> Image.Image:
    image = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    ImageDraw.Draw(image).rounded_rectangle((36, 36, 988, 988), radius=radius, fill=color)
    return image


def concept_side_by_side(jian: Image.Image, yu: Image.Image) -> Image.Image:
    icon = rounded_background(INK)
    icon.alpha_composite(fit_glyph(jian, (95, 145, 507, 875), PAPER))
    icon.alpha_composite(fit_glyph(yu, (510, 135, 925, 885), VERMILION))
    return icon


def concept_jian_frame(jian: Image.Image, yu: Image.Image) -> Image.Image:
    icon = rounded_background(JADE)
    icon.alpha_composite(fit_glyph(jian, (125, 88, 899, 936), PAPER))
    # The original Yu remains intact, centered as a contrasting seal within Jian.
    icon.alpha_composite(fit_glyph(yu, (365, 340, 750, 806), VERMILION))
    return icon


def concept_offset(jian: Image.Image, yu: Image.Image) -> Image.Image:
    icon = rounded_background((238, 227, 202, 255))
    icon.alpha_composite(fit_glyph(jian, (83, 80, 660, 755), INK))
    icon.alpha_composite(fit_glyph(yu, (420, 285, 931, 910), VERMILION))
    return icon


def concept_seal(jian: Image.Image, yu: Image.Image) -> Image.Image:
    icon = rounded_background(CINNABAR)
    icon.alpha_composite(fit_glyph(jian, (46, 62, 526, 964), WHITE))
    icon.alpha_composite(fit_glyph(yu, (492, 58, 982, 968), WHITE))
    return icon


def downsample(icon: Image.Image, size: int) -> Image.Image:
    return icon.resize((size, size), Image.Resampling.LANCZOS)


def load_font(size: int, bold=False):
    name = "seguisb.ttf" if bold else "segoeui.ttf"
    path = Path("C:/Windows/Fonts") / name
    return ImageFont.truetype(str(path), size) if path.exists() else ImageFont.load_default()


def checkerboard(size: tuple[int, int], cell=12) -> Image.Image:
    image = Image.new("RGB", size, "#FFFFFF")
    draw = ImageDraw.Draw(image)
    for y in range(0, size[1], cell):
        for x in range(0, size[0], cell):
            if (x // cell + y // cell) % 2:
                draw.rectangle((x, y, x + cell - 1, y + cell - 1), fill="#E5E7EB")
    return image


def make_board(concepts: list[tuple[str, str, Image.Image]]) -> Image.Image:
    board = Image.new("RGB", (1800, 1320), "#EEF1F2")
    draw = ImageDraw.Draw(board)
    title = load_font(46, bold=True)
    subtitle = load_font(23)
    label = load_font(25, bold=True)
    note = load_font(19)
    draw.text((64, 45), "Simpilot — Jian + Yu raster icon studies", font=title, fill="#14282A")
    draw.text((64, 106), "Original scanned brush shapes; no vector tracing. Taskbar checks at 16 / 24 / 48 px.", font=subtitle, fill="#536568")

    for index, (name, description, icon) in enumerate(concepts):
        col, row = index % 2, index // 2
        x = 64 + col * 862
        y = 170 + row * 560
        draw.rounded_rectangle((x, y, x + 810, y + 510), radius=8, fill="#FFFFFF", outline="#D4DCDE", width=2)
        hero = downsample(icon, 310)
        board.paste(hero, (x + 28, y + 62), hero)
        draw.text((x + 28, y + 20), name, font=label, fill="#14282A")
        draw.text((x + 370, y + 68), description, font=note, fill="#536568")

        for bar_index, (bar_color, text_color, bar_name) in enumerate((("#F6F7F8", "#172124", "LIGHT"), ("#1D2225", "#F7F7F7", "DARK"))):
            top = y + 120 + bar_index * 128
            draw.rounded_rectangle((x + 365, top, x + 782, top + 102), radius=7, fill=bar_color)
            draw.text((x + 383, top + 12), bar_name, font=note, fill=text_color)
            px = x + 390
            for size in (16, 24, 48):
                sample = downsample(icon, size)
                py = top + 43 + (48 - size) // 2
                board.paste(sample, (px, py), sample)
                draw.text((px + size + 8, top + 55), str(size), font=note, fill=text_color)
                px += 116

        draw.text((x + 370, y + 407), "transparent PNG masters · 1024 × 1024", font=note, fill="#758589")
    return board


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    jian = extract_ink(JIAN_SOURCE)
    yu = extract_ink(YU_SOURCE)
    concepts = [
        ("A · DOUBLE MARK", "Most literal / both glyphs complete", concept_side_by_side(jian, yu)),
        ("B · JIAN FRAME", "Jian defines the silhouette / Yu is the focus", concept_jian_frame(jian, yu)),
        ("C · OFFSET IMPRINT", "More dynamic / keeps the paper-and-ink character", concept_offset(jian, yu)),
        ("D · SQUARE SEAL", "Cinnabar field / enlarged pure-white glyphs", concept_seal(jian, yu)),
    ]
    for code, (_, _, icon) in zip(("a-double-mark", "b-jian-frame", "c-offset-imprint", "d-square-seal"), concepts):
        icon.save(OUT / f"{code}-1024.png")
        for size in (16, 24, 32, 48, 64, 128, 256):
            downsample(icon, size).save(OUT / f"{code}-{size}.png")
    make_board(concepts).save(OUT / "jian-yu-raster-comparison.png")


if __name__ == "__main__":
    main()
