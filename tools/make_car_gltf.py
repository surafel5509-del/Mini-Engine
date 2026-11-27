#!/usr/bin/env python3
"""Generates samples/CarScene/models/car.gltf, car.glb, and car.bin.

A simple stylized car built from boxes/cylinders with PBR materials:
body (teal), cabin (dark glass), 4 wheels, 2 headlights.
Run: python3 tools/make_car_gltf.py
"""
import json, math, struct, os

def box(cx, cy, cz, sx, sy, sz):
    hx, hy, hz = sx/2, sy/2, sz/2
    faces = [
        ([-hx,-hy, hz],[ hx,-hy, hz],[ hx, hy, hz],[-hx, hy, hz], (0,0,1)),
        ([ hx,-hy,-hz],[-hx,-hy,-hz],[-hx, hy,-hz],[ hx, hy,-hz], (0,0,-1)),
        ([ hx,-hy, hz],[ hx,-hy,-hz],[ hx, hy,-hz],[ hx, hy, hz], (1,0,0)),
        ([-hx,-hy,-hz],[-hx,-hy, hz],[-hx, hy, hz],[-hx, hy,-hz], (-1,0,0)),
        ([-hx, hy, hz],[ hx, hy, hz],[ hx, hy,-hz],[-hx, hy,-hz], (0,1,0)),
        ([-hx,-hy,-hz],[ hx,-hy,-hz],[ hx,-hy, hz],[-hx,-hy, hz], (0,-1,0)),
    ]
    pos, nrm, uv, idx = [], [], [], []
    for v0, v1, v2, v3, n in faces:
        base = len(pos)//3
        for v, u in [(v0,(0,0)),(v1,(1,0)),(v2,(1,1)),(v3,(0,1))]:
            pos += [cx+v[0], cy+v[1], cz+v[2]]
            nrm += list(n)
            uv += list(u)
        idx += [base, base+1, base+2, base, base+2, base+3]
    return pos, nrm, uv, idx

def cylinder_x(cx, cy, cz, r, h, sectors=20):
    """Cylinder with axis along X (wheel)."""
    pos, nrm, uv, idx = [], [], [], []
    hx = h/2
    for j in range(sectors+1):
        t = j/sectors * 2*math.pi
        y, z = r*math.cos(t), r*math.sin(t)
        for x, u in [(-hx, 0.0), (hx, 1.0)]:
            pos += [cx+x, cy+y, cz+z]
            nrm += [0.0, math.cos(t), math.sin(t)]
            uv += [j/sectors, u]
    for j in range(sectors):
        a = j*2
        idx += [a, a+2, a+1, a+1, a+2, a+3]
    # caps
    for side, x in [(-1, cx-hx), (1, cx+hx)]:
        center = len(pos)//3
        pos += [x, cy, cz]; nrm += [float(side), 0, 0]; uv += [0.5, 0.5]
        ring = len(pos)//3
        for j in range(sectors+1):
            t = j/sectors * 2*math.pi
            pos += [x, cy + r*math.cos(t), cz + r*math.sin(t)]
            nrm += [float(side), 0, 0]
            uv += [0.5 + 0.5*math.cos(t), 0.5 + 0.5*math.sin(t)]
        for j in range(sectors):
            if side < 0: idx += [center, ring+j+1, ring+j]
            else: idx += [center, ring+j, ring+j+1]
    return pos, nrm, uv, idx

parts = [
    ("Body",       box(0, 0.55, 0, 4.2, 0.7, 1.8),          (0.05, 0.65, 0.60, 1.0), 0.6, 0.35),
    ("Cabin",      box(-0.2, 1.15, 0, 2.2, 0.55, 1.6),      (0.08, 0.08, 0.10, 1.0), 0.2, 0.15),
    ("Wheel_FL",   cylinder_x(-1.4, 0.38,  0.85, 0.38, 0.3), (0.10, 0.10, 0.10, 1.0), 0.1, 0.8),
    ("Wheel_FR",   cylinder_x(-1.4, 0.38, -0.85, 0.38, 0.3), (0.10, 0.10, 0.10, 1.0), 0.1, 0.8),
    ("Wheel_RL",   cylinder_x( 1.4, 0.38,  0.85, 0.38, 0.3), (0.10, 0.10, 0.10, 1.0), 0.1, 0.8),
    ("Wheel_RR",   cylinder_x( 1.4, 0.38, -0.85, 0.38, 0.3), (0.10, 0.10, 0.10, 1.0), 0.1, 0.8),
    ("Headlight_L",box(2.11, 0.62,  0.55, 0.06, 0.18, 0.4), (1.00, 0.95, 0.70, 1.0), 0.0, 0.3),
    ("Headlight_R",box(2.11, 0.62, -0.55, 0.06, 0.18, 0.4), (1.00, 0.95, 0.70, 1.0), 0.0, 0.3),
]

