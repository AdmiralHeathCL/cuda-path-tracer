"""Exercise CUDA CLI defaults, batch continuity, mesh materials and diagnostics."""
from pathlib import Path
import subprocess
import sys
import tempfile
from image_io import read_ppm

renderer = str(Path(sys.argv[1]).resolve())
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    def render(extra):
        result = subprocess.run([renderer, '--width', '19', '--height', '13', '--samples', '8'] + extra,
                                capture_output=True, timeout=90, cwd=directory)
        if result.returncode == 77:
            print(result.stderr.decode()); sys.exit(77)
        assert result.returncode == 0, result.stderr.decode()
        image = directory / 'image.ppm'; image.write_bytes(result.stdout)
        pixels = read_ppm(image, 19, 13)
        assert any(pixels), 'unexpected black frame'
        return result.stdout
    for name in ['blender_sphere/sphere', 'cube/cube', 'textured_triangle/triangle', 'gallery/gallery', 'vodka/vodka']:
        asset = ['--obj', str(root / 'assets/models' / (name + '.obj'))]
        assert render(asset + ['--batch-samples', '3']) == render(asset + ['--batch-samples', '8']), name
    for mode in ['normals', 'triangle-id', 'material-id', 'bvh', 'intersections']:
        render(['--debug', mode])
    render(['--obj', str(root/'assets/models/vodka/vodka.obj'), '--obj-rotate', '0', '90', '0'])
    for args in [['--backend', b] for b in ['reference', 'flat', 'portable', 'portable-brute', 'cuda-brute']]+[
        ['--samples','0'], ['--width','0'], ['--obj'], ['--obj', '/missing.obj'],
        ['--debug','invalid'], ['--lighting','invalid'], ['--cuda-block','3','3']]:
        result = subprocess.run([renderer]+args, capture_output=True, timeout=15)
        assert result.returncode == 1 and not result.stdout, (args, result.stderr)
print('CUDA render, batching, imported assets, debug modes and argument checks passed')
