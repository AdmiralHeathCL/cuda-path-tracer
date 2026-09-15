"""Render CUDA lighting and sample-convergence images for the README; requires Pillow."""
import argparse
from pathlib import Path
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', type=Path, default=ROOT/'build/ray_tracer')
    parser.add_argument('--obj', type=Path, default=ROOT/'assets/models/gallery/gallery.obj')
    parser.add_argument('--output', type=Path, default=ROOT/'output/noise')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for lighting, samples in [('classic',8), ('mis',8), ('classic',32), ('mis',32), ('mis',1024)]:
        stem = f'noise-{lighting}-{samples}'
        ppm = args.output/(stem+'.ppm')
        command = [str(args.renderer.resolve()), '--obj',str(args.obj.resolve()),
                   '--width','384','--height','240','--samples',str(samples),
                   '--seed','7','--lighting',lighting]
        print(f'Rendering {lighting}, {samples} samples/pixel', flush=True)
        with ppm.open('wb') as image, (args.output/(stem+'.log')).open('wb') as log:
            subprocess.run(command, stdout=image, stderr=log, check=True, timeout=600)
        with Image.open(ppm) as image:
            if image.size != (384,240):
                raise RuntimeError(f'Unexpected image dimensions: {image.size}')
            image.save(args.output/(stem+'.png'))
    print('Saved CUDA render images to',args.output)


if __name__ == '__main__':
    main()
