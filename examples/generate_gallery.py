"""Assemble the material gallery from the included sphere and cube OBJ fixtures."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    sphere = (ROOT/'assets/models/blender_sphere/sphere.obj').read_text().splitlines()
    cube = (ROOT/'assets/models/cube/cube.obj').read_text().splitlines()
    # Positions are scaled then translated; normals use the inverse scale.
    objects = [('paint',sphere,(-2.5,0,0),(1,1,1)),
               ('glass',cube,(0,0,0),(0.8,1,0.8)),
               ('mirror',sphere,(2.5,0,0),(1,0.7,1)),
               ('lamp',cube,(0,3,0),(1.5,0.2,0.8))]
    counts = [0,0,0]
    with (directory/'gallery.obj').open('w') as out:
        out.write('mtllib gallery.mtl\n')
        for name, lines, offset, scale in objects:
            out.write(f'o {name}\nusemtl {name}\n')
            local = [0,0,0]
            for line in lines:
                parts = line.split()
                if not parts:
                    continue
                if parts[0] == 'v':
                    point = [float(parts[i+1])*scale[i]+offset[i] for i in range(3)]
                    out.write('v '+' '.join(map(str,point))+'\n')
                    local[0] += 1
                elif parts[0] == 'vt':
                    out.write(line+'\n')
                    local[1] += 1
                elif parts[0] == 'vn':
                    normal = [float(parts[i+1])/scale[i] for i in range(3)]
                    out.write('vn '+' '.join(map(str,normal))+'\n')
                    local[2] += 1
                elif parts[0] == 'f':
                    corners = ['/'.join(str(int(index)+counts[i]) for i,index in enumerate(corner.split('/')))
                               for corner in parts[1:]]
                    out.write('f '+' '.join(corners)+'\n')
            counts = [a+b for a,b in zip(counts,local)]
    (directory/'gallery.mtl').write_text(
        'newmtl paint\nKd 0.3 0.7 0.2\nillum 2\n'
        'newmtl glass\nNi 1.5\nillum 7\n'
        'newmtl mirror\nKs 0.8 0.7 0.6\nillum 5\n'
        'newmtl lamp\nKe 4 3 2\nillum 2\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'output/generated-gallery')
    args = parser.parse_args()
    generate(args.output)
    print('Saved material gallery to',args.output)
