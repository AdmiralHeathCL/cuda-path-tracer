"""Run with Blender 3.6: blender -b --python examples/export_blender_fixture.py."""

from pathlib import Path
import bpy

directory = Path(__file__).resolve().parents[1] / "assets/models/blender_sphere"
directory.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=1)
sphere = bpy.context.object
sphere.name = "TexturedSphere"
for face in sphere.data.polygons:
    face.use_smooth = True

material = bpy.data.materials.new("CheckerPaint")
material.use_nodes = True
shader = material.node_tree.nodes.get("Principled BSDF")
shader.inputs["Base Color"].default_value = (1, 1, 1, 1)
shader.inputs["Specular"].default_value = 0
texture = material.node_tree.nodes.new("ShaderNodeTexImage")
texture.image = bpy.data.images.load(str(directory / "checker.png"))
material.node_tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])
sphere.data.materials.append(material)

bpy.ops.export_scene.obj(
    filepath=str(directory / "sphere.obj"),
    use_selection=True,
    use_normals=True,
    use_uvs=True,
    use_materials=True,
    use_triangles=True,
    axis_forward="-Z",
    axis_up="Y",
    path_mode="RELATIVE",
)
