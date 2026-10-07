using System;
using Newtonsoft.Json;

[Serializable]
public sealed class RetroVdpRational
{
    public long numerator;
    public long denominator = 1;
}

[Serializable]
public sealed class RetroVdpClipMapEntry
{
    public string id = "";
    public string label = "";
    public long startFrame;
    public long? endFrame;
    public string source = "";
    public string audio = "";
    public string recipe = "";
    public string target = "";
}

[Serializable]
public sealed class RetroVdpClipMap
{
    public string kind = "";
    public int schemaVersion;
    public string name = "";
    public string mediaRoot = "";
    public RetroVdpRational frameRate;
    public string defaultRecipe = "";
    public string defaultTarget = "";
    public RetroVdpClipMapEntry[] entries = Array.Empty<RetroVdpClipMapEntry>();

    public static RetroVdpClipMap Parse(string json)
    {
        if (json == null)
            throw new ArgumentNullException(nameof(json));

        var map = JsonConvert.DeserializeObject<RetroVdpClipMap>(json);
        if (map == null || map.kind != "retrovdp-clip-map" || map.schemaVersion != 1)
            throw new FormatException("Unsupported RetroVDP clip-map document.");
        if (map.frameRate != null &&
            (map.frameRate.numerator <= 0 || map.frameRate.denominator <= 0))
            throw new FormatException("Clip-map frameRate must be positive.");

        var ids = new System.Collections.Generic.HashSet<string>();
        foreach (var entry in map.entries ?? Array.Empty<RetroVdpClipMapEntry>())
        {
            if (entry == null || string.IsNullOrEmpty(entry.id) ||
                string.IsNullOrEmpty(entry.source) || !ids.Add(entry.id))
                throw new FormatException("Clip-map entries need unique IDs and sources.");
            if (entry.endFrame.HasValue && entry.endFrame.Value < entry.startFrame)
                throw new FormatException("A clip-map endFrame cannot precede startFrame.");
        }
        return map;
    }
}
