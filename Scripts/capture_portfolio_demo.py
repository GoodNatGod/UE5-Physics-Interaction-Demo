import math
import os
import re
import time
import traceback

import unreal


BOX_TAG = "PhysicsWorldP0Box"
TRAINING_ENEMY_TAG = "RoverP0TrainingEnemy"
EXPECTED_PAWN = "/Script/RoverReplica.RoverCharacter"
CAPTURE_WIDTH = 1280
CAPTURE_HEIGHT = 720
CAPTURE_DURATION_SECONDS = 8.5
CAPTURE_SAMPLE_FPS = 12.0
START_TIMEOUT_SECONDS = 45.0
PHASE_TIMEOUT_SECONDS = 20.0
STOP_TIMEOUT_SECONDS = 15.0
SUCCESS_MARKER = "PORTFOLIO_CAPTURE_OK"
FAILURE_MARKER = "PORTFOLIO_CAPTURE_FAIL"


def command_line_value(name):
    match = re.search(
        rf'(?:^|\s)-{re.escape(name)}=(?:"([^"]*)"|(\S+))',
        unreal.SystemLibrary.get_command_line(),
    )
    return (match.group(1) or match.group(2)) if match else ""


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {
    "phase": "starting",
    "deadline": time.monotonic() + START_TIMEOUT_SECONDS,
    "result": None,
    "last": "PIE has not started",
    "world": None,
    "controller": None,
    "pawn": None,
    "movement": None,
    "combat": None,
    "weapon": None,
    "skill": None,
    "subsystem": None,
    "boxes": [],
    "cluster_boxes": [],
    "base_location": None,
    "ground_z": 0.0,
    "box_extent": unreal.Vector(50.0, 50.0, 50.0),
    "trace_target": None,
    "trace_score": float("inf"),
    "saw_calibration_trace": False,
    "phase_start": 0.0,
    "capture_start": 0.0,
    "capture_directory": command_line_value("PortfolioCaptureDir"),
    "capture_frame_count": 0,
    "next_capture_time": 0.0,
    "viewport_size": (0, 0),
    "melee_hit": False,
    "melee_box_snapped": False,
    "fireball": None,
    "explosion_receivers": 0,
    "destroyed_cluster_count": 0,
    "play_settings": None,
    "original_play_settings": None,
}
tick_handle = None


def object_path(value):
    return value.get_path_name() if value else "None"


def dot(left, right):
    return left.x * right.x + left.y * right.y + left.z * right.z


def game_time():
    world = state["world"]
    return unreal.GameplayStatics.get_time_seconds(world) if world else 0.0


def run_console(command):
    unreal.SystemLibrary.execute_console_command(
        state["world"], command, state["controller"]
    )


def restore_play_settings():
    play_settings = state["play_settings"]
    original = state["original_play_settings"]
    if play_settings is None or original is None:
        return
    for name, value in original.items():
        play_settings.set_editor_property(name, value)
    state["play_settings"] = None
    state["original_play_settings"] = None


def shutdown():
    global tick_handle

    if state["phase"] == "done":
        return
    state["phase"] = "done"
    ok, detail = state["result"]
    marker = SUCCESS_MARKER if ok else FAILURE_MARKER
    (unreal.log if ok else unreal.log_error)(f"{marker} {detail}")
    restore_play_settings()
    handle, tick_handle = tick_handle, None
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def finish(ok, detail):
    if state["result"] is not None:
        return
    state["result"] = (ok, detail)
    if level_editor.is_in_play_in_editor():
        state["phase"] = "stopping"
        state["deadline"] = time.monotonic() + STOP_TIMEOUT_SECONDS
        level_editor.editor_request_end_play()
    else:
        shutdown()


def fail(detail):
    finish(False, detail)


def stop_box_motion(box):
    mesh = box.get_intact_mesh()
    if mesh is None:
        raise RuntimeError(f"Box has no intact mesh: {object_path(box)}")
    mesh.set_physics_linear_velocity(unreal.Vector(), False)
    mesh.set_physics_angular_velocity_in_degrees(unreal.Vector(), False)
    mesh.put_rigid_body_to_sleep()


