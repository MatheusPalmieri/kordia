"""Gera a logo do Kordia (K de tubo de neon, branco sobre quadrado preto): python assets/gerar_icones.py

Saídas nesta pasta: kordia.svg (vetor), kordia-1024.png e kordia-32.png (ícone do app JUCE), kordia.ico (atalho).
A geometria repete a do <symbol id="logo"> de acompanhante/ui/index.html; mudou aqui, mude lá.
"""
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).parent
BOX = 512
STEM = [(160, 112), (160, 400)]
ARMS = [(356, 112), (232, 256), (356, 400)]
STROKE = 28
MARK_SCALE = .8      # the mark sits in the middle of the tile with room for the glow
CORNER = .225        # corner radius / side (app-icon proportion)
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def render(size):
    small = size <= 32   # at icon sizes the glow turns to mud: bigger, heavier mark and only a faint halo
    ss = 8 if size < 256 else 4
    side = size * ss
    scale = side / BOX * (1 if small else MARK_SCALE)
    width = round((46 if small else STROKE) * scale)
    place = lambda p: (side / 2 + (p[0] - BOX / 2) * scale, side / 2 + (p[1] - BOX / 2) * scale)

    tube = Image.new("L", (side, side), 0)
    pen = ImageDraw.Draw(tube)
    for line in (STEM, ARMS):
        pts = [place(p) for p in line]
        pen.line(pts, fill=255, width=width, joint="curve")
        for x, y in (pts[0], pts[-1]):   # round caps: PIL lines end flat
            pen.ellipse((x - width / 2, y - width / 2, x + width / 2, y + width / 2), fill=255)

    near = tube.filter(ImageFilter.GaussianBlur(width * .45))
    far = tube.filter(ImageFilter.GaussianBlur(width * 1.5))
    glow = ImageChops.add(near, far.point(lambda v: min(255, v * (.5 if small else 1.5))))

    # white tube, slightly cool glow: reads as neon rather than as a blur
    rgb = [ImageChops.add(tube, glow.point(lambda v: v * k)) for k in (.84, .91, 1)]
    if size >= 48:   # hairline so the black tile does not vanish on a dark taskbar or desktop
        edge = Image.new("L", (side, side), 0)
        ImageDraw.Draw(edge).rounded_rectangle((0, 0, side - 1, side - 1), side * CORNER, outline=38, width=max(ss, side // 300))
        rgb = [ImageChops.add(c, edge) for c in rgb]

    alpha = Image.new("L", (side, side), 0)
    ImageDraw.Draw(alpha).rounded_rectangle((0, 0, side - 1, side - 1), side * CORNER, fill=255)
    return Image.merge("RGBA", (*rgb, alpha)).resize((size, size), Image.LANCZOS)


def svg():
    path = lambda pts: "M" + " ".join(f"{x} {y}" for x, y in pts)
    transform = f"translate({BOX / 2 * (1 - MARK_SCALE):g} {BOX / 2 * (1 - MARK_SCALE):g}) scale({MARK_SCALE})"
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {BOX} {BOX}">
  <title>Kordia</title>
  <defs>
    <filter id="neon" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur stdDeviation="7" result="near"/>
      <feGaussianBlur stdDeviation="24" result="far"/>
      <feFlood flood-color="#d6e8ff"/>
      <feComposite in2="far" operator="in" result="halo"/>
      <feMerge><feMergeNode in="halo"/><feMergeNode in="halo"/><feMergeNode in="near"/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
  </defs>
  <rect width="{BOX}" height="{BOX}" rx="{BOX * CORNER:g}" fill="#000"/>
  <g transform="{transform}" filter="url(#neon)" fill="none" stroke="#fff" stroke-width="{STROKE}" stroke-linecap="round" stroke-linejoin="round">
    <path d="{path(STEM)}"/>
    <path d="{path(ARMS)}"/>
  </g>
</svg>
"""


if __name__ == "__main__":
    (HERE / "kordia.svg").write_text(svg(), encoding="utf-8")
    render(1024).save(HERE / "kordia-1024.png")
    render(32).save(HERE / "kordia-32.png")
    icons = [render(s) for s in ICO_SIZES]
    icons[-1].save(HERE / "kordia.ico", sizes=[(s, s) for s in ICO_SIZES], append_images=icons[:-1])
