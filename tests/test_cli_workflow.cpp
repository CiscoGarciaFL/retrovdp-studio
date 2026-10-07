#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>

namespace {

struct TestContext {
    int failures{};

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

struct RunResult {
    int exitCode{-1};
    QByteArray standardOutput;
    QByteArray standardError;
    bool completed{};
};

RunResult runCli(const QStringList& arguments)
{
    QProcess process;
    process.setProgram(QStringLiteral(RETROVDP_CLI_PATH));
    process.setArguments(arguments);
    process.start();
    const bool started = process.waitForStarted(15'000);
    const bool finished = started && process.waitForFinished(90'000);
    return {
        .exitCode = finished ? process.exitCode() : -1,
        .standardOutput = process.readAllStandardOutput(),
        .standardError = process.readAllStandardError(),
        .completed = finished && process.exitStatus() == QProcess::NormalExit,
    };
}

QJsonObject jsonResult(const RunResult& run)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(run.standardOutput.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
    return document.object();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    TestContext test;

    const QString source = QDir(QStringLiteral(RETROVDP_GOLDEN_DIR))
                               .filePath(QStringLiteral("source/tiny-rgba.png"));
    QTemporaryDir output;
    test.expect(QFileInfo::exists(QStringLiteral(RETROVDP_CLI_PATH)),
                "CLI executable should exist");
    test.expect(QFileInfo::exists(source), "CLI source fixture should exist");
    test.expect(output.isValid(), "CLI output directory should be available");

    const RunResult help = runCli({QStringLiteral("--help")});
    if (!help.completed || help.exitCode != 0) {
        std::cerr << "CLI help launch failed; executable=" << RETROVDP_CLI_PATH
                  << " exit=" << help.exitCode
                  << " stderr=" << help.standardError.toStdString() << '\n';
    }
    test.expect(help.completed && help.exitCode == 0
                    && help.standardOutput.contains("--input")
                    && help.standardOutput.contains("--recipe")
                    && help.standardOutput.contains("--target")
                    && help.standardOutput.contains("--format")
                    && help.standardOutput.contains("--check-media-tools")
                    && help.standardOutput.contains("--probe-media")
                    && help.standardOutput.contains("--convert-clip-map")
                    && help.standardOutput.contains("--conversion-workers")
                    && help.standardOutput.contains("sega-sms-vdp")
                    && help.standardOutput.contains("mode-4-sms-240-pal")
                    && help.standardOutput.contains("sega-genesis-vdp")
                    && help.standardOutput.contains("mode-5-genesis-h40"),
                "CLI help should describe its stable input, target, and export options");

    const RunResult mediaTools = runCli({
        QStringLiteral("--check-media-tools"),
        QStringLiteral("--ffmpeg-path"),
        QStringLiteral(RETROVDP_FFMPEG_FIXTURE_PATH),
        QStringLiteral("--ffprobe-path"),
        QStringLiteral(RETROVDP_FFPROBE_FIXTURE_PATH),
        QStringLiteral("--json"),
    });
    const QJsonObject mediaToolsJson = jsonResult(mediaTools);
    test.expect(mediaTools.completed && mediaTools.exitCode == 0
                    && mediaToolsJson.value(QStringLiteral("status"))
                        == QStringLiteral("ok")
                    && mediaToolsJson.value(QStringLiteral("ffmpeg"))
                           .toObject().value(QStringLiteral("available")).toBool()
                    && mediaToolsJson.value(QStringLiteral("ffprobe"))
                           .toObject().value(QStringLiteral("available")).toBool(),
                "CLI should validate explicit FFmpeg and FFprobe executables");

    const RunResult missingMediaTools = runCli({
        QStringLiteral("--check-media-tools"),
        QStringLiteral("--ffmpeg-path"),
        output.filePath(QStringLiteral("missing-ffmpeg")),
        QStringLiteral("--ffprobe-path"),
        output.filePath(QStringLiteral("missing-ffprobe")),
        QStringLiteral("--json"),
    });
    test.expect(missingMediaTools.completed && missingMediaTools.exitCode == 7
                    && jsonResult(missingMediaTools).value(QStringLiteral("code"))
                        == QStringLiteral("media-tools-unavailable"),
                "CLI should use the dependency exit code when media tools are missing");

    const RunResult mediaProbe = runCli({
        QStringLiteral("--probe-media"), source,
        QStringLiteral("--ffprobe-path"),
        QStringLiteral(RETROVDP_FFPROBE_FIXTURE_PATH),
        QStringLiteral("--json"),
    });
    const QJsonObject mediaProbeJson = jsonResult(mediaProbe);
    test.expect(mediaProbe.completed && mediaProbe.exitCode == 0
                    && mediaProbeJson.value(QStringLiteral("status"))
                        == QStringLiteral("ok")
                    && mediaProbeJson.value(QStringLiteral("kind"))
                        == QStringLiteral("media-probe")
                    && mediaProbeJson.value(QStringLiteral("durationUs")).toInteger()
                        == 5'055'000
                    && mediaProbeJson.value(QStringLiteral("videoStreams"))
                           .toArray().size() == 1
                    && mediaProbeJson.value(QStringLiteral("audioStreams"))
                           .toArray().size() == 1,
                "CLI should expose normalized FFprobe video and audio metadata as JSON");

    const QString extractedPackage = output.filePath(QStringLiteral("media-package"));
    const RunResult mediaExtraction = runCli({
        QStringLiteral("--extract-media"), source,
        QStringLiteral("--output"), extractedPackage,
        QStringLiteral("--ffmpeg-path"),
        QStringLiteral(RETROVDP_FFMPEG_FIXTURE_PATH),
        QStringLiteral("--ffprobe-path"),
        QStringLiteral(RETROVDP_FFPROBE_FIXTURE_PATH),
        QStringLiteral("--duration"), QStringLiteral("1"),
        QStringLiteral("--fps"), QStringLiteral("2"),
        QStringLiteral("--extract-sizing"), QStringLiteral("fit"),
        QStringLiteral("--extract-width"), QStringLiteral("320"),
        QStringLiteral("--extract-height"), QStringLiteral("240"),
        QStringLiteral("--audio"), QStringLiteral("wav"),
        QStringLiteral("--json"),
    });
    const QJsonObject mediaExtractionJson = jsonResult(mediaExtraction);
    test.expect(mediaExtraction.completed && mediaExtraction.exitCode == 0
                    && mediaExtractionJson.value(QStringLiteral("status"))
                        == QStringLiteral("ok")
                    && mediaExtractionJson.value(QStringLiteral("kind"))
                        == QStringLiteral("media-extraction")
                    && mediaExtractionJson.value(QStringLiteral("frames")).toInteger() == 2
                    && QFileInfo::exists(QDir(extractedPackage).filePath(
                        QStringLiteral("clip.json")))
                    && QFileInfo::exists(QDir(extractedPackage).filePath(
                        QStringLiteral("audio/source.wav"))),
                "CLI should extract an atomic frame and audio clip package");

    for (const QString& frameName : {QStringLiteral("frame_000001.png"),
                                     QStringLiteral("frame_000002.png")}) {
        const QString framePath = QDir(extractedPackage)
                                      .filePath(QStringLiteral("frames/%1").arg(frameName));
        QFile::remove(framePath);
        QFile::copy(source, framePath);
    }
    const QString convertedPackage = output.filePath(QStringLiteral("target-run"));
    const QString batchRecipePath = QDir(QStringLiteral(RETROVDP_FIXTURE_DIR))
                                        .filePath(QStringLiteral("screen-image-v1.rvdp.json"));
    const RunResult mediaConversion = runCli({
        QStringLiteral("--convert-media-clip"),
        QDir(extractedPackage).filePath(QStringLiteral("clip.json")),
        QStringLiteral("--recipe"), batchRecipePath,
        QStringLiteral("--output"), convertedPackage,
        QStringLiteral("--conversion-workers"), QStringLiteral("1"),
        QStringLiteral("--json"),
    });
    const QJsonObject mediaConversionJson = jsonResult(mediaConversion);
    if (!mediaConversion.completed || mediaConversion.exitCode != 0
        || mediaConversionJson.value(QStringLiteral("status"))
               == QStringLiteral("error")) {
        std::cerr << "CLI media conversion failure: exit=" << mediaConversion.exitCode
                  << " stdout=" << mediaConversion.standardOutput.toStdString()
                  << " stderr=" << mediaConversion.standardError.toStdString() << '\n';
    }
    test.expect(mediaConversion.completed && mediaConversion.exitCode == 0
                    && mediaConversionJson.value(QStringLiteral("kind"))
                        == QStringLiteral("media-conversion")
                    && mediaConversionJson.value(QStringLiteral("frames")).toInteger() == 2
                    && mediaConversionJson.value(QStringLiteral("workers")).toInt() == 1
                    && QFileInfo::exists(QDir(convertedPackage).filePath(
                        QStringLiteral("run.json")))
                    && QFileInfo::exists(QDir(convertedPackage).filePath(
                        QStringLiteral("clip.json")))
                    && QFileInfo::exists(QDir(convertedPackage).filePath(
                        QStringLiteral("previews/frame_000001.png")))
                    && QFileInfo::exists(QDir(convertedPackage).filePath(
                        QStringLiteral("native/frame_000001/frame_000001.TIAP"))),
                "CLI should apply one frozen recipe to native outputs and monitor previews");

    const QString daphneFramefile = QDir(QStringLiteral(RETROVDP_FIXTURE_DIR))
                                         .filePath(QStringLiteral("daphne_framefile.txt"));
    const QString nativeClipMap = output.filePath(QStringLiteral("clips.json"));
    const RunResult clipMapConversion = runCli({
        QStringLiteral("--convert-clip-map"), daphneFramefile,
        QStringLiteral("--clip-map-input-format"), QStringLiteral("daphne"),
        QStringLiteral("--mapping-fps"), QStringLiteral("30000/1001"),
        QStringLiteral("--clip-map-output-format"), QStringLiteral("json"),
        QStringLiteral("--output"), nativeClipMap,
        QStringLiteral("--json"),
    });
    const QJsonObject clipMapJson = jsonResult(clipMapConversion);
    QFile nativeClipMapFile(nativeClipMap);
    QJsonObject nativeClipMapDocument;
    if (nativeClipMapFile.open(QIODevice::ReadOnly)) {
        nativeClipMapDocument = QJsonDocument::fromJson(nativeClipMapFile.readAll()).object();
    }
    test.expect(clipMapConversion.completed && clipMapConversion.exitCode == 0
                    && clipMapJson.value(QStringLiteral("kind"))
                        == QStringLiteral("clip-map-conversion")
                    && clipMapJson.value(QStringLiteral("entries")).toInteger() == 3
                    && nativeClipMapDocument.value(QStringLiteral("kind"))
                        == QStringLiteral("retrovdp-clip-map")
                    && nativeClipMapDocument.value(QStringLiteral("frameRate")).toObject()
                           .value(QStringLiteral("numerator")).toInteger() == 30'000,
                "CLI should normalize Daphne mappings and attach explicit rational FPS");

    const QStringList validArguments{
        QStringLiteral("--input"), source,
        QStringLiteral("--output"), output.path(),
        QStringLiteral("--target"), QStringLiteral("tms9918a"),
        QStringLiteral("--mode"), QStringLiteral("bitmap-9918a"),
        QStringLiteral("--preset"), QStringLiteral("crisp-pixel-art"),
        QStringLiteral("--format"), QStringLiteral("raw"),
        QStringLiteral("--json"),
    };
    const RunResult first = runCli(validArguments);
    const QJsonObject firstJson = jsonResult(first);
    const QJsonArray files = firstJson.value(QStringLiteral("files")).toArray();
    bool filesExist = files.size() == 2;
    for (const auto& entry : files) {
        const QJsonObject file = entry.toObject();
        filesExist &= QFileInfo::exists(file.value(QStringLiteral("path")).toString())
            && file.value(QStringLiteral("bytes")).toInteger() > 0;
    }
    test.expect(first.completed && first.exitCode == 0
                    && firstJson.value(QStringLiteral("status")) == QStringLiteral("ok")
                    && firstJson.value(QStringLiteral("mode"))
                        == QStringLiteral("bitmap-9918a")
                    && firstJson.value(QStringLiteral("target"))
                        == QStringLiteral("tms9918a")
                    && firstJson.value(QStringLiteral("preset"))
                        == QStringLiteral("crisp-pixel-art")
                    && filesExist,
                "CLI should load, convert, export, and report generated files as JSON");

    const RunResult conflict = runCli(validArguments);
    const QJsonObject conflictJson = jsonResult(conflict);
    test.expect(conflict.completed && conflict.exitCode == 6
                    && conflictJson.value(QStringLiteral("status"))
                        == QStringLiteral("error")
                    && conflictJson.value(QStringLiteral("code"))
                        == QStringLiteral("output-conflict")
                    && !conflictJson.value(QStringLiteral("details"))
                            .toObject().value(QStringLiteral("conflicts")).toArray().isEmpty(),
                "CLI should reject overwrites with a machine-readable conflict");

    QStringList overwriteArguments = validArguments;
    overwriteArguments.push_back(QStringLiteral("--overwrite"));
    const RunResult overwrite = runCli(overwriteArguments);
    test.expect(overwrite.completed && overwrite.exitCode == 0
                    && jsonResult(overwrite).value(QStringLiteral("status"))
                        == QStringLiteral("ok"),
                "CLI should replace files only when --overwrite is explicit");

    const QStringList smsModes{
        QStringLiteral("mode-4-sms-192"),
        QStringLiteral("mode-4-sms-224"),
        QStringLiteral("mode-4-sms-240-pal"),
    };
    for (const QString& mode : smsModes) {
        const QString smsOutput = output.filePath(mode);
        const RunResult smsRun = runCli({
            QStringLiteral("--input"), source,
            QStringLiteral("--output"), smsOutput,
            QStringLiteral("--target"), QStringLiteral("sega-sms-vdp"),
            QStringLiteral("--mode"), mode,
            QStringLiteral("--preset"), QStringLiteral("crisp-pixel-art"),
            QStringLiteral("--format"), QStringLiteral("raw"),
            QStringLiteral("--json"),
        });
        const QJsonObject smsJson = jsonResult(smsRun);
        const QJsonArray smsFiles = smsJson.value(QStringLiteral("files")).toArray();
        bool smsFilesValid = smsFiles.size() == 4;
        for (const auto& entry : smsFiles) {
            const QJsonObject file = entry.toObject();
            smsFilesValid &= QFileInfo::exists(
                file.value(QStringLiteral("path")).toString())
                && file.value(QStringLiteral("bytes")).toInteger() > 0;
        }
        test.expect(smsRun.completed && smsRun.exitCode == 0
                        && smsJson.value(QStringLiteral("status"))
                            == QStringLiteral("ok")
                        && smsJson.value(QStringLiteral("target"))
                            == QStringLiteral("sega-sms-vdp")
                        && smsJson.value(QStringLiteral("mode")) == mode
                        && smsFilesValid,
                    "CLI should convert and export every Master System Mode 4 height");
    }

    const QString genesisOutput = output.filePath(QStringLiteral("genesis-h40"));
    const RunResult genesisRun = runCli({
        QStringLiteral("--input"), source,
        QStringLiteral("--output"), genesisOutput,
        QStringLiteral("--target"), QStringLiteral("sega-genesis-vdp"),
        QStringLiteral("--mode"), QStringLiteral("mode-5-genesis-h40"),
        QStringLiteral("--preset"), QStringLiteral("crisp-pixel-art"),
        QStringLiteral("--format"), QStringLiteral("raw"),
        QStringLiteral("--json"),
    });
    const QJsonObject genesisJson = jsonResult(genesisRun);
    const QJsonArray genesisFiles = genesisJson.value(QStringLiteral("files")).toArray();
    bool genesisFilesValid = genesisFiles.size() == 4;
    for (const auto& entry : genesisFiles) {
        const QJsonObject file = entry.toObject();
        genesisFilesValid &= QFileInfo::exists(
            file.value(QStringLiteral("path")).toString())
            && file.value(QStringLiteral("bytes")).toInteger() > 0;
    }
    test.expect(genesisRun.completed && genesisRun.exitCode == 0
                    && genesisJson.value(QStringLiteral("status"))
                        == QStringLiteral("ok")
                    && genesisJson.value(QStringLiteral("target"))
                        == QStringLiteral("sega-genesis-vdp")
                    && genesisJson.value(QStringLiteral("mode"))
                        == QStringLiteral("mode-5-genesis-h40")
                    && genesisFilesValid,
                "CLI should convert and export a Genesis Mode V H40 screen");

    const QString recipePath = output.filePath(QStringLiteral("batch.rvdp.json"));
    QFile recipeFile(recipePath);
    const QJsonObject recipe{
        {QStringLiteral("kind"), QStringLiteral("newconvert9918-recipe")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("workspace"), QStringLiteral("screen-image")},
        {QStringLiteral("source"), QJsonObject{{QStringLiteral("path"), source}}},
        {QStringLiteral("conversion"),
         QJsonObject{{QStringLiteral("mode"), 0},
                     {QStringLiteral("targetProfile"), QStringLiteral("tms9918a")},
                     {QStringLiteral("dither"), 0},
                     {QStringLiteral("scalingFilter"), 0},
                     {QStringLiteral("fillMode"), 0},
                     {QStringLiteral("backgroundColor"), QStringLiteral("#000000")},
                     {QStringLiteral("exportFormat"), 2}}},
    };
    const bool recipeWritten = recipeFile.open(QIODevice::WriteOnly)
        && recipeFile.write(QJsonDocument(recipe).toJson()) > 0;
    recipeFile.close();
    const QString recipeOutput = output.filePath(QStringLiteral("recipe-output"));
    const RunResult recipeRun = runCli({
        QStringLiteral("--recipe"), recipePath,
        QStringLiteral("--output"), recipeOutput,
        QStringLiteral("--json"),
    });
    const QJsonObject recipeJson = jsonResult(recipeRun);
    test.expect(recipeWritten && recipeRun.completed && recipeRun.exitCode == 0
                    && recipeJson.value(QStringLiteral("preset"))
                        == QStringLiteral("recipe")
                    && recipeJson.value(QStringLiteral("mode"))
                        == QStringLiteral("bitmap-9918a")
                    && recipeJson.value(QStringLiteral("target"))
                        == QStringLiteral("tms9918a")
                    && recipeJson.value(QStringLiteral("format"))
                        == QStringLiteral("raw"),
                "CLI should migrate a versioned recipe saved under the retired identity");

    const QString canonicalRecipe = QDir(QStringLiteral(RETROVDP_FIXTURE_DIR))
                                        .filePath(QStringLiteral("screen-image-v1.rvdp.json"));
    const QString canonicalOutput = output.filePath(QStringLiteral("canonical-output"));
    const RunResult canonicalRun = runCli({
        QStringLiteral("--recipe"), canonicalRecipe,
        QStringLiteral("--input"), source,
        QStringLiteral("--output"), canonicalOutput,
        QStringLiteral("--json"),
    });
    const QJsonObject canonicalJson = jsonResult(canonicalRun);
    test.expect(canonicalRun.completed && canonicalRun.exitCode == 0
                    && canonicalJson.value(QStringLiteral("preset"))
                        == QStringLiteral("recipe")
                    && canonicalJson.value(QStringLiteral("mode"))
                        == QStringLiteral("bitmap-9918a")
                    && canonicalJson.value(QStringLiteral("target"))
                        == QStringLiteral("tms9918a")
                    && canonicalJson.value(QStringLiteral("format"))
                        == QStringLiteral("raw"),
                "CLI should execute the canonical version-1 recipe fixture");

    const RunResult badMode = runCli({
        QStringLiteral("--input"), source,
        QStringLiteral("--output"), output.path(),
        QStringLiteral("--mode"), QStringLiteral("not-a-mode"),
        QStringLiteral("--json"),
    });
    test.expect(badMode.completed && badMode.exitCode == 2
                    && jsonResult(badMode).value(QStringLiteral("code"))
                        == QStringLiteral("unknown-mode"),
                "CLI should use the documented usage exit code for an unknown mode");

    const RunResult missingInput = runCli({
        QStringLiteral("--input"), output.filePath(QStringLiteral("missing.png")),
        QStringLiteral("--output"), output.path(),
        QStringLiteral("--json"),
    });
    test.expect(missingInput.completed && missingInput.exitCode == 3
                    && jsonResult(missingInput).value(QStringLiteral("code"))
                        == QStringLiteral("input-failed"),
                "CLI should use the documented input exit code when loading fails");

    if (test.failures == 0) {
        std::cout << "CLI workflow validation passed\n";
    }
    return test.failures == 0 ? 0 : 1;
}