def place_box(box, location):
    mesh = box.get_intact_mesh()
    if mesh is None:
        raise RuntimeError(f"Box has no intact mesh: {object_path(box)}")
    mesh.set_simulate_physics(False)
    box.set_actor_location(location, False, True)
    box.set_actor_rotation(unreal.Rotator(), False)
    mesh.set_simulate_physics(True)
    mesh.set_enable_gravity(True)
    stop_box_motion(box)


def camera_look_at(target):
    view_location, _ = state["controller"].get_player_view_point()
    delta = target - view_location
    horizontal = math.sqrt(delta.x * delta.x + delta.y * delta.y)
    yaw = math.degrees(math.atan2(delta.y, delta.x))
    pitch = math.degrees(math.atan2(delta.z, max(horizontal, 0.001)))
    state["controller"].set_control_rotation(
        unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw)
    )


def sample_calibration_trace():
    pawn = state["pawn"]
    weapon = state["weapon"]
    base_location = state["base_location"]
    forward = unreal.Vector(1.0, 0.0, 0.0)
    right = unreal.Vector(0.0, 1.0, 0.0)
    trace_base = weapon.get_socket_location("WeaponTraceBase")
    trace_tip = weapon.get_socket_location("WeaponTraceTip")
    blade = trace_tip - trace_base
    trace_radius = 20.0
    desired_height = state["ground_z"] + state["box_extent"].z * 0.8

    for fraction in (0.35, 0.5, 0.65, 0.8, 0.95):
        target = trace_base + blade * fraction
        relative = target - base_location
        forward_distance = dot(relative, forward)
        lateral_distance = abs(dot(relative, right))
        vertical_error = abs(target.z - desired_height)
        if forward_distance < 40.0 or forward_distance > 280.0:
            continue
        score = (
            abs(forward_distance - 145.0)
            + lateral_distance * 0.3
            + max(
                0.0,
                vertical_error - state["box_extent"].z - trace_radius,
            )
            * 3.0
        )
        if score < state["trace_score"]:
            state["trace_score"] = score
            state["trace_target"] = unreal.Vector(target.x, target.y, target.z)


def prepare_calibration(world):
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if controller is None or pawn is None:
        state["last"] = "waiting for player controller and pawn"
        return
    if object_path(pawn.get_class()) != EXPECTED_PAWN:
        fail(f"unexpected pawn={object_path(pawn.get_class())}")
        return

    movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
    if movement is None or not movement.is_moving_on_ground():
        state["last"] = "waiting for the player to reach the ground"
        return

    boxes = [
        box
        for box in unreal.GameplayStatics.get_all_actors_with_tag(world, BOX_TAG)
        if box is not None
        and not box.is_broken()
        and abs(box.get_current_health() - box.get_max_health()) <= 0.01
    ]
    if len(boxes) < 5:
        fail(f"portfolio capture requires at least 5 intact boxes; found={len(boxes)}")
        return
    boxes.sort(key=lambda box: box.get_path_name())
    boxes = boxes[:5]

    combat = pawn.get_combat_component()
    weapon = pawn.get_combat_weapon()
    skill = pawn.get_world_skill_component()
    subsystem = unreal.RoverEditorTestLibrary.get_world_interaction_subsystem(world)
    if any(value is None for value in (combat, weapon, skill, subsystem)):
        fail("capture is missing combat, weapon, world skill, or interaction subsystem")
        return

    base_location = pawn.get_actor_location()
    base_location = unreal.Vector(base_location.x, base_location.y, base_location.z)
    ground_z = sorted(box.get_actor_location().z for box in boxes)[len(boxes) // 2]
    try:
        _, box_extent = boxes[0].get_actor_bounds(False, False)
    except Exception:
        box_extent = unreal.Vector(50.0, 50.0, 50.0)

    state.update(
        {
            "world": world,
            "controller": controller,
            "pawn": pawn,
            "movement": movement,
            "combat": combat,
            "weapon": weapon,
            "skill": skill,
            "subsystem": subsystem,
            "boxes": boxes,
            "base_location": base_location,
            "ground_z": ground_z,
            "box_extent": box_extent,
        }
    )

    for enemy in unreal.GameplayStatics.get_all_actors_with_tag(
        world, TRAINING_ENEMY_TAG
    ):
        enemy.set_actor_location(base_location + unreal.Vector(5000.0, 5000.0, 0.0), False, True)

    for index, box in enumerate(boxes):
        place_box(
            box,
            base_location
            + unreal.Vector(4000.0 + index * 140.0, 4000.0, ground_z - base_location.z),
        )

    movement.stop_movement_immediately()
    pawn.set_actor_location(base_location, False, True)
    pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False)
    controller.set_control_rotation(
        unreal.Rotator(roll=0.0, pitch=-10.0, yaw=0.0)
    )
    combat.set_light_attack_held(False)
    if not combat.request_attack():
        fail("calibration Attack01 request was rejected")
        return

    state["phase"] = "calibrating"
    state["deadline"] = time.monotonic() + PHASE_TIMEOUT_SECONDS
    state["last"] = "sampling the real Attack01 weapon path"
    unreal.log("PORTFOLIO_CAPTURE_CALIBRATION_STARTED")


