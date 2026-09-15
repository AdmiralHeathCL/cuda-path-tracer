"""Compare actual OpenGL screenshots against matching command-line renders."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
viewer, renderer = [str(Path(p).resolve()) for p in sys.argv[1:3]]
backend = "cuda"
from image_io import read_ppm

with tempfile.TemporaryDirectory(prefix="ray-viewer-") as temporary:
    directory = Path(temporary)
    for mode in ["shaded", "normals", "material-id"]:
        screenshot = directory / (mode + ".ppm")
        command = [viewer, "--hidden", "--frames", "2", "--width", "19", "--height", "13",
                   "--batch-samples", "2", "--seed", "7", "--backend", backend, "--debug", mode,
                   "--screenshot", str(screenshot)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode == 77:
            print(result.stderr); sys.exit(77)
        assert result.returncode == 0, result.stderr
        assert "Rendered frames: 2, samples/pixel: 4, framebuffer: 19x13" in result.stdout, result.stdout
        expected = directory / "cli.ppm"
        with expected.open("wb") as out:
            subprocess.run([renderer, "--obj", str(root / "assets/models/blender_sphere/sphere.obj"),
                            "--width", "19", "--height", "13", "--samples", "4", "--seed", "7",
                            "--backend", backend, "--debug", mode, "--lighting", "mis"], stdout=out, stderr=subprocess.PIPE, check=True, timeout=60)
        actual = read_ppm(screenshot,19,13); reference = read_ppm(expected,19,13)
        assert any(actual)
        assert max(abs(a-b) for a,b in zip(actual,reference)) <= 1, mode
        print(backend, mode, "OpenGL screenshot matches CLI within 1 byte")
    if backend == "cuda":
        copied = directory / "copy.ppm"
        args = [viewer, "--hidden", "--frames", "1", "--backend", "cuda", "--width", "19", "--height", "13"]
        copy = subprocess.run(args+["--display", "copy", "--screenshot", str(copied)], capture_output=True, text=True, timeout=60)
        assert copy.returncode == 0, copy.stderr
        shared = directory / "interop.ppm"
        interop = subprocess.run(args+["--display", "interop", "--screenshot", str(shared)], capture_output=True, text=True, timeout=60)
        if interop.returncode == 0:
            assert max(abs(a-b) for a,b in zip(read_ppm(copied,19,13), read_ppm(shared,19,13))) <= 1
        else:
            assert interop.returncode == 1 and "interop unavailable" in interop.stderr, interop.stderr
            assert not shared.exists()
            print("Forced interop reports unavailable; explicit CUDA copy display passed")
    for flags in [["--hidden"],["--frames","0"],["--backend","reference"],["--samples","2"],
                  ["--lighting","wrong"],["--display","wrong"],
                  ["--backend","portable"], ["--backend","cuda-brute"]]:
        result = subprocess.run([viewer]+flags, capture_output=True, timeout=10)
        assert result.returncode == 1
