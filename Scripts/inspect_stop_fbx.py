import math
import os
import sys

import bpy


STOP_NAMES = (
    "Stop_Walk_L.fbx",
    "Stop_Walk_R.fbx",
    "Stop_Run_L.fbx",
    "Stop_Run_R.fbx",
    "Stop_Sprint_L.fbx",
    "Stop_Sprint_R.fbx",
)


def vec_text(value):
    return f"({value.x:.4f},{value.y:.4f},{value.z:.4f})"


def find_pose_bone(armature, suffix):
    suffix = suffix.lower()
    for bone in armature.pose.bones:
        normalized = bone.name.replace("_", "").lower()
        if normalized.endswith(suffix):
            return bone
    raise RuntimeError(f"Missing bone suffix '{suffix}' in {armature.name}")


def find_pose_bone_exact(armature, name):
    normalized_name = name.replace("_", "").lower()
    for bone in armature.pose.bones:
        normalized = bone.name.replace("_", "").lower()
        if normalized == normalized_name:
            return bone
    raise RuntimeError(f"Missing bone '{name}' in {armature.name}")


def bone_world_location(armature, bone):
    return (armature.matrix_world @ bone.matrix).translation.copy()


def inspect_fbx(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path)
    armatures = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
    if len(armatures) != 1:
        raise RuntimeError(f"Expected one armature in {path}; found {len(armatures)}")

    armature = armatures[0]
    action = armature.animation_data.action if armature.animation_data else None
    if action is None:
        raise RuntimeError(f"No action found in {path}")

    root = find_pose_bone(armature, "root")
    body = find_pose_bone_exact(armature, "Bip001")
    left = find_pose_bone(armature, "lfoot")
    right = find_pose_bone(armature, "rfoot")
    start = int(math.floor(action.frame_range[0]))
    end = int(math.ceil(action.frame_range[1]))
    sample_frames = sorted({
        start,
        start + 1,
        int(round(start + (end - start) * 0.10)),
        int(round(start + (end - start) * 0.25)),
        int(round(start + (end - start) * 0.50)),
        int(round(start + (end - start) * 0.75)),
        end,
    })

    bpy.context.scene.frame_set(start)
    root_start = bone_world_location(armature, root)
    bpy.context.scene.frame_set(end)
    root_end = bone_world_location(armature, root)
    root_delta = root_end - root_start
    planar_axes = sorted(range(3), key=lambda axis: abs(root_delta[axis]), reverse=True)
    travel_axis = planar_axes[0]
    travel_sign = 1.0 if root_delta[travel_axis] >= 0.0 else -1.0

    body_locations = []
    root_locations = []
    root_rotations = []
    for frame in range(start, end + 1):
        bpy.context.scene.frame_set(frame)
        body_locations.append(bone_world_location(armature, body))
        root_locations.append(bone_world_location(armature, root))
        root_rotations.append((armature.matrix_world @ root.matrix).to_euler("XYZ"))

    body_net = body_locations[-1] - body_locations[0]
    body_forward = body_net.copy()
    body_forward.z = 0.0
    body_forward.normalize()
    body_lateral = body_forward.cross(type(body_forward)((0.0, 0.0, 1.0)))

    def trajectory_metrics(locations):
        origin = locations[0]
        forwards = [(location - origin).dot(body_forward) for location in locations]
        laterals = [(location - origin).dot(body_lateral) for location in locations]
        return (
            forwards[-1],
            max(forwards),
            max(forwards) - forwards[-1],
            max(
                (previous - current for previous, current in zip(forwards, forwards[1:])),
                default=0.0,
            ),
            max(abs(value) for value in laterals),
        )

    body_final, body_peak, body_rebound, body_backstep, body_lateral_max = trajectory_metrics(body_locations)
    root_final, root_peak, root_rebound, root_backstep, root_lateral_max = trajectory_metrics(root_locations)
    first_rotation = root_rotations[0]
    max_root_rotation_delta = max(
        math.degrees(max(abs(value - first) for value, first in zip(rotation, first_rotation)))
        for rotation in root_rotations
    )

    print(
        "STOP_FBX_TRACK "
        f"name={os.path.basename(path)} "
        f"body_final={body_final:.4f} body_peak={body_peak:.4f} "
        f"body_rebound={body_rebound:.4f} body_backstep={body_backstep:.4f} "
        f"body_lateral={body_lateral_max:.4f} "
        f"root_final={root_final:.4f} root_peak={root_peak:.4f} "
        f"root_rebound={root_rebound:.4f} root_backstep={root_backstep:.4f} "
        f"root_lateral={root_lateral_max:.4f} "
        f"root_rotation_delta_deg={max_root_rotation_delta:.4f}"
    )

    print(
        "STOP_FBX "
        f"name={os.path.basename(path)} frames={start}:{end} "
        f"root_start={vec_text(root_start)} root_end={vec_text(root_end)} "
        f"root_delta={vec_text(root_delta)} travel_axis={travel_axis} sign={travel_sign:+.0f}"
    )
    for frame in sample_frames:
        bpy.context.scene.frame_set(frame)
        left_location = bone_world_location(armature, left)
        right_location = bone_world_location(armature, right)
        left_projection = (left_location[travel_axis] - root_start[travel_axis]) * travel_sign
        right_projection = (right_location[travel_axis] - root_start[travel_axis]) * travel_sign
        leading = "L" if left_projection > right_projection else "R"
        print(
            "STOP_FBX_FRAME "
            f"name={os.path.basename(path)} frame={frame} "
            f"left={vec_text(left_location)} right={vec_text(right_location)} "
            f"left_travel={left_projection:.4f} right_travel={right_projection:.4f} "
            f"leading={leading}"
        )


def main():
    separator = sys.argv.index("--") if "--" in sys.argv else len(sys.argv) - 1
    source_root = os.path.abspath(sys.argv[separator + 1])
    for name in STOP_NAMES:
        inspect_fbx(os.path.join(source_root, name))


main()