def arrange_capture_scene():
    pawn = state["pawn"]
    movement = state["movement"]
    boxes = state["boxes"]
    base = state["base_location"]
    ground_z = state["ground_z"]
    trace_target = state["trace_target"]

    movement.stop_movement_immediately()
    pawn.set_actor_location(base, False, True)
    pawn.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False)

    melee_location = unreal.Vector(trace_target.x, trace_target.y, ground_z)
    cluster_center = unreal.Vector(base.x + 900.0, base.y, ground_z)
    cluster_locations = [
        cluster_center,
        cluster_center + unreal.Vector(60.0, 135.0, 0.0),
        cluster_center + unreal.Vector(60.0, -135.0, 0.0),
        cluster_center + unreal.Vector(175.0, 0.0, 0.0),
    ]

    place_box(boxes[0], melee_location)
    for box, location in zip(boxes[1:], cluster_locations):
        place_box(box, location)

    state["cluster_boxes"] = boxes[1:]
    camera_target = cluster_center + unreal.Vector(0.0, 0.0, state["box_extent"].z * 0.7)
    camera_look_at(camera_target)
    state["camera_target"] = camera_target
    state["phase"] = "settling"
    state["phase_start"] = game_time()
    state["deadline"] = time.monotonic() + PHASE_TIMEOUT_SECONDS
    state["last"] = "waiting for the staged boxes and camera to settle"
    unreal.log(
        "PORTFOLIO_CAPTURE_SCENE_READY "
        f"melee={melee_location} cluster={cluster_center} trace_score={state['trace_score']:.2f}"
    )


def start_recording():
    capture_directory = state["capture_directory"]
    if not capture_directory or not os.path.isdir(capture_directory):
        fail(f"invalid portfolio capture directory={capture_directory!r}")
        return
    for command in (
        "DisableAllScreenMessages",
        "sg.ViewDistanceQuality 3",
        "sg.AntiAliasingQuality 3",
        "sg.ShadowQuality 3",
        "sg.GlobalIlluminationQuality 2",
        "sg.ReflectionQuality 2",
        "sg.PostProcessQuality 3",
        "sg.TextureQuality 3",
        "sg.EffectsQuality 3",
        "r.ScreenPercentage 100",
        "rover.combat.DrawAttackTrace 1",
        "rover.combat.DrawAttackTraceDuration 0.75",
    ):
        run_console(command)
    state["capture_start"] = game_time()
    state["capture_frame_count"] = 0
    state["next_capture_time"] = 0.0
    state["phase"] = "establishing"
    state["deadline"] = time.monotonic() + PHASE_TIMEOUT_SECONDS
    state["last"] = "recording establishing shot"
    unreal.log("PORTFOLIO_CAPTURE_RECORDING_STARTED")


def capture_frame_if_due(recorded_elapsed):
    if recorded_elapsed + 1.0e-4 < state["next_capture_time"]:
        return
    if state["next_capture_time"] >= CAPTURE_DURATION_SECONDS:
        return

    frame_index = state["capture_frame_count"]
    filename = os.path.abspath(
        os.path.join(
            state["capture_directory"],
            f"portfolio_{frame_index:04d}.bmp",
        )
    )
    if not unreal.RoverEditorTestLibrary.capture_game_viewport_bitmap(
        state["world"], filename
    ):
        fail(f"unable to capture PIE game viewport frame={frame_index} file={filename}")
        return
    state["capture_frame_count"] += 1
    state["next_capture_time"] += 1.0 / CAPTURE_SAMPLE_FPS


