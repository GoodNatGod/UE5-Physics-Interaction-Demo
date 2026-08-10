import unreal


JUMP_ACTION_PATH = "/Game/Input/Actions/IA_Jump.IA_Jump"


jump_action = unreal.load_asset(JUMP_ACTION_PATH)
if not isinstance(jump_action, unreal.InputAction):
    raise RuntimeError(f"Missing Jump Input Action: {JUMP_ACTION_PATH}")

# With no explicit triggers, Started fires on press and Completed fires on release.
# Pressed + Released trigger objects make the action complete immediately after each edge.
jump_action.set_editor_property("triggers", [])
if jump_action.get_editor_property("triggers"):
    raise RuntimeError("IA_Jump retained explicit triggers after configuration.")

if not unreal.EditorAssetLibrary.save_loaded_asset(jump_action, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save configured Jump Input Action: {JUMP_ACTION_PATH}")

unreal.log("ROVER_INPUT_CONFIG_OK jump_triggers=none")
