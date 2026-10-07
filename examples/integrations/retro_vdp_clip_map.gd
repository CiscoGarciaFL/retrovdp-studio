class_name RetroVdpClipMap
extends RefCounted

static func parse_text(text: String) -> Dictionary:
    var value = JSON.parse_string(text)
    if not value is Dictionary:
        push_error("RetroVDP clip map must be a JSON object.")
        return {}
    if value.get("kind") != "retrovdp-clip-map" or value.get("schemaVersion") != 1:
        push_error("Unsupported RetroVDP clip-map document.")
        return {}

    var frame_rate = value.get("frameRate")
    if frame_rate != null:
        if not frame_rate is Dictionary \
                or int(frame_rate.get("numerator", 0)) <= 0 \
                or int(frame_rate.get("denominator", 0)) <= 0:
            push_error("Clip-map frameRate must be a positive rational.")
            return {}

    var entries = value.get("entries", [])
    if not entries is Array:
        push_error("Clip-map entries must be an array.")
        return {}
    var ids := {}
    for entry in entries:
        if not entry is Dictionary:
            push_error("Every clip-map entry must be an object.")
            return {}
        var id := str(entry.get("id", ""))
        var source := str(entry.get("source", ""))
        if id.is_empty() or source.is_empty() or ids.has(id):
            push_error("Clip-map entries need unique IDs and sources.")
            return {}
        if entry.has("endFrame") and int(entry.endFrame) < int(entry.get("startFrame", 0)):
            push_error("A clip-map endFrame cannot precede startFrame.")
            return {}
        ids[id] = true
    return value