def request_recorded_attack():
    combat = state["combat"]
    if not combat.request_attack():
        fail("recorded Attack01 request was rejected")
        return
    state["phase"] = "recorded_attack"
    state["deadline"] = time.monotonic() + PHASE_TIMEOUT_SECONDS
    state["last"] = "recording Attack01 and weapon Sweep visualization"
    unreal.log("PORTFOLIO_CAPTURE_ATTACK_STARTED")


def snap_melee_box_to_weapon():
    box = state["boxes"][0]
    weapon = state["weapon"]
    trace_base = weapon.get_socket_location("WeaponTraceBase")
    trace_tip = weapon.get_socket_location("WeaponTraceTip")
    trace_target = trace_base + (trace_tip - trace_base) * 0.85
    mesh = box.get_intact_mesh()
    if mesh is None:
        raise RuntimeError("Melee capture box has no intact mesh")
    mesh.set_simulate_physics(False)
    box.set_actor_location(trace_target, False, True)
    mesh.set_simulate_physics(True)
    mesh.set_enable_gravity(False)
    stop_box_motion(box)
    state["melee_box_snapped"] = True
    unreal.log(f"PORTFOLIO_CAPTURE_MELEE_SNAPPED target={trace_target}")


def request_recorded_fireball():
    state["subsystem"].reset_debug_stats()
    if not state["skill"].request_fireball():
        fail("recorded fireball request was rejected")
        return
    fireball = state["skill"].get_last_spawned_fireball()
    if fireball is None:
        fail("recorded fireball request spawned no projectile")
        return
    state["fireball"] = fireball
    state["phase"] = "recorded_fireball"
    state["deadline"] = time.monotonic() + PHASE_TIMEOUT_SECONDS
    state["last"] = "recording real fireball flight"
    unreal.log("PORTFOLIO_CAPTURE_FIREBALL_STARTED")


