SetFactory("OpenCASCADE");

If (!Exists(MeshTarget))
  MeshTarget = 0.100;
EndIf

length = 1.0;
width = 0.1;
height = 0.1;
// OpenCASCADE entity bounds include its modeling tolerance. The selector box
// must enclose that tolerance or Gmsh can create named-but-empty groups.
epsilon = 1.0e-6;

Box(1) = {0.0, 0.0, 0.0, length, width, height};

fixed() = Surface In BoundingBox {
  -epsilon, -epsilon, -epsilon,
   epsilon, width + epsilon, height + epsilon
};
loaded() = Surface In BoundingBox {
  length - epsilon, -epsilon, -epsilon,
  length + epsilon, width + epsilon, height + epsilon
};
y_min() = Surface In BoundingBox {
  -epsilon, -epsilon, -epsilon,
  length + epsilon, epsilon, height + epsilon
};
y_max() = Surface In BoundingBox {
  -epsilon, width - epsilon, -epsilon,
  length + epsilon, width + epsilon, height + epsilon
};
z_min() = Surface In BoundingBox {
  -epsilon, -epsilon, -epsilon,
  length + epsilon, width + epsilon, epsilon
};
z_max() = Surface In BoundingBox {
  -epsilon, -epsilon, height - epsilon,
  length + epsilon, width + epsilon, height + epsilon
};

Physical Surface("FIXED") = {fixed()};
Physical Surface("LOADED") = {loaded()};
Physical Surface("SIDES") = {y_min(), y_max(), z_min(), z_max()};
Physical Volume("BAR") = {1};

Mesh.ElementOrder = 1;
Mesh.MeshSizeMin = MeshTarget;
Mesh.MeshSizeMax = MeshTarget;
Mesh.Algorithm3D = 1;
// Restrict INP output to the boundary triangles and volume tetrahedra needed by
// the reviewed-selection/compiler seam; omit point and curve element sections.
Mesh.SaveGroupsOfElements = -1100;
