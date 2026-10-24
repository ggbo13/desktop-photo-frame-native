from pathlib import Path
from PIL import Image, ImageDraw

image = Image.new("RGBA", (256, 256))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((16, 16, 240, 240), radius=40, fill="#203336")
draw.rounded_rectangle((43, 43, 213, 213), radius=14, fill="#D9E8E3")
draw.ellipse((143, 68, 177, 102), fill="#F5F6F3")
draw.polygon(((43, 170), (94, 105), (157, 182), (190, 132), (213, 163), (213, 200), (43, 200)), fill="#287A7F")
image.save(Path(__file__).parent / "frame.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
