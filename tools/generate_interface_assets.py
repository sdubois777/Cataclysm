"""Builds the Widget Blueprints the interface C++ classes need, first time only.

    python tools/run_editor_python.py tools/generate_interface_assets.py

WHAT IT MAKES, under `/Game/Interface`:

    WBP_ChoiceButton        one option in a list: a weapon type, a damage type,
                            a passive node
    WBP_CharacterCreation   the character creator, issue #50
    WBP_PassiveTree         the passive class tree, issue #50
    WBP_EmpireMap           the empire overview, issue #1087

WHY A GENERATOR AND NOT A HAND-DRAWN ASSET. `docs/DECISIONS.md`, 2026-08-24,
puts a screen's layout in a Widget Blueprint and its logic in a C++ base class,
joined by `UPROPERTY(meta = (BindWidget))`. The Blueprint compiler refuses to
compile a tree that is missing one of those, so the FIRST version of a screen has
to contain a widget of the right name and type for each -- and getting that right
by clicking is a chore that produces the same tree every time. This writes it.

IT REFUSES TO TOUCH AN ASSET THAT ALREADY EXISTS, and that is the whole point of
the split rather than a safety catch. Layout belongs to whoever opens the
designer. Delete the asset if you want a fresh one.

WHY THE TREE IS BUILT THROUGH C++ AND NOT HERE. `UWidgetBlueprint::WidgetTree` is
a plain `UPROPERTY()` with no `EditAnywhere`, so the editor scripting layer
refuses to read it and every widget in the tree is behind it. Measured on
2026-08-25 by `tools/probe_widget_blueprint.py`, which reported:

    Exception: WidgetBlueprint: Failed to find property 'widget_tree' for
    attribute 'widget_tree' on 'WidgetBlueprint'

`UCataclysmWidgetAuthoring` in the `CataclysmEditor` module exists for that, in
the same way and for the same reason as `UCataclysmLevelAuthoring`.

WHAT THE FIRST VERSION LOOKS LIKE, stated plainly: a dark panel, a column of
headings and lists, nothing else. It is legible and it is not designed. Making it
look like something is the designer's job and this script must not be extended to
do it -- every property it sets is one a person opening the Blueprint would have
to fight.
"""

import unreal

INTERFACE_DIR = "/Game/Interface"

# The C++ classes the Blueprints derive from. Named as the editor sees them,
# without the leading U.
CHOICE_BUTTON_PARENT = "CataclysmChoiceButton"
CREATION_PARENT = "CataclysmCharacterCreationWidget"

CHOICE_BUTTON_ASSET = "WBP_ChoiceButton"
CREATION_ASSET = "WBP_CharacterCreation"
PASSIVE_PARENT = "CataclysmPassiveTreeWidget"
PASSIVE_ASSET = "WBP_PassiveTree"
EMPIRE_PARENT = "CataclysmEmpireMapWidget"
EMPIRE_ASSET = "WBP_EmpireMap"

# --- the little colour there is ----------------------------------------------
# THE SAME NEARLY-BLACK PANEL THE INVENTORY SCREEN USES, from
# UCataclysmInventoryScreen::PanelHex, so the two screens do not read as parts of
# different games. Everything else is left at the widget's own default.
PANEL = unreal.LinearColor(0.039, 0.059, 0.071, 0.94)
INK = unreal.LinearColor(0.961, 0.941, 0.918, 1.0)

log = unreal.log
editor_assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

authoring = unreal.CataclysmWidgetAuthoring


def parent_class(name):
    """The C++ UUserWidget subclass of that name, or raise saying so."""
    found = getattr(unreal, name, None)
    if found is None:
        raise SystemExit(
            "unreal.{} does not exist. The C++ has to be compiled before this "
            "runs: python tools/unreal_build.py build".format(name))
    return found


def add(blueprint, widget_class, name, parent=""):
    """One widget into the tree, complaining by name when it does not go in."""
    widget = authoring.add_widget(blueprint, widget_class, name, parent)
    if widget is None:
        raise SystemExit(
            "Could not add {} ({}) under {!r}. The editor log says why."
            .format(name, widget_class.get_name(), parent or "the root"))
    return widget


