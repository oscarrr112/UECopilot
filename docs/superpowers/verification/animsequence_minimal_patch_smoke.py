import unreal


ASSET_PATH = "/Game/Generated/Animation/AS_Minimal.AS_Minimal"


def _call(target, method_name, *args):
    method = getattr(target, method_name, None)
    if method is None:
        return None
    try:
        return method(*args)
    except Exception:
        return None


def _get_property(value, *property_names):
    getter = getattr(value, "get_editor_property", None)
    if getter is None:
        return None

    for property_name in property_names:
        try:
            return getter(property_name)
        except Exception:
            pass

    return None


def _as_list(value):
    if value is None:
        return []
    try:
        return list(value)
    except TypeError:
        return [value]


def _name(value):
    if value is None:
        return ""

    for method_name in ("get_name", "get_display_name"):
        method = getattr(value, method_name, None)
        if method is None:
            continue
        try:
            result = method()
            if result:
                return str(result)
        except Exception:
            pass

    return str(value)


def _get_skeleton(asset):
    skeleton = _call(asset, "get_skeleton")
    if skeleton is not None:
        return skeleton

    library = getattr(unreal, "AnimationLibrary", None)
    if library is not None:
        skeleton = _call(library, "get_skeleton", asset)
        if skeleton is not None:
            return skeleton

    return _get_property(asset, "skeleton")


def _get_sampled_key_count(asset):
    for method_name in (
        "get_number_of_sampled_keys",
        "get_number_of_frames",
        "get_num_frames",
    ):
        result = _call(asset, method_name)
        if result is not None:
            return int(result)

    library = getattr(unreal, "AnimationLibrary", None)
    if library is not None:
        for method_name in ("get_num_frames", "get_num_keys"):
            result = _call(library, method_name, asset)
            if result is not None:
                return int(result)

    data_model = _call(asset, "get_data_model")
    if data_model is not None:
        for method_name in ("get_number_of_keys", "get_number_of_frames"):
            result = _call(data_model, method_name)
            if result is not None:
                return int(result)

    return 0


def _get_notify_events(asset):
    library = getattr(unreal, "AnimationLibrary", None)
    if library is not None:
        for method_name in (
            "get_animation_notify_events",
            "get_animation_notifies",
        ):
            events = _call(library, method_name, asset)
            if events:
                return _as_list(events)

    events = _get_property(asset, "notifies")
    if events:
        return _as_list(events)

    events = []
    tracks = _get_property(asset, "anim_notify_tracks", "notify_tracks")
    for track in _as_list(tracks):
        events.extend(_as_list(_get_property(track, "notifies")))

    return events


def _get_sync_markers(asset):
    library = getattr(unreal, "AnimationLibrary", None)
    if library is not None:
        for method_name in (
            "get_animation_sync_markers",
            "get_sync_markers",
        ):
            markers = _call(library, method_name, asset)
            if markers:
                return _as_list(markers)

    return _as_list(_get_property(asset, "authored_sync_markers", "sync_markers"))


def _get_notify_names(event):
    names = set()

    for property_name in ("notify_name", "name"):
        value = _get_property(event, property_name)
        if value is not None:
            names.add(_name(value))

    for property_name in ("notify", "notify_state_class"):
        value = _get_property(event, property_name)
        if value is None:
            continue
        notify_name = _get_property(value, "notify_name")
        if notify_name is not None:
            names.add(_name(notify_name))
        names.add(_name(value))
        value_class = _call(value, "get_class")
        if value_class is not None:
            names.add(_name(value_class))

    names.add(_name(event))
    return names


def _get_marker_name(marker):
    value = _get_property(marker, "marker_name", "name")
    if value is not None:
        return _name(value)
    return _name(marker)


asset = unreal.load_asset(ASSET_PATH)
if asset is None:
    raise RuntimeError("Missing generated AnimSequence: {0}".format(ASSET_PATH))

if not isinstance(asset, unreal.AnimSequence):
    raise RuntimeError(
        "Generated asset is not AnimSequence: {0}".format(asset.get_class().get_name())
    )

skeleton = _get_skeleton(asset)
if skeleton is None:
    raise RuntimeError("Generated AnimSequence has no skeleton")

sampled_key_count = _get_sampled_key_count(asset)
if sampled_key_count < 1:
    raise RuntimeError("Generated AnimSequence has no sampled keys")

notify_names = set()
for event in _get_notify_events(asset):
    notify_names.update(_get_notify_names(event))

if not any("Footstep" in notify_name for notify_name in notify_names):
    raise RuntimeError("Expected Footstep notify")

if not any("Window" in notify_name for notify_name in notify_names):
    raise RuntimeError("Expected Window notify state")

marker_names = {_get_marker_name(marker) for marker in _get_sync_markers(asset)}
if not any("LeftFoot" in marker_name for marker_name in marker_names):
    raise RuntimeError("Expected LeftFoot sync marker")

unreal.log("AnimSequence minimal/patch smoke passed")
