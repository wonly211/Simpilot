"""Generate Simpilot's production raster icon assets from the selected D study."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
MASTER = ASSETS / "jian-yu-icon-studies" / "d-square-seal-1024.png"
SIZES = (16, 20, 24, 32, 48, 64, 128, 256)
INK = "#151A22"


def render_icon(master: Image.Image, size: int) -> Image.Image:
    """Downsample the bitmap master without reconstructing or tracing its glyphs."""
    return master.resize((size, size), Image.Resampling.LANCZOS)


def load_font(size: int):
    candidates = (
        Path("C:/Windows/Fonts/segoeui.ttf"),
        Path("C:/Windows/Fonts/arial.ttf"),
    )
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def make_preview(master: Image.Image, icons: dict[int, Image.Image]) -> Image.Image:
    preview = Image.new("RGB", (1200, 720), "#E9EDF1")
    draw = ImageDraw.Draw(preview)
    title_font = load_font(42)
    body_font = load_font(22)
    label_font = load_font(18)

    draw.text((64, 48), "Simpilot system icon", font=title_font, fill=INK)
    draw.text((64, 108), "Jian + Yu square seal / raster master / built for 16 px", font=body_font, fill="#52606D")

    hero = render_icon(master, 320)
    preview.paste(hero, (72, 190), hero)

    panels = (("Light system bar", "#F7F7F7", "#202124"), ("Dark system bar", "#202124", "#F5F5F5"))
    for row, (label, background, foreground) in enumerate(panels):
        top = 192 + row * 190
        draw.rounded_rectangle((470, top, 1136, top + 150), radius=8, fill=background)
        draw.text((500, top + 20), label, font=label_font, fill=foreground)
        x = 510
        for size in (16, 20, 24, 32, 48, 64):
            icon = icons[size]
            y = top + 66 + (64 - size) // 2
            preview.paste(icon, (x, y), icon)
            draw.text((x - 2, top + 126), str(size), font=label_font, fill=foreground)
            x += 92
    return preview


def main() -> None:
    master = Image.open(MASTER).convert("RGBA")
    icons = {size: render_icon(master, size) for size in SIZES}

    icons[256].save(ASSETS / "simpilot-icon.png")
    master.save(ASSETS / "simpilot.ico", format="ICO", sizes=[(size, size) for size in SIZES])
    make_preview(master, icons).save(ASSETS / "simpilot-icon-preview.png")


if __name__ == "__main__":
    main()