def set_text(widget, text, size=None, colour=INK):
    """A text block's words, size and colour, all optional except the words."""
    widget.set_text(unreal.Text(text) if not isinstance(text, unreal.Text) else text)
    widget.set_color_and_opacity(unreal.SlateColor(colour))
    if size is not None:
        font = widget.get_editor_property("font")
        font.set_editor_property("size", size)
        widget.set_editor_property("font", font)


def fill_the_screen(widget):
    """Anchor a canvas child to all four edges, with no inset.

    THE ONE PIECE OF GEOMETRY THIS SCRIPT SETS. A canvas child defaults to a
    100 by 100 box in the top left corner, so without this the whole screen is
    a small square in the corner, which reads as a broken asset rather than as
    an undesigned one.

    THROUGH `layout_data` AND NOT THROUGH `anchors` DIRECTLY. A
    `UCanvasPanelSlot` keeps its anchors, offsets and alignment inside one
    `FAnchorData` struct called `LayoutData`, and that struct is the
    `EditAnywhere` property. Setting `anchors` on the slot fails with

        Exception: CanvasPanelSlot: Failed to find property 'anchors' for
        attribute 'anchors' on 'CanvasPanelSlot'
    """
    slot = widget.get_editor_property("slot")
    layout = slot.get_editor_property("layout_data")
    layout.set_editor_property(
        "anchors", unreal.Anchors(unreal.Vector2D(0.0, 0.0),
                                  unreal.Vector2D(1.0, 1.0)))
    layout.set_editor_property("offsets", unreal.Margin(0.0, 0.0, 0.0, 0.0))
    slot.set_editor_property("layout_data", layout)


def check_every_bound_widget(blueprint, parent):
    """Every BindWidget the base class declares has a widget in the tree.

    ASKED HERE AS WELL AS BY THE COMPILE, because the compile reports one
    missing widget at a time and this reports all of them at once. A generator
    that added nine of twelve is a script to fix, not a Blueprint to open.
    """
    required = set(authoring.required_widget_names(parent))
    present = set(authoring.widget_names(blueprint))
    missing = sorted(required - present)
    if missing:
        raise SystemExit(
            "{} does not satisfy {}: no widget named {}. Present: {}".format(
                blueprint.get_name(), parent.get_name(),
                ", ".join(missing), ", ".join(sorted(present))))
    log("  satisfies {} BindWidget properties: {}".format(
        len(required), ", ".join(sorted(required))))


def make_choice_button():
    parent = parent_class(CHOICE_BUTTON_PARENT)
    if authoring.widget_blueprint_exists(INTERFACE_DIR, CHOICE_BUTTON_ASSET):
        log("{}/{} already exists; left alone.".format(
            INTERFACE_DIR, CHOICE_BUTTON_ASSET))
        return

    blueprint = authoring.create_or_load_widget_blueprint(
        INTERFACE_DIR, CHOICE_BUTTON_ASSET, parent)
    if blueprint is None:
        raise SystemExit("Could not create {}.".format(CHOICE_BUTTON_ASSET))

    # A BUTTON HOLDS EXACTLY ONE CHILD, so the label goes inside it and nothing
    # else can. That is the whole widget.
    add(blueprint, unreal.Button, "ChoiceButton")
    label = add(blueprint, unreal.TextBlock, "ChoiceLabel", "ChoiceButton")
    set_text(label, "Option", size=16)

    check_every_bound_widget(blueprint, parent)
    if not authoring.compile_and_save(blueprint):
        raise SystemExit("{} did not compile or could not be saved.".format(
            CHOICE_BUTTON_ASSET))
    log("created {}/{}".format(INTERFACE_DIR, CHOICE_BUTTON_ASSET))


