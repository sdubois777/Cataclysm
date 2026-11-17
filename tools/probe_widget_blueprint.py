"""Probe: can a Widget Blueprint be built from editor Python at all?

Run with:  python tools/run_editor_python.py tools/probe_widget_blueprint.py

WHY THIS EXISTS. `docs/DECISIONS.md`, 2026-08-24, "Screens are a C++ base class
with the layout in a Widget Blueprint", commits every new screen to shipping a
`.uasset` that derives from a C++ `UUserWidget` and satisfies its `BindWidget`
properties. Nothing in this repository had ever created a Widget Blueprint by any
means, so before writing a screen against that decision it was worth finding out
whether the editor's Python interface can create one, put named widgets in its
tree, compile it and save it.

It writes to /Game/Developers/Probe and deletes what it wrote, so it leaves
nothing behind and cannot be mistaken for a generator.
"""

import unreal

PROBE_DIR = "/Game/Developers/Probe"
PROBE_NAME = "WBP_ProbeThrowaway"
PROBE_PATH = "{}/{}".format(PROBE_DIR, PROBE_NAME)

log = unreal.log
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def report(step, ok, detail=""):
    unreal.log("PROBE {:<28} {}  {}".format(step, "ok" if ok else "FAILED", detail))
    return ok


def main():
    if editor_assets.does_asset_exist(PROBE_PATH):
        editor_assets.delete_asset(PROBE_PATH)

    # --- create -------------------------------------------------------------
    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", unreal.UserWidget)
    blueprint = asset_tools.create_asset(PROBE_NAME, PROBE_DIR,
                                         unreal.WidgetBlueprint, factory)
    if not report("create_asset", blueprint is not None):
        raise SystemExit(1)

    # --- reach the widget tree ---------------------------------------------
    tree = blueprint.get_editor_property("widget_tree")
    if not report("widget_tree", tree is not None, str(tree)):
        raise SystemExit(1)

    # --- construct widgets --------------------------------------------------
    try:
        canvas = unreal.new_object(unreal.CanvasPanel, tree, "RootCanvas")
        label = unreal.new_object(unreal.TextBlock, tree, "ProbeLabel")
        button = unreal.new_object(unreal.Button, tree, "ProbeButton")
        box = unreal.new_object(unreal.VerticalBox, tree, "ProbeBox")
    except Exception as error:            # noqa: BLE001 -- a probe reports
        report("new_object", False, repr(error))
        raise SystemExit(1) from error
    report("new_object", True, "canvas, text block, button, vertical box")

    # --- assemble a tree ----------------------------------------------------
    try:
        tree.set_editor_property("root_widget", canvas)
        slot = canvas.add_child(box)
        box.add_child(label)
        box.add_child(button)
    except Exception as error:            # noqa: BLE001
        report("add_child", False, repr(error))
        raise SystemExit(1) from error
    report("add_child", True, "slot is {}".format(type(slot).__name__))

    # --- make them variables, which is what BindWidget matches on -----------
    try:
        for widget in (canvas, label, button, box):
            widget.set_editor_property("is_variable", True)
    except Exception as error:            # noqa: BLE001
        report("is_variable", False, repr(error))
        raise SystemExit(1) from error
    report("is_variable", True)

    # --- can a slot be positioned? -----------------------------------------
    try:
        slot.set_editor_property("anchors", unreal.Anchors(unreal.Vector2D(0.5, 0.5),
                                                           unreal.Vector2D(0.5, 0.5)))
        slot.set_editor_property("alignment", unreal.Vector2D(0.5, 0.5))
        slot.set_editor_property("auto_size", True)
        report("canvas slot layout", True)
    except Exception as error:            # noqa: BLE001
        report("canvas slot layout", False, repr(error))

    # --- can text and font be set? -----------------------------------------
    try:
        label.set_text("Probe")
        font = label.get_editor_property("font")
        font.set_editor_property("size", 24)
        label.set_editor_property("font", font)
        report("text and font", True)
    except Exception as error:            # noqa: BLE001
        report("text and font", False, repr(error))

    # --- compile and save ---------------------------------------------------
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        report("compile_blueprint", True)
    except Exception as error:            # noqa: BLE001
        report("compile_blueprint", False, repr(error))

    saved = editor_assets.save_asset(PROBE_PATH)
    report("save_asset", bool(saved))

    # --- read it back -------------------------------------------------------
    reloaded = editor_assets.load_asset(PROBE_PATH)
    if reloaded:
        back = reloaded.get_editor_property("widget_tree")
        names = []
        try:
            for widget in back.get_editor_property("all_widgets"):
                names.append(widget.get_name())
        except Exception:                 # noqa: BLE001
            names = ["all_widgets unavailable"]
        report("read back", True, "widgets: {}".format(", ".join(names)))

    # --- clean up -----------------------------------------------------------
    editor_assets.delete_asset(PROBE_PATH)
    report("delete_asset", not editor_assets.does_asset_exist(PROBE_PATH))
    log("PROBE done")


main()
