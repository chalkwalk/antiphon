#!/usr/bin/env python3
"""Regenerate every logo derivative from one source image.

The mark appears in five places at four sizes. Doing that by hand once is fine;
doing it again when the logo changes is how a stale favicon survives three
redesigns. So it is a script, and the source of truth is a single PNG.

    scripts/make_logo_assets.py path/to/new-logo.png

Writes:
    docs/images/logo.png            512px, the README mark
    website/static/img/logo.png     512px, the site navbar and hero
    website/static/img/favicon.ico  16/32/48/64/128/256
    website/static/img/social-card.png  1200x630 Open Graph card

The source should be square-ish with a transparent background; it is trimmed to
its content and padded back to a square, so framing in the original does not
matter. The social card uses the product typeface from assets/fonts/.

Requires Pillow.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BACKGROUND = (18, 22, 26, 255)
TEAL = (0, 180, 216, 255)
SUBTITLE = (207, 233, 241, 255)


def squared(path):
    """Trim to content and pad to a centred square, so the mark fills its box."""
    image = Image.open(path).convert("RGBA")
    box = image.getbbox()
    if box is None:
        sys.exit("{}: image is entirely transparent".format(path))
    image = image.crop(box)
    width, height = image.size
    side = max(width, height)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(image, ((side - width) // 2, (side - height) // 2))
    return canvas


def social_card(mark, out):
    card = Image.new("RGBA", (1200, 630), BACKGROUND)
    card.alpha_composite(mark.resize((360, 360), Image.LANCZOS), (90, 135))

    fonts = os.path.join(REPO, "assets", "fonts")
    title = ImageFont.truetype(os.path.join(fonts, "Inter-Bold.ttf"), 96)
    body = ImageFont.truetype(os.path.join(fonts, "Inter-Regular.ttf"), 40)
    small = ImageFont.truetype(os.path.join(fonts, "Inter-Regular.ttf"), 30)

    draw = ImageDraw.Draw(card)
    draw.text((520, 210), "Antiphon", font=title, fill=(255, 255, 255, 255))
    draw.text((524, 330), "Jam with strangers,", font=body, fill=SUBTITLE)
    draw.text((524, 382), "from inside your DAW", font=body, fill=SUBTITLE)
    draw.text((524, 452), "NINJAM client - VST3, CLAP, standalone",
              font=small, fill=TEAL)
    card.convert("RGB").save(out, optimize=True)


def main(source):
    mark = squared(source)

    targets = {
        os.path.join(REPO, "docs", "images", "logo.png"): 512,
        os.path.join(REPO, "website", "static", "img", "logo.png"): 512,
    }
    for path, size in targets.items():
        os.makedirs(os.path.dirname(path), exist_ok=True)
        mark.resize((size, size), Image.LANCZOS).save(path, optimize=True)

    icon = os.path.join(REPO, "website", "static", "img", "favicon.ico")
    # Each cut is resampled from the full-resolution mark rather than from one
    # scaled copy; a detailed logo loses too much when 16px comes off 32px.
    mark.resize((256, 256), Image.LANCZOS).save(
        icon, sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    )

    card = os.path.join(REPO, "website", "static", "img", "social-card.png")
    social_card(mark, card)

    for path in list(targets) + [icon, card]:
        print("{:5d} KB  {}".format(os.path.getsize(path) // 1024,
                                    os.path.relpath(path, REPO)))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
