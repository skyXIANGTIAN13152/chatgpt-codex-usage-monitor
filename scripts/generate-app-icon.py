"""Build the multi-resolution Windows icon from the transparent monitor artwork."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def crop_for_size(source: Image.Image, alpha_box: tuple[int, int, int, int], size: int) -> Image.Image:
    left, top, right, bottom = alpha_box
    object_width = right - left
    side_padding = max(8, round(object_width * 0.055))

    if size <= 24:
        # The crystal head carries the recognizable silhouette at tray-icon sizes.
        crop_box = (left - side_padding, top - 8, right + side_padding, min(bottom, top + 520))
    elif size <= 48:
        # Add the gold collar at taskbar sizes without shrinking the head into a thin line.
        crop_box = (left - side_padding, top - 10, right + side_padding, min(bottom, top + 690))
    else:
        # Explorer's large views show the complete device.
        vertical_padding = max(8, round((bottom - top) * 0.025))
        crop_box = (
            left - side_padding,
            top - vertical_padding,
            right + side_padding,
            bottom + vertical_padding,
        )

    crop_box = (
        max(0, crop_box[0]),
        max(0, crop_box[1]),
        min(source.width, crop_box[2]),
        min(source.height, crop_box[3]),
    )
    return source.crop(crop_box)


def make_frame(source: Image.Image, alpha_box: tuple[int, int, int, int], size: int) -> Image.Image:
    subject = crop_for_size(source, alpha_box, size)
    margin = max(1, round(size * (0.055 if size <= 48 else 0.045)))
    available = size - margin * 2
    subject.thumbnail((available, available), Image.Resampling.LANCZOS, reducing_gap=3.0)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    x = (size - subject.width) // 2
    y = (size - subject.height) // 2
    canvas.alpha_composite(subject, (x, y))
    return canvas


def make_preview(frames: list[Image.Image], output: Path) -> None:
    scale = 4
    gap = 24
    tile = 280
    preview = Image.new("RGB", (tile * 3, tile * 3), (25, 28, 40))
    draw = ImageDraw.Draw(preview)
    for index, frame in enumerate(frames):
        row, column = divmod(index, 3)
        enlarged = frame.resize((frame.width * scale, frame.height * scale), Image.Resampling.NEAREST)
        x = column * tile + (tile - enlarged.width) // 2
        y = row * tile + max(gap, (tile - enlarged.height) // 2)
        preview.paste(enlarged, (x, y), enlarged)
        draw.text((column * tile + 10, row * tile + 8), f"{frame.width} px", fill=(220, 230, 255))
    output.parent.mkdir(parents=True, exist_ok=True)
    preview.save(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    source = Image.open(args.input).convert("RGBA")
    alpha_box = source.getchannel("A").getbbox()
    if alpha_box is None:
        raise ValueError("Input artwork is fully transparent")

    frames = [make_frame(source, alpha_box, size) for size in ICON_SIZES]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    frames[-1].save(
        args.output,
        format="ICO",
        sizes=[(size, size) for size in ICON_SIZES],
        append_images=frames[:-1],
    )
    if args.preview:
        make_preview(frames, args.preview)


if __name__ == "__main__":
    main()