bin_data = bytearray()
buffer_views, accessors, meshes, materials, nodes = [], [], [], [], []

def add_buffer_view(data_bytes):
    off = len(bin_data)
    bin_data.extend(data_bytes)
    # pad to 4-byte alignment
    while len(bin_data) % 4: bin_data.append(0)
    buffer_views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data_bytes)})
    return len(buffer_views) - 1

def add_accessor(view, comp_type, count, typ, minv=None, maxv=None):
    a = {"bufferView": view, "componentType": comp_type, "count": count, "type": typ}
    if minv is not None: a["min"] = minv; a["max"] = maxv
    accessors.append(a)
    return len(accessors) - 1

for i, (name, (pos, nrm, uv, idx), color, metallic, roughness) in enumerate(parts):
    pos_b = struct.pack(f"<{len(pos)}f", *pos)
    nrm_b = struct.pack(f"<{len(nrm)}f", *nrm)
    uv_b  = struct.pack(f"<{len(uv)}f", *uv)
    idx_b = struct.pack(f"<{len(idx)}I", *idx)
    xs, ys, zs = pos[0::3], pos[1::3], pos[2::3]
    acc_pos = add_accessor(add_buffer_view(pos_b), 5126, len(pos)//3, "VEC3",
                           [min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)])
    acc_nrm = add_accessor(add_buffer_view(nrm_b), 5126, len(nrm)//3, "VEC3")
    acc_uv  = add_accessor(add_buffer_view(uv_b), 5126, len(uv)//2, "VEC2")
    acc_idx = add_accessor(add_buffer_view(idx_b), 5125, len(idx), "SCALAR")
    materials.append({"name": name + "Mat", "pbrMetallicRoughness": {
        "baseColorFactor": list(color), "metallicFactor": metallic, "roughnessFactor": roughness}})
    meshes.append({"name": name, "primitives": [{
        "attributes": {"POSITION": acc_pos, "NORMAL": acc_nrm, "TEXCOORD_0": acc_uv},
        "indices": acc_idx, "material": i}]})
    nodes.append({"name": name, "mesh": i})

gltf = {
    "asset": {"version": "2.0", "generator": "Mini-Engine make_car_gltf.py"},
    "scene": 0,
    "scenes": [{"name": "Car", "nodes": list(range(len(nodes)))}],
    "nodes": nodes,
    "meshes": meshes,
    "materials": materials,
    "buffers": [{"uri": "car.bin", "byteLength": len(bin_data)}],
    "bufferViews": buffer_views,
    "accessors": accessors,
}

out_dir = os.path.join(os.path.dirname(__file__), "..", "samples", "CarScene", "models")
os.makedirs(out_dir, exist_ok=True)
with open(os.path.join(out_dir, "car.bin"), "wb") as f: f.write(bytes(bin_data))
with open(os.path.join(out_dir, "car.gltf"), "w") as f: json.dump(gltf, f, indent=2)

# Emit the same asset as a GLB 2.0 container to exercise and demonstrate the
# single-file delivery path. JSON and BIN chunks are padded to four-byte sizes.
glb_json = dict(gltf)
glb_json["buffers"] = [{"byteLength": len(bin_data)}]
json_chunk = json.dumps(glb_json, separators=(",", ":")).encode("utf-8")
json_chunk += b" " * ((-len(json_chunk)) % 4)
bin_chunk = bytes(bin_data)
bin_chunk += b"\0" * ((-len(bin_chunk)) % 4)
glb_length = 12 + 8 + len(json_chunk) + 8 + len(bin_chunk)
glb = (struct.pack("<4sII", b"glTF", 2, glb_length) +
       struct.pack("<II", len(json_chunk), 0x4E4F534A) + json_chunk +
       struct.pack("<II", len(bin_chunk), 0x004E4942) + bin_chunk)
with open(os.path.join(out_dir, "car.glb"), "wb") as f: f.write(glb)
print(f"Wrote car.gltf and car.glb ({len(meshes)} meshes, {len(bin_data)} byte bin)")
