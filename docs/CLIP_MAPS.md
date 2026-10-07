# Clip-map interchange

RetroVDP Studio uses one normalized clip-map document to exchange logical
frame mappings between extraction jobs, Daphne/Hypseus installations, batch
spreadsheets, and custom game-engine ports. Mapping conversion does not decode
media and does not require FFmpeg.

## Native JSON

The lossless format is a UTF-8 JSON object with `kind` set to
`retrovdp-clip-map` and `schemaVersion` set to `1`:

```json
{
  "kind": "retrovdp-clip-map",
  "schemaVersion": 1,
  "name": "Disc 1",
  "mediaRoot": "../media",
  "frameRate": { "numerator": 30000, "denominator": 1001 },
  "defaultRecipe": "recipes/default.rvdp.json",
  "defaultTarget": "tms9918a",
  "entries": [
    {
      "id": "opening",
      "label": "Opening",
      "startFrame": -35,
      "endFrame": 119,
      "source": "opening.m2v",
      "audio": "opening.ogg",
      "recipe": "",
      "target": ""
    }
  ]
}
```

`frameRate` may be `null` when the source format does not declare a rate.
Frame numbers are signed because real Daphne framefiles use negative logical
frames for preroll. `endFrame` is optional. Empty per-entry `recipe` and
`target` values inherit `defaultRecipe` and `defaultTarget`. Relative source
and audio locators resolve below `mediaRoot`; a relative `mediaRoot` resolves
from the mapping file's directory. Recipe locators remain declarative at this
stage and are not executed during mapping conversion.

Entry IDs must be nonempty and unique. Sources must be nonempty. Input is
limited to 16 MiB and 100,000 entries before unbounded allocation can occur.
Writers reject an existing destination unless `--overwrite` is explicit, and
commit through an atomic save file.

## CSV and TSV

CSV and TSV use the same columns:

```text
id,label,start_frame,end_frame,source,audio,recipe,target
```

`start_frame` and `source` are required. Missing IDs are generated as
`clip-0001`, `clip-0002`, and so on. Optional metadata records precede the
header and use two fields:

```text
#name,Disc 1
#media_root,../media
#frame_rate,30000/1001
#default_recipe,recipes/default.rvdp.json
#default_target,tms9918a
```

Fields use doubled-quote escaping and may contain delimiters, quotes, or line
breaks.

## Daphne and Hypseus framefiles

The adapter follows the framefile parser used by Hypseus, the maintained
Daphne fork:

```text
../media

-35 opening.m2v
120 room-01.m2v
980 opening.m2v
```

The first line is the media root. Each nonblank following line contains a
signed logical start frame and a whitespace-free media filename. Reusing the
same file at multiple logical frames is valid. Additional words after the
filename are ignored on import for compatibility.

Framefiles do not encode FPS. Import therefore leaves `frameRate` unset unless
the caller supplies `--mapping-fps`; it never guesses NTSC or PAL timing.
Daphne export can represent only `mediaRoot`, `startFrame`, and `source`, so
the CLI reports a warning when labels, end frames, audio, recipes, targets, or
FPS are omitted. A source name containing whitespace cannot be exported.

The compatibility behavior is grounded in the upstream
[Hypseus parser](https://github.com/DirtBagXon/hypseus-singe/blob/master/src/ldp-out/ldp-vldp.cpp),
its [framefile parser tests](https://github.com/DirtBagXon/hypseus-singe/blob/master/src/game/releasetest.cpp),
and its [`-framefile` command documentation](https://github.com/DirtBagXon/hypseus-singe/blob/master/doc/CmdLine.md).

## CLI conversion

Convert a framefile into lossless native JSON while attaching an explicit
rate:

```shell
retrovdp-cli --convert-clip-map framefile.txt \
  --clip-map-input-format daphne \
  --mapping-fps 30000/1001 \
  --clip-map-output-format json \
  --output clips.json --json
```

Formats are `auto`, `json`, `csv`, `tsv`, `daphne`, `hypseus`, and
`framefile`; the last three names select the same adapter. Auto detection uses
`.json`, `.csv`, and `.tsv`; other extensions are treated as framefiles.

## Unity and Godot

The native JSON is the engine-neutral exchange contract. Reference loaders
are provided in
[`examples/integrations/RetroVdpClipMap.cs`](../examples/integrations/RetroVdpClipMap.cs)
for Unity and
[`examples/integrations/retro_vdp_clip_map.gd`](../examples/integrations/retro_vdp_clip_map.gd)
for Godot 4. They validate the format marker, schema version, entry identity,
source fields, ranges, and rational frame rate before a port uses the data.
Media playback and logical-frame dispatch remain owned by the port.

## Current boundary

This slice imports, validates, resolves, and converts mapping documents. It
does not yet expand entries into extracted clip-package folders, probe ordered
VOB segments, infer matching Ogg files, or execute global/per-entry recipes.
Those operations will consume this normalized model in later media slices.
