# Target support thumbnails

The catalog in `targets.json` is the single data contract used by the target
support popup. Each target has one or more real-product thumbnail sources,
including source-page and licensing metadata. The popup prefers a bundled
`local` thumbnail when present, while retaining the source URL for provenance
and an optional browser link. If a local thumbnail is unavailable, the popup
falls back to the target initials and never requires the network for the
standardized hardware summary. Network image loading occurs only when the user
chooses **Load online** for a failed bundled thumbnail.

To populate the bundled files in a connected development environment, run
`powershell -ExecutionPolicy Bypass -File tools/fetch_target_thumbnails.ps1`.
The script only stages the referenced source images; keep the source-page and
license metadata with every redistributed thumbnail.

Chip and upgrade profiles use a real host product or board photograph and say
so in the `hardware` field. They are not presented as standalone consumer
consoles.
