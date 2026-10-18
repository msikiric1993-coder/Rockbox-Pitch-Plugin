"""Regenerate the original TDSpeed icon (optional authoring dependency: Pillow)."""
from pathlib import Path
from math import cos, sin, radians
from PIL import Image, ImageDraw
assets=Path(__file__).resolve().parents[1]/"Source"/"Assets"
assets.mkdir(exist_ok=True)
k=4
image=Image.new("RGBA",(256*k,256*k))
d=ImageDraw.Draw(image)
d.rounded_rectangle((4*k,4*k,252*k,252*k),radius=58*k,fill="#10171b",outline="#304047",width=8*k)
d.arc((43*k,43*k,213*k,213*k),145,395,fill="#81dfb5",width=15*k)
ends=[(128+85*cos(radians(a)),128+85*sin(radians(a)),"#81dfb5") for a in (145,395)]
d.line([(90*k,94*k),(166*k,94*k)],fill="#eef5f2",width=15*k)
d.line([(128*k,94*k),(128*k,158*k)],fill="#eef5f2",width=15*k)
ends += [(90,94,"#eef5f2"),(166,94,"#eef5f2"),(128,158,"#eef5f2")]
for x,y,colour in ends:d.ellipse(((x-7.5)*k,(y-7.5)*k,(x+7.5)*k,(y+7.5)*k),fill=colour)
wave=[(60,196),(89,196),(104,175),(119,214),(138,174),(154,196),(196,196)]
d.line([(x*k,y*k) for x,y in wave],fill="#81dfb5",width=9*k,joint="curve")
for x,y in wave:d.ellipse(((x-4.5)*k,(y-4.5)*k,(x+4.5)*k,(y+4.5)*k),fill="#81dfb5")
image=image.resize((256,256),Image.Resampling.LANCZOS)
image.save(assets/"TDSpeed.png")
image.save(assets/"TDSpeed.ico",sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
