import re, sys

path = sys.argv[1] if len(sys.argv) > 1 else r"D:\BaiduNetdiskDownload\艾斯3d建模-鸣潮 通用类型武器-附赠品\刀剑类型-01\R2Sword001.fbx"
print(f"Analyzing: {path}\n")

with open(path, 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# === Model/Nodes ===
models = re.findall(r'Model:\s*(\d+),\s*"Model::([^"]+)"', content)
print("=== Model/Nodes ===")
for mid, name in models:
    node_attrs = []
    idx = content.find(f'Model: {mid},"Model::{name}"')
    if idx > 0:
        chunk = content[idx:idx+600]
        if 'Skeleton' in chunk:
            node_attrs.append('SKELETON')
        if 'Mesh' in chunk:
            node_attrs.append('MESH')
        if 'Null' in chunk:
            node_attrs.append('NULL')
        if 'LimbNode' in chunk:
            node_attrs.append('LIMB')
    attrs_str = ', '.join(node_attrs) if node_attrs else 'ROOT'
    print(f"  [{mid}] {name} ({attrs_str})")

# === Vertices ===
verts = re.findall(r'Vertices:\s*\*(\d+)\s*\{', content)
print(f"\n=== Vertices: {len(verts)} group(s) ===")
total_verts = 0
for i, v in enumerate(verts):
    idx = content.find(f"Vertices: *{v}")
    if idx > 0:
        chunk = content[idx:idx+500]
        nums = re.findall(r'([\-0-9\.eE+]+)', chunk)
        actual = (len(nums) - 1) // 3
        total_verts += actual
        print(f"  Group {i}: declared={v}, actual~{actual}")
print(f"  Total estimated vertices: {total_verts}")

# === Polygon indices ===
poly = re.findall(r'PolygonVertexIndex:\s*\*(\d+)\s*\{', content)
if poly:
    total_poly = sum(int(p) for p in poly)
    print(f"\n=== Polygon Indices ===")
    print(f"  Groups: {len(poly)}, total indices: {total_poly}")

# === Materials and textures ===
mats = re.findall(r'Material:\s*(\d+),\s*"([^"]+)"', content)
print(f"\n=== Materials ({len(mats)}) ===")
for mid, mat_name in mats:
    # Find connected texture near this material
    mat_idx = content.find(f'Material: {mid},"{mat_name}"')
    tex_section = content[mat_idx:mat_idx+3000] if mat_idx > 0 else ""
    tex_refs = re.findall(r'Texture\S+\s*,\s*"Texture::([^"]+)"', tex_section)
    print(f"  - {mat_name}")
    for t in tex_refs:
        print(f"      texture: {t}")

# === Deformers ===
deformers = re.findall(r'Deformer:\s*(\d+),\s*"([^"]+)"', content)
print(f"\n=== Deformers ({len(deformers)}) ===")
for did, dname in deformers:
    d_idx = content.find(f'Deformer: {did},"{dname}"')
    d_chunk = content[d_idx:d_idx+400] if d_idx > 0 else ""
    d_type_m = re.search(r'"DeformerType"\s*,\s*"([^"]+)"', d_chunk)
    d_type = d_type_m.group(1) if d_type_m else "?"
    print(f"  - {dname} (type: {d_type})")

# === BindPose bones ===
poses = re.findall(r'PoseNode:\s*\d+,\s*"([^"]+)"', content)
print(f"\n=== BindPose Bones ({len(poses)}) ===")
for p in poses[:15]:
    print(f"  - {p}")
if len(poses) > 15:
    print(f"  ... and {len(poses)-15} more")

# Summary
print("\n=== QUICK SUMMARY ===")
print(f"  Nodes: {len(models)}")
print(f"  Meshes: {len(verts)} group(s)")
print(f"  Estimated triangles: {total_poly//3 if poly else '?'}")
print(f"  Materials: {len(mats)}")
print(f"  Skeleton bones: {len(poses)}")
print(f"  Skinned: {'YES' if deformers else 'NO'}")
if deformers:
    print("  WARNING: This FBX has skin deformers! May need special handling for UE import.")
