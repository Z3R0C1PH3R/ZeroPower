#!/usr/bin/env python3
"""Dibuja el icono de la app (una batería con un rayo) en los tamaños que piden el lanzador
(assets/images) y la tienda (store/icon.png). Necesita Pillow.

    Tools/generar_icono.py
"""

import os

from PIL import Image, ImageDraw

LADO = 1024  # se dibuja grande y se reduce: así los bordes salen suaves


def icono():
    imagen = Image.new("RGBA", (LADO, LADO), (0, 0, 0, 0))

    # Fondo: degradado verde oscuro en un cuadrado de esquinas redondas.
    fondo = Image.new("RGBA", (LADO, LADO))
    trazo = ImageDraw.Draw(fondo)
    arriba, abajo = (24, 88, 64), (10, 34, 26)
    for y in range(LADO):
        t = y / (LADO - 1)
        trazo.line([(0, y), (LADO, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(arriba, abajo)) + (255,))
    mascara = Image.new("L", (LADO, LADO), 0)
    ImageDraw.Draw(mascara).rounded_rectangle([40, 40, LADO - 40, LADO - 40], radius=200, fill=255)
    imagen.paste(fondo, (0, 0), mascara)

    d = ImageDraw.Draw(imagen)
    blanco = (236, 246, 240, 255)
    verde = (99, 226, 183, 255)

    # La batería, tumbada: cuerpo con borde grueso y el borne a la derecha.
    x0, y0, x1, y1 = 190, 330, 790, 700
    d.rounded_rectangle([x0, y0, x1, y1], radius=70, outline=blanco, width=48)
    d.rounded_rectangle([x1 + 14, 440, x1 + 74, 590], radius=24, fill=blanco)

    # Nivel: casi llena, en verde.
    m = 48 + 30
    d.rounded_rectangle([x0 + m, y0 + m, x1 - m, y1 - m], radius=30, fill=verde)

    # El rayo encima, en blanco con un borde del color del fondo para que se separe.
    rayo = [(560, 250), (380, 545), (500, 545), (440, 790), (660, 470), (530, 470), (600, 250)]
    d.polygon(rayo, fill=(16, 52, 40, 255))
    rayo_dentro = [(560, 290), (412, 527), (526, 527), (474, 735), (626, 488), (500, 488), (580, 290)]
    d.polygon(rayo_dentro, fill=blanco)
    return imagen


def main():
    raiz = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    os.makedirs(os.path.join(raiz, "store"), exist_ok=True)
    imagen = icono()
    for ruta, lado in (
        ("assets/images/zeropower.png", 100),
        ("assets/images/zeropower_100.png", 100),
        ("assets/images/zeropower_80.png", 80),
        ("store/icon.png", 256),
    ):
        imagen.resize((lado, lado), Image.LANCZOS).save(os.path.join(raiz, ruta))


if __name__ == "__main__":
    main()
