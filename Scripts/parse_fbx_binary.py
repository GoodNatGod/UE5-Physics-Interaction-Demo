import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else r"D:\BaiduNetdiskDownload\艾斯3d建模-鸣潮 通用类型武器-附赠品\刀剑类型-01\R2Sword001.fbx"
print(f"Parsing: {path}\n")

with open(path, 'rb') as f:
    data = f.read()

# Skip binary header: 27 bytes
offset = 27
nodes_info = []

while offset < len(data) - 13:
    end_offset = struct.unpack_from('<I', data, offset)[0]
    num_props = struct.unpack_from('<I', data, offset+4)[0]
    prop_list_len = struct.unpack_from('<I', data, offset+8)[0]
    name_len = data[offset+12]
    
    if name_len > 100 or name_len < 1:
        break
    
    name = data[offset+13:offset+13+name_len].decode('ascii', errors='replace')
    
    # Read property list
    prop_start = offset + 13 + name_len
    prop_end = prop_start + prop_list_len
    props_raw = data[prop_start:prop_end]
    
    # Extract string properties (type code 'S')
    strings = []
    p = 0
    while p < len(props_raw) - 1:
        if props_raw[p:p+1] == b'S' and p + 4 < len(props_raw):
            slen = struct.unpack_from('<I', props_raw, p+1)[0]
            if 0 < slen < 200 and p + 5 + slen <= len(props_raw):
                sval = props_raw[p+5:p+5+slen].decode('ascii', errors='replace')
                strings.append(sval)
        p += 1
    
    # Extract int properties (type code 'I')
    ints = []
    p = 0
    while p < len(props_raw) - 4:
        if props_raw[p:p+1] == b'I' and p + 5 <= len(props_raw):
            ival = struct.unpack_from('<i', props_raw, p+1)[0]
            ints.append(ival)
        p += 1
    
    nodes_info.append({
        'name': name,
        'end': end_offset,
        'strings': strings,
        'ints': ints,
    })
    
    # Move to next
    next_offset = offset + 13 + name_len + prop_list_len
    if end_offset == 0:
        offset = next_offset
    else:
        offset = end_offset
    
    if offset <= 0 or offset >= len(data):
        break

# === Parse Results ===
models = []
for node in nodes_info:
    name = node['name']
    strings = node['strings']
    ints = node['ints']
    
    if name == 'Model' and strings:
        model_type = strings[1] if len(strings) > 1 else '?'
        models.append(f"  {strings[0]} (type: {model_type})")
    elif name in ('Vertices', 'PolygonVertexIndex'):
        if ints:
            count = ints[0]
            print(f"  {name}: {count}")
    elif name == 'Deformer' and strings:
        d_type = strings[1] if len(strings) > 1 else '?'
        print(f"  Deformer: {strings[0]} (type: {d_type})")
    elif name == 'PoseNode' and strings:
        print(f"  PoseNode (bone): {strings[0]}")
    elif name == 'Material' and strings:
        mat_name = strings[0] if strings else '?'
        print(f"  Material: {mat_name}")
    elif name == 'Texture' and strings:
        tex_name = strings[0] if strings else '?'
        tex_file = strings[2] if len(strings) > 2 else '?'
        if tex_file != '?':
            print(f"    Texture: {tex_name} -> {tex_file}")
    elif name == 'NodeAttribute' and strings:
        print(f"    NodeAttr: {strings[0]}")

if models:
    print("\n=== Model Hierarchy ===")
    for m in models:
        print(m)

# Check if this was a skinned mesh
has_skin = any(n['name'] == 'Deformer' and len(n['strings']) > 1 and n['strings'][1] == 'Skin' for n in nodes_info)
has_cluster = any(n['name'] == 'Cluster' for n in nodes_info)
has_pose = any(n['name'] == 'PoseNode' for n in nodes_info)
total_verts = sum(n['ints'][0] for n in nodes_info if n['name'] == 'Vertices' and n['ints'])
total_poly_idx = sum(n['ints'][0] for n in nodes_info if n['name'] == 'PolygonVertexIndex' and n['ints'])

print(f"\n=== SUMMARY ===")
print(f"  Vertices: ~{total_verts}")
print(f"  Polygon indices: {total_poly_idx}")
print(f"  Est. triangles: ~{total_poly_idx // 3}")
print(f"  Materials: {sum(1 for n in nodes_info if n['name'] == 'Material')}")
print(f"  Textures: {sum(1 for n in nodes_info if n['name'] == 'Texture')}")
print(f"  Has skin deform: {has_skin}")
print(f"  Has bone clusters: {has_cluster}")
print(f"  Has pose/bones: {has_pose}")

if has_skin:
    print("\n  [INFO] This is a SKINNED mesh (SkeletalMesh in UE).")
    print("  For a weapon, import as SkeletalMesh and attach to hand socket.")
else:
    print("\n  [INFO] This is a STATIC mesh. Import as StaticMesh.")
print()
