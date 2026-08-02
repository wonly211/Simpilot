"""Generate non-destructive rounding studies for selected Yu source glyphs."""

from __future__ import annotations

from collections import deque
from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "assets" / "yu-small-seal-sources"
OUTPUT = ROOT / "assets" / "yu-rounded-studies"
SVG_NS = "http://www.w3.org/2000/svg"


def write_rounded_svg(source: Path, target: Path, stroke_width: int) -> None:
    ET.register_namespace("", SVG_NS)
    tree = ET.parse(source)
    root = tree.getroot()

    for child in list(root):
        if child.tag == f"{{{SVG_NS}}}g":
            root.remove(child)

    for path in root.findall(f"{{{SVG_NS}}}path"):
        path.set("fill", "#111719")
        if stroke_width:
            path.set("stroke", "#111719")
            path.set("stroke-width", str(stroke_width))
            path.set("stroke-linejoin", "round")
            path.set("stroke-linecap", "round")
            path.set("paint-order", "stroke fill")

    target.write_bytes(ET.tostring(root, encoding="utf-8", xml_declaration=True))


def connected_components(mask: Image.Image):
    width, height = mask.size
    pixels = mask.load()
    visited = bytearray(width * height)
    components = []

    for y in range(height):
        for x in range(width):
            index = y * width + x
            if visited[index] or pixels[x, y] == 0:
                continue

            queue = deque([(x, y)])
            visited[index] = 1
            points = []
            min_x = max_x = x
            min_y = max_y = y

            while queue:
                current_x, current_y = queue.popleft()
                points.append((current_x, current_y))
                min_x = min(min_x, current_x)
                max_x = max(max_x, current_x)
                min_y = min(min_y, current_y)
                max_y = max(max_y, current_y)

                for offset_x, offset_y in (
                    (-1, -1), (0, -1), (1, -1),
                    (-1, 0),           (1, 0),
                    (-1, 1),  (0, 1),  (1, 1),
                ):
                    neighbor_x = current_x + offset_x
                    neighbor_y = current_y + offset_y
                    if not (0 <= neighbor_x < width and 0 <= neighbor_y < height):
                        continue
                    neighbor_index = neighbor_y * width + neighbor_x
                    if visited[neighbor_index] or pixels[neighbor_x, neighbor_y] == 0:
                        continue
                    visited[neighbor_index] = 1
                    queue.append((neighbor_x, neighbor_y))

            components.append(
                {
                    "points": points,
                    "area": len(points),
                    "bbox": (min_x, min_y, max_x + 1, max_y + 1),
                }
            )

    return components


def clean_scan(source: Path, target_dark: Path, target_light: Path) -> tuple[int, int]:
    image = Image.open(source).convert("L")
    softened = image.filter(ImageFilter.MedianFilter(3))
    mask = softened.point(lambda value: 255 if value >= 170 else 0)
    mask_pixels = mask.load()
    width, height = mask.size

    # The scan has a noisy white frame joined to the right component. Remove only
    # the frame bands and the two corner fields before component filtering.
    for y in range(height):
        for x in range(width):
            in_frame = x < width * 0.035 or x > width * 0.965 or y < height * 0.06 or y > height * 0.965
            in_top_right_noise = x > width * 0.60 and y < height * 0.20
            in_bottom_right_noise = x > width * 0.58 and y > height * 0.92
            if in_frame or in_top_right_noise or in_bottom_right_noise:
                mask_pixels[x, y] = 0

    components = connected_components(mask)

    # Keep the two main glyph components and the detached horse-tail stroke.
    # Other detached marks in this scan are border speckles or paper damage.
    kept = [
        component
        for component in components
        if component["area"] >= 2000
        or (component["area"] >= 500 and component["bbox"][0] < width * 0.25)
    ]
    cleaned_mask = Image.new("L", image.size, 0)
    cleaned_pixels = cleaned_mask.load()
    for component in kept:
        for x, y in component["points"]:
            cleaned_pixels[x, y] = 255

    bounds = cleaned_mask.getbbox()
    if bounds is None:
        raise RuntimeError("Noise cleanup removed every foreground component")

    margin = 14
    left = max(0, bounds[0] - margin)
    top = max(0, bounds[1] - margin)
    right = min(image.width, bounds[2] + margin)
    bottom = min(image.height, bounds[3] + margin)
    cleaned_mask = cleaned_mask.crop((left, top, right, bottom))

    dark = Image.new("L", cleaned_mask.size, 0)
    dark.paste(255, mask=cleaned_mask)
    dark.save(target_dark)

    light = Image.new("L", cleaned_mask.size, 255)
    light.paste(0, mask=cleaned_mask)
    light.save(target_light)
    return len(components), len(kept)


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    source_svg = SOURCES / "02-shuowen-ancient-form-yu.svg"
    for label, width in (("original-clean", 0), ("soft-round", 8), ("round", 16)):
        write_rounded_svg(source_svg, OUTPUT / f"yu-first-{label}.svg", width)

    component_count, kept_count = clean_scan(
        SOURCES / "04-ding-foyan-seal.png",
        OUTPUT / "yu-fifth-clean-white-on-black.png",
        OUTPUT / "yu-fifth-clean-black-on-white.png",
    )
    print(f"Fifth source components: {component_count}; kept after cleanup: {kept_count}")


if __name__ == "__main__":
    main()