def make_character_creation():
    parent = parent_class(CREATION_PARENT)
    if authoring.widget_blueprint_exists(INTERFACE_DIR, CREATION_ASSET):
        log("{}/{} already exists; left alone.".format(
            INTERFACE_DIR, CREATION_ASSET))
        return

    blueprint = authoring.create_or_load_widget_blueprint(
        INTERFACE_DIR, CREATION_ASSET, parent)
    if blueprint is None:
        raise SystemExit("Could not create {}.".format(CREATION_ASSET))

    add(blueprint, unreal.CanvasPanel, "RootCanvas")

    backdrop = add(blueprint, unreal.Border, "Backdrop", "RootCanvas")
    backdrop.set_editor_property("brush_color", PANEL)
    fill_the_screen(backdrop)
    backdrop.set_editor_property("padding", unreal.Margin(48.0, 36.0, 48.0, 36.0))

    add(blueprint, unreal.VerticalBox, "Body", "Backdrop")

    title = add(blueprint, unreal.TextBlock, "TitleLabel", "Body")
    set_text(title, "Create a character", size=30)

    weapon_heading = add(blueprint, unreal.TextBlock, "WeaponHeading", "Body")
    set_text(weapon_heading, "Weapon", size=18)

    # A WRAP BOX RATHER THAN A HORIZONTAL ONE. Fourteen weapon types do not fit
    # across a window in one line, and a horizontal box would squeeze them to
    # nothing rather than starting a second row.
    add(blueprint, unreal.WrapBox, "WeaponTypeBox", "Body")

    damage_heading = add(blueprint, unreal.TextBlock, "DamageHeading", "Body")
    set_text(damage_heading, "Damage type", size=18)

    add(blueprint, unreal.WrapBox, "DamageTypeBox", "Body")

    summary = add(blueprint, unreal.TextBlock, "SummaryLabel", "Body")
    set_text(summary, "", size=18)

    unlocked = add(blueprint, unreal.TextBlock, "UnlockedLabel", "Body")
    set_text(unlocked, "", size=16)

    refusal = add(blueprint, unreal.TextBlock, "RefusalLabel", "Body")
    set_text(refusal, "", size=16)

    add(blueprint, unreal.Button, "ConfirmButton", "Body")
    confirm = add(blueprint, unreal.TextBlock, "ConfirmLabel", "ConfirmButton")
    set_text(confirm, "Begin", size=20)

    check_every_bound_widget(blueprint, parent)
    if not authoring.compile_and_save(blueprint):
        raise SystemExit("{} did not compile or could not be saved.".format(
            CREATION_ASSET))
    log("created {}/{}".format(INTERFACE_DIR, CREATION_ASSET))



def fill_remaining_height(widget):
    """Let a child of a vertical box take whatever height is left.

    A VERTICAL BOX SIZES EACH CHILD TO ITS CONTENTS BY DEFAULT, so a scroll box
    holding 74 rows would be 74 rows tall and run off the bottom of the screen
    rather than scrolling. `Fill` is what makes it stop at the space available
    and scroll inside it.
    """
    slot = widget.get_editor_property("slot")
    slot.set_editor_property(
        "size", unreal.SlateChildSize(value=1.0,
                                      size_rule=unreal.SlateSizeRule.FILL))


def make_passive_tree():
    parent = parent_class(PASSIVE_PARENT)
    if authoring.widget_blueprint_exists(INTERFACE_DIR, PASSIVE_ASSET):
        log("{}/{} already exists; left alone.".format(
            INTERFACE_DIR, PASSIVE_ASSET))
        return

    blueprint = authoring.create_or_load_widget_blueprint(
        INTERFACE_DIR, PASSIVE_ASSET, parent)
    if blueprint is None:
        raise SystemExit("Could not create {}.".format(PASSIVE_ASSET))

    add(blueprint, unreal.CanvasPanel, "RootCanvas")

    backdrop = add(blueprint, unreal.Border, "Backdrop", "RootCanvas")
    backdrop.set_editor_property("brush_color", PANEL)
    fill_the_screen(backdrop)
    backdrop.set_editor_property("padding", unreal.Margin(48.0, 36.0, 48.0, 36.0))

    add(blueprint, unreal.VerticalBox, "Body", "Backdrop")

    title = add(blueprint, unreal.TextBlock, "TitleLabel", "Body")
    set_text(title, "Passive tree", size=30)

    points = add(blueprint, unreal.TextBlock, "PointsLabel", "Body")
    set_text(points, "", size=18)

    # THE FOUR TREES. A wrap box rather than a horizontal one, for the reason
    # the creator's weapon list is one: the other twenty trees arrive in issue
    # #24 and a horizontal box would squeeze them all onto one line.
    add(blueprint, unreal.WrapBox, "TreeBox", "Body")

    tree = add(blueprint, unreal.TextBlock, "TreeLabel", "Body")
    set_text(tree, "", size=18)

    # WHERE THE TREE IS DRAWN. A canvas panel, because every node goes at its
    # own authored position rather than in a list: which limb a node is on and
    # how far it is from the trunk are decisions made in the tree authoring tool
    # and a list threw all of them away. Issue #937.
    #
    # THE PANEL IS EMPTY HERE AND FILLED AT RUN TIME.
    # UCataclysmPassiveTreeWidget places one button per node and one line per
    # edge on it, scaled and panned. No designer could place 74 of each by hand,
    # and the arithmetic that does it is in UCataclysmPassiveTreeLayout.
    canvas = add(blueprint, unreal.CanvasPanel, "GraphCanvas", "Body")
    fill_remaining_height(canvas)
    canvas.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)

    description = add(blueprint, unreal.TextBlock, "DescriptionLabel", "Body")
    set_text(description, "", size=16)
    description.set_editor_property("auto_wrap_text", True)

    refusal = add(blueprint, unreal.TextBlock, "RefusalLabel", "Body")
    set_text(refusal, "", size=16)
    refusal.set_editor_property("auto_wrap_text", True)

    check_every_bound_widget(blueprint, parent)
    if not authoring.compile_and_save(blueprint):
        raise SystemExit("{} did not compile or could not be saved.".format(
            PASSIVE_ASSET))
    log("created {}/{}".format(INTERFACE_DIR, PASSIVE_ASSET))


