# Included models

| Directory | Contents | Attribution |
| --- | --- | --- |
| `blender_sphere` | Smooth UV sphere exported from Blender 3.6.23; 224 triangles and a checker texture | Project fixture |
| `cube` | Six quads, face normals, UVs and two materials; triangulates to 12 triangles | Project fixture |
| `textured_triangle` | Independent/negative OBJ indices, nested MTL and an image filename containing spaces | Project fixture |
| `gallery` | Diffuse sphere, dielectric cube, reflective ellipsoid and an emissive mesh | Assembled from project fixtures |
| `vodka` | Textured bottle exported from Blender; 2,270 triangles | Marcin.Kwiatkowski, CC BY 4.0; [full attribution](vodka/ATTRIBUTION.md) |

All models load without Blender installed. The sphere can be regenerated with
Blender 3.6 using `blender -b --python examples/export_blender_fixture.py` from the
project root. The other fixtures are included directly.

## Material gallery construction

`gallery/gallery.obj` contains 472 triangles assembled from the included sphere
and cube fixtures. It is a project-created scene, not an external model download.

| Object | Source mesh | Scale | Translation | Material |
| --- | --- | --- | --- | --- |
| Paint | Blender sphere | `(1, 1, 1)` | `(-2.5, 0, 0)` | Green diffuse |
| Glass | Cube | `(0.8, 1, 0.8)` | `(0, 0, 0)` | Dielectric, IOR 1.5 |
| Mirror | Blender sphere | `(1, 0.7, 1)` | `(2.5, 0, 0)` | Reflective metal |
| Lamp | Cube | `(1.5, 0.2, 0.8)` | `(0, 3, 0)` | Emissive |

The metal object is intentionally flattened along Y. The generator applies inverse
scaling to normals as well as scaling positions. The preview scene adds the floor
and a sphere light at runtime; they are not part of the OBJ's triangle count.

Reproduce the exact included OBJ and MTL into an output directory:

```sh
python3 examples/generate_gallery.py
```
