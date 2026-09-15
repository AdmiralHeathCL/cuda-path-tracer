"""Capture README images from the CUDA viewer's OpenGL framebuffer (requires Pillow)."""
import argparse
from pathlib import Path
import subprocess
from PIL import Image

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--viewer', type=Path, default=root/'build/ray_tracer_viewer')
parser.add_argument('--output', type=Path, default=root/'output/gallery')
parser.add_argument('--samples', type=int, default=1024)
args = parser.parse_args()
if args.samples < 16 or args.samples % 16:
    parser.error('--samples must be a positive multiple of 16')
args.output.mkdir(parents=True, exist_ok=True)
views = [('vodka-front', 'vodka/vodka.obj', (0,0,0), 600,700,args.samples),
         ('vodka-side', 'vodka/vodka.obj', (0,75,0), 600,700,args.samples),
         ('vodka-back', 'vodka/vodka.obj', (0,160,0), 600,700,args.samples),
         ('viewer-gallery-validated', 'gallery/gallery.obj', (0,0,0), 960,600,256),
         ('showcase-normals', 'gallery/gallery.obj', (0,0,0), 480,300,16),
         ('showcase-bvh', 'gallery/gallery.obj', (0,0,0), 480,300,16)]
for name, asset, rotation, width, height, samples in views:
    ppm = args.output/(name+'.ppm')
    command = [str(args.viewer.resolve()), '--obj', str(root/'assets/models'/asset),
               '--obj-rotate', *map(str,rotation), '--width',str(width),'--height',str(height),
               '--batch-samples','16','--frames',str(samples//16),'--hidden','--seed','7',
               '--display','copy','--screenshot',str(ppm.resolve())]
    if name == 'showcase-normals': command += ['--debug','normals']
    if name == 'showcase-bvh': command += ['--debug','bvh']
    print('Rendering', name, flush=True)
    subprocess.run(command, check=True)
    with Image.open(ppm) as image: image.save(args.output/(name+'.png'))
print('Saved actual viewer captures to',args.output)
