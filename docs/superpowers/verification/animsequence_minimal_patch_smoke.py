import unreal


ASSET_PATH = "/Game/Generated/Animation/AS_Minimal.AS_Minimal"


def _try_call(target, method_name, *args):
    method = getattr(target, method_name, None)
    if method is None:
        return None
    try:
        return method(*args)
    except Exception:
        return None


def _call_required(target, method_name, *args):
    method = getattr(target, method_name, None)
    if method is None:
        raise RuntimeError("Missing required method: {0}".format(method_name))
    try:
        return method(*args)
    except Exception as exc:
        raise RuntimeError("Failed to call {0}: {1}".format(method_name, exc))


def _try_get_property(value, property_name):
    getter = getattr(value, "get_editor_property", None)
    if getter is None:
        return None

    try:
        return getter(property_name)
    except Exception:
        return None


def _get_property(value, *property_names):
    for property_name in property_names:
        result = _try_get_property(value, property_name)
        if result is not None:
            return result

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


def _normalized_name(value):
    name = _name(value).strip()
    if name.startswith("Name(") and name.endswith(")"):
        name = name[5:-1]
    if "'" in name:
        name = name.strip("'")
    if "." in name:
        name = name.rsplit(".", 1)[-1]
    if ":" in name:
        name = name.rsplit(":", 1)[-1]
    return name.strip()


def _names_match(names, expected_name):
    expected = _normalized_name(expected_name)
    return any(_normalized_name(name) == expected for name in names)


def _get_skeleton(asset):
    if getattr(asset, "get_skeleton", None) is not None:
        skeleton = _call_required(asset, "get_skeleton")
        if skeleton is not None:
            return skeleton

    library = getattr(unreal, "AnimationLibrary", None)
    if library is not None:
        skeleton = _try_call(library, "get_skeleton", asset)
        if skeleton is not None:
            return skeleton

    return _get_property(asset, "skeleton")


def _get_sampled_key_count(asset):
    for method_name in (
        "get_number_of_sampled_keys",
        "get_number_of_frames",
        "get_num_frames",
    ):
        if getattr(asset, method_name, None) is not None:
            result = _call_required(asset, method_name)
            if result is not None:
                return int(result)

    library = getattr(unreal, "AnimationLibrary", None)
    if library is not None:
        for method_name in ("get_num_frames", "get_num_keys"):
            result = _try_call(library, method_name, asset)
            if result is not None:
                return int(result)

    data_model = None
    if getattr(asset, "get_data_model", None) is not None:
        data_model = _call_required(asset, "get_data_model")
    if data_model is not None:
        for method_name in ("get_number_of_keys", "get_number_of_frames"):
            if getattr(data_model, method_name, None) is not None:
                result = _call_required(data_model, method_name)
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
            events = _try_call(library, method_name, asset)
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
            markers = _try_call(library, method_name, asset)
            if markers:
                return _as_list(markers)

    return _as_list(_get_property(asset, "authored_sync_markers", "sync_markers"))


def _get_notify_names_from_object(value):
    names = set()

    if value is None:
        return names

    notify_name = _get_property(value, "notify_name")
    if notify_name is not None:
        names.add(_name(notify_name))

    names.add(_name(value))
    if getattr(value, "get_class", None) is not None:
        value_class = _call_required(value, "get_class")
        if value_class is not None:
            names.add(_name(value_class))

    return names


def _get_event_names(event):
    names = set()

    for property_name in ("notify_name", "name"):
        value = _get_property(event, property_name)
        if value is not None:
            names.add(_name(value))

    names.add(_name(event))
    return names


def _is_notify_state_event(event):
    if _get_property(event, "notify_state_class") is not None:
        return True

    duration = _get_notify_event_duration(event)
    return duration is not None and duration > 0.0


def _get_notify_event_duration(event):
    library = getattr(unreal, "AnimationLibrary", None)
    if library is not None:
        duration = _try_call(library, "get_anim_notify_event_duration", event)
        if duration is not None:
            return float(duration)

    for property_name in ("duration", "duration_time", "notify_duration", "notify_event_duration"):
        value = _get_property(event, property_name)
        if value is not None:
            return float(value)

    end_link = _get_property(event, "end_link")
    if end_link is not None:
        value = _get_property(end_link, "time", "segment_begin_time")
        if value is not None:
            start_time = _get_property(event, "time", "display_time")
            if start_time is not None:
                return float(value) - float(start_time)

    return None


def _get_normal_notify_names(event):
    if _is_notify_state_event(event):
        return set()

    names = _get_event_names(event)
    names.update(_get_notify_names_from_object(_get_property(event, "notify")))
    return names


def _get_notify_state_names(event):
    if not _is_notify_state_event(event):
        return set()

    names = _get_event_names(event)
    names.update(_get_notify_names_from_object(_get_property(event, "notify_state_class")))
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

normal_notify_names = set()
notify_state_names = set()
for event in _get_notify_events(asset):
    normal_notify_names.update(_get_normal_notify_names(event))
    notify_state_names.update(_get_notify_state_names(event))

if not _names_match(normal_notify_names, "Footstep"):
    raise RuntimeError("Expected Footstep notify, found: {0}".format(sorted(normal_notify_names)))

if not _names_match(notify_state_names, "Window"):
    raise RuntimeError(
        "Expected Window notify state with notify_state_class or duration > 0, found: {0}".format(
            sorted(notify_state_names)
        )
    )

marker_names = {_get_marker_name(marker) for marker in _get_sync_markers(asset)}
if not _names_match(marker_names, "LeftFoot"):
    raise RuntimeError("Expected LeftFoot sync marker, found: {0}".format(sorted(marker_names)))

unreal.log("AnimSequence minimal/patch smoke passed")