def make_empire_map():
    parent = parent_class(EMPIRE_PARENT)
    if authoring.widget_blueprint_exists(INTERFACE_DIR, EMPIRE_ASSET):
        log("{}/{} already exists; left alone.".format(
            INTERFACE_DIR, EMPIRE_ASSET))
        return

    blueprint = authoring.create_or_load_widget_blueprint(
        INTERFACE_DIR, EMPIRE_ASSET, parent)
    if blueprint is None:
        raise SystemExit("Could not create {}.".format(EMPIRE_ASSET))

    add(blueprint, unreal.CanvasPanel, "RootCanvas")

    backdrop = add(blueprint, unreal.Border, "Backdrop", "RootCanvas")
    backdrop.set_editor_property("brush_color", PANEL)
    fill_the_screen(backdrop)
    backdrop.set_editor_property("padding", unreal.Margin(48.0, 36.0, 48.0, 36.0))

    add(blueprint, unreal.VerticalBox, "Body", "Backdrop")

    title = add(blueprint, unreal.TextBlock, "TitleLabel", "Body")
    set_text(title, "The empire", size=30)

    status = add(blueprint, unreal.TextBlock, "StatusLabel", "Body")
    set_text(status, "", size=18)

    surge = add(blueprint, unreal.TextBlock, "SurgeLabel", "Body")
    set_text(surge, "", size=18)

    # WHERE THE 25 CITIES ARE DRAWN. A canvas panel, because only 25 of a 7 by 7
    # grid's 49 cells exist: a grid would need 24 empty widgets to hold the
    # diamond's shape, and the diamond would be locked to whatever spacing the
    # grid was given rather than fitted to the panel.
    #
    # THE PANEL IS EMPTY HERE AND FILLED AT RUN TIME.
    # UCataclysmEmpireMapWidget places one box per city on it, and
    # UCataclysmEmpireMapLayout is the arithmetic that decides where.
    canvas = add(blueprint, unreal.CanvasPanel, "MapCanvas", "Body")
    fill_remaining_height(canvas)
    canvas.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)

    detail = add(blueprint, unreal.TextBlock, "DetailLabel", "Body")
    set_text(detail, "", size=16)
    detail.set_editor_property("auto_wrap_text", True)

    check_every_bound_widget(blueprint, parent)
    if not authoring.compile_and_save(blueprint):
        raise SystemExit("{} did not compile or could not be saved.".format(
            EMPIRE_ASSET))
    log("created {}/{}".format(INTERFACE_DIR, EMPIRE_ASSET))


def main():
    # THE BUTTON FIRST. The screen's ChoiceButtonClass points at it by path, so
    # a screen made before it exists would load nothing on its first press.
    make_choice_button()
    make_character_creation()
    make_passive_tree()
    make_empire_map()
    editor_assets.save_directory(INTERFACE_DIR, recursive=True)
    log("done")


main()
