# Third-party sources

- `stb_image.h`: existing vendored stb_image v2.30. License is embedded in the header.
  `stb_image.cpp` compiles its implementation exactly once.
- `tinyobjloader/tiny_obj_loader.h`: tinyobjloader v2.0.0rc13, pinned to commit
  `2945a967c5303b2c8c14174117c45f3302591150` from
  <https://github.com/tinyobjloader/tinyobjloader>.
  The unmodified upstream header and MIT license are included. Its implementation
  is compiled once in `tinyobjloader/implementation.cpp`.

Both dependencies are local. Builds require no network access.

The original ray-tracing foundation comes from the
[Ray Tracing in One Weekend series](https://github.com/RayTracing/raytracing.github.io),
released under CC0 1.0. The upstream license is included in `raytracing-CC0.txt`.
Model and texture licenses are described in [assets/models/README.md](../assets/models/README.md).
