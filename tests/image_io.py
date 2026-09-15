"""Minimal P3 reader shared by image regression tests."""
from pathlib import Path


def read_ppm(path, width, height):
    tokens = []
    for line in Path(path).read_text().splitlines():
        tokens.extend(line.split('#', 1)[0].split())
    assert tokens[:4] == ['P3', str(width), str(height), '255'], tokens[:4]
    pixels = [int(value) for value in tokens[4:]]
    assert len(pixels) == width * height * 3
    assert all(0 <= value <= 255 for value in pixels)
    return pixels
