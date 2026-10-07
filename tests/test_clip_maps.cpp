#include "ClipMapAdapter.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool writeFixture(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

retrovdp::media::ClipMapDocument sampleDocument()
{
    using namespace retrovdp;
    media::ClipMapDocument document;
    document.name = "Dragon's Lair, Disc 1";
    document.mediaRoot = "media/disc 1";
    document.frameRate = media::Rational{30'000, 1'001};
    document.defaultRecipeLocator = "recipes/default.rvdp.json";
    document.defaultTarget = "tms9918a";
    document.entries = {
        {.id = "attract-1", .label = "Attract, opening", .startFrame = -35,
         .endFrame = 120, .sourceLocator = "opening.m2v",
         .audioLocator = "opening.ogg"},
        {.id = "death-1", .label = "Death \"one\"", .startFrame = 121,
         .sourceLocator = "death.m2v", .recipeLocator = "recipes/dark.rvdp.json",
         .target = "f18a"},
    };
    return document;
}

void testJsonRoundTrip(const QString& directory)
{
    using namespace retrovdp;
    const QString path = QDir(directory).filePath(QStringLiteral("map.json"));
    const auto written = appsupport::writeClipMap(
        sampleDocument(), path, appsupport::ClipMapFormat::RetroVdpJson);
    check(static_cast<bool>(written), "native JSON clip map should write");
    const auto loaded = appsupport::readClipMap(path);
    check(static_cast<bool>(loaded), "native JSON clip map should load");
    if (!loaded) return;
    check(loaded.document->entries.size() == 2, "JSON should preserve entry count");
    check(loaded.document->frameRate == media::Rational{30'000, 1'001},
          "JSON should preserve rational FPS");
    check(loaded.document->entries[0].startFrame == -35,
          "JSON should preserve signed frame numbers");
    check(loaded.document->entries[1].recipeLocator == "recipes/dark.rvdp.json",
          "JSON should preserve per-entry recipe override");
    const auto collision = appsupport::writeClipMap(
        sampleDocument(), path, appsupport::ClipMapFormat::RetroVdpJson);
    check(!collision && collision.errorCode == QStringLiteral("clip-map-output-exists"),
          "clip-map writer should reject an implicit overwrite");
    const auto overwritten = appsupport::writeClipMap(
        sampleDocument(), path, appsupport::ClipMapFormat::RetroVdpJson, true);
    check(static_cast<bool>(overwritten), "explicit clip-map overwrite should succeed");
}

void testDelimitedRoundTrip(const QString& directory)
{
    using namespace retrovdp;
    for (const auto [suffix, format] : {
             std::pair{QStringLiteral("csv"), appsupport::ClipMapFormat::Csv},
             std::pair{QStringLiteral("tsv"), appsupport::ClipMapFormat::Tsv}}) {
        const QString path = QDir(directory).filePath(QStringLiteral("map.") + suffix);
        const auto written = appsupport::writeClipMap(sampleDocument(), path, format);
        check(static_cast<bool>(written), "delimited clip map should write");
        const auto loaded = appsupport::readClipMap(path, format);
        check(static_cast<bool>(loaded), "delimited clip map should load");
        if (!loaded) continue;
        check(loaded.document->name == "Dragon's Lair, Disc 1",
              "delimited metadata should preserve commas");
        check(loaded.document->entries[0].label == "Attract, opening",
              "delimited rows should preserve escaped fields");
        check(loaded.document->entries[1].label == "Death \"one\"",
              "delimited rows should preserve escaped quotes");
    }
}

void testDaphneCompatibility(const QString& directory)
{
    using namespace retrovdp;
    const QString framefile = QDir(directory).filePath(QStringLiteral("game/framefile.txt"));
    QDir().mkpath(QFileInfo(framefile).absolutePath());
    check(writeFixture(framefile,
                       "../mpeg\r\n\r\n-35 opening.m2v\r\n5 opening.m2v ignored\r\n"),
          "Daphne fixture should write");
    const auto loaded = appsupport::readClipMap(
        framefile, appsupport::ClipMapFormat::DaphneFramefile,
        media::Rational{30'000, 1'001});
    check(static_cast<bool>(loaded), "Daphne framefile should load");
    if (!loaded) return;
    check(loaded.document->mediaRoot == "../mpeg", "Daphne media root should be preserved");
    check(loaded.document->entries.size() == 2, "Daphne blank lines should be ignored");
    check(loaded.document->entries[0].startFrame == -35,
          "Daphne negative preroll should be supported");
    check(loaded.document->entries[0].sourceLocator == "opening.m2v",
          "Daphne source should parse");
    check(loaded.document->entries[1].sourceLocator == "opening.m2v",
          "Daphne repeated source should be supported");
    const QString resolved = appsupport::resolveClipMapLocator(
        framefile, *loaded.document, loaded.document->entries[0].sourceLocator);
    check(resolved == QDir::cleanPath(QDir(QFileInfo(framefile).absolutePath())
                                         .absoluteFilePath(QStringLiteral("../mpeg/opening.m2v"))),
          "relative Daphne root should resolve from the framefile directory");

    const QString noFpsPath = QDir(directory).filePath(QStringLiteral("no-fps.txt"));
    check(writeFixture(noFpsPath, ".\n0 clip.m2v\n"), "no-FPS fixture should write");
    const auto noFps = appsupport::readClipMap(
        noFpsPath, appsupport::ClipMapFormat::DaphneFramefile);
    check(noFps && !noFps.document->frameRate && !noFps.warnings.isEmpty(),
          "Daphne import should warn rather than guess FPS");

    const QString badPath = QDir(directory).filePath(QStringLiteral("bad.txt"));
    check(writeFixture(badPath, ".\nfilename.m2v\n"), "invalid fixture should write");
    const auto bad = appsupport::readClipMap(
        badPath, appsupport::ClipMapFormat::DaphneFramefile);
    check(!bad && bad.errorCode == QStringLiteral("daphne-framefile-entry-invalid"),
          "Daphne entry without frame should be rejected");
}

void testMalformedNativeMap(const QString& directory)
{
    using namespace retrovdp;
    const QString path = QDir(directory).filePath(QStringLiteral("missing-frame.json"));
    check(writeFixture(path,
                       R"({"kind":"retrovdp-clip-map","schemaVersion":1,"entries":[{"id":"a","source":"a.m2v"}]})"),
          "malformed native fixture should write");
    const auto loaded = appsupport::readClipMap(
        path, appsupport::ClipMapFormat::RetroVdpJson);
    check(!loaded && loaded.errorCode == QStringLiteral("clip-map-frame-invalid"),
          "native entry without startFrame should be rejected");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory should be available");
    if (temporary.isValid()) {
        testJsonRoundTrip(temporary.path());
        testDelimitedRoundTrip(temporary.path());
        testDaphneCompatibility(temporary.path());
        testMalformedNativeMap(temporary.path());
    }
    if (failures == 0) std::cout << "Clip-map adapter validation passed.\n";
    return failures == 0 ? 0 : 1;
}