def tick(_delta_seconds):
    try:
        phase = state["phase"]
        if phase == "done":
            return
        if phase == "stopping":
            if not level_editor.is_in_play_in_editor():
                shutdown()
            elif time.monotonic() > state["deadline"]:
                state["result"] = (False, "PIE did not stop before timeout")
                shutdown()
            return
        if time.monotonic() > state["deadline"]:
            fail(f"phase={phase} timed out; last={state['last']}")
            return
        if phase == "starting":
            if not level_editor.is_in_play_in_editor():
                return
            world = unreal_editor.get_game_world()
            if world is not None:
                restore_play_settings()
                prepare_calibration(world)
            return

        combat = state["combat"]
        if phase == "calibrating":
            if combat.is_weapon_trace_active():
                state["saw_calibration_trace"] = True
                sample_calibration_trace()
            if combat.is_attacking():
                return
            if not state["saw_calibration_trace"] or state["trace_target"] is None:
                fail("calibration never observed the Attack01 weapon trace")
                return
            state["phase"] = "waiting_combo_reset"
            state["last"] = "waiting for Attack01 combo reset after calibration"
            return

        if phase == "waiting_combo_reset":
            if combat.get_current_combo_index() != -1:
                return
            arrange_capture_scene()
            return

        if phase == "settling":
            elapsed = game_time() - state["phase_start"]
            if elapsed < 0.25:
                return
            camera_look_at(state["camera_target"])
            width, height = state["controller"].get_viewport_size()
            if (
                abs(width - CAPTURE_WIDTH) > 2
                or abs(height - CAPTURE_HEIGHT) > 2
            ):
                run_console(f"r.SetRes {CAPTURE_WIDTH}x{CAPTURE_HEIGHT}w")
                state["phase_start"] = game_time()
                state["last"] = f"waiting for viewport resize; current={width}x{height}"
                return
            if elapsed < 0.75:
                return
            state["viewport_size"] = (width, height)
            start_recording()
            return

        recorded_elapsed = game_time() - state["capture_start"]
        capture_frame_if_due(recorded_elapsed)
        if state["result"] is not None:
            return
        if recorded_elapsed >= CAPTURE_DURATION_SECONDS:
            if not state["melee_hit"]:
                fail("recording ended without a real melee crate hit")
                return
            if state["explosion_receivers"] < 3:
                fail(
                    "recording ended without a multi-crate explosion; "
                    f"receivers={state['explosion_receivers']}"
                )
                return
            finish(
                True,
                f"duration={recorded_elapsed:.2f}s melee=true "
                f"explosion_receivers={state['explosion_receivers']} "
                f"destroyed_cluster={state['destroyed_cluster_count']} "
                f"frames={state['capture_frame_count']} "
                f"viewport={state['viewport_size'][0]}x{state['viewport_size'][1]}",
            )
            return

        if phase == "establishing":
            if recorded_elapsed >= 1.2:
                request_recorded_attack()
            return

        if phase == "recorded_attack":
            melee_box = state["boxes"][0]
            if not state["melee_hit"]:
                if (
                    combat.is_weapon_trace_active()
                    and not state["melee_box_snapped"]
                    and not melee_box.is_broken()
                ):
                    snap_melee_box_to_weapon()
                if melee_box.is_broken() or melee_box.get_current_health() <= 0.0:
                    state["melee_hit"] = True
            if combat.is_attacking():
                return
            if not state["melee_hit"]:
                box_states = "; ".join(
                    f"{box.get_name()} hp={box.get_current_health():.1f} "
                    f"location={box.get_actor_location()}"
                    for box in state["boxes"]
                )
                fail(
                    "Attack01 completed without hitting the staged crate; "
                    f"boxes=[{box_states}]"
                )
                return
            state["phase"] = "post_melee"
            state["phase_start"] = game_time()
            state["last"] = "holding the melee fracture before fireball"
            unreal.log("PORTFOLIO_CAPTURE_MELEE_HIT")
            return

        if phase == "post_melee":
            if game_time() - state["phase_start"] >= 0.35:
                request_recorded_fireball()
            return

        if phase == "recorded_fireball":
            subsystem = state["subsystem"]
            if subsystem.get_processed_request_count() < 1:
                return
            result = subsystem.get_last_interaction_result()
            if not result.get_editor_property("accepted"):
                fail("fireball explosion interaction was rejected")
                return
            receivers = int(result.get_editor_property("receiver_count"))
            destroyed_count = sum(
                1
                for box in state["cluster_boxes"]
                if (not unreal.SystemLibrary.is_valid(box))
                or box.is_broken()
                or box.get_current_health() <= 0.0
            )
            if receivers < 3 or destroyed_count < 3:
                fail(
                    "fireball did not break multiple staged crates; "
                    f"receivers={receivers} destroyed={destroyed_count}"
                )
                return
            state["explosion_receivers"] = receivers
            state["destroyed_cluster_count"] = destroyed_count
            state["phase"] = "post_explosion"
            state["last"] = "recording Chaos debris expansion and cleanup"
            unreal.log(
                "PORTFOLIO_CAPTURE_MULTI_BOX_HIT "
                f"receivers={receivers} destroyed={destroyed_count}"
            )
            return

        if phase == "post_explosion":
            return
    except Exception:
        fail("unexpected Python error:\n" + traceback.format_exc())


def request_pie():
    play_settings_class = unreal.load_class(
        None, "/Script/UnrealEd.LevelEditorPlaySettings"
    )
    if play_settings_class is None:
        raise RuntimeError("LevelEditorPlaySettings class is unavailable")
    play_settings = unreal.get_default_object(play_settings_class)
    overrides = {
        "NewWindowWidth": CAPTURE_WIDTH,
        "NewWindowHeight": CAPTURE_HEIGHT,
        "CenterNewWindow": True,
        "PIEAlwaysOnTop": False,
        "EnableGameSound": False,
    }
    state["play_settings"] = play_settings
    state["original_play_settings"] = {
        name: play_settings.get_editor_property(name) for name in overrides
    }
    for name, value in overrides.items():
        play_settings.set_editor_property(name, value)
    if not unreal.RoverEditorTestLibrary.request_play_in_new_window():
        restore_play_settings()
        raise RuntimeError("Unable to request Play In New Window")


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
tick_handle = unreal.register_slate_post_tick_callback(tick)
try:
    request_pie()
except Exception:
    state["result"] = (False, "unable to start PIE:\n" + traceback.format_exc())
    shutdown()
