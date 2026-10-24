import json
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageOps

output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=True)
photo = Image.new("RGB", (640, 360))
draw = ImageDraw.Draw(photo)
draw.rectangle((0, 0, 319, 179), fill=(230, 40, 30))
draw.rectangle((320, 0, 639, 179), fill=(30, 210, 60))
draw.rectangle((0, 180, 319, 359), fill=(30, 60, 225))
draw.rectangle((320, 180, 639, 359), fill=(230, 210, 35))
expected = []
for extension in ("png", "jpg", "bmp", "gif", "tiff", "webp", "avif"):
    target = output / f"quadrants.{extension}"
    photo.save(target)
    expected.append({"file": target.name, "width": 640, "height": 360, "corner": [230, 40, 30]})
for extension in ("jpg", "webp", "avif"):
    for direction in range(2, 9):
        exif = Image.Exif()
        exif[274] = direction
        target = output / f"orientation-{direction}.{extension}"
        photo.save(target, exif=exif)
        with Image.open(target) as image:
            oriented = ImageOps.exif_transpose(image).convert("RGB")
            expected.append({"file": target.name, "width": oriented.width, "height": oriented.height, "corner": oriented.getpixel((30, 30))})
(output / "broken.png").write_bytes(b"not an image")
(output / "expected.json").write_text(json.dumps(expected, indent=2), encoding="utf-8")
print(f"已生成 {len(expected)} 份图片与损坏文件，用于独立解码验证。")
