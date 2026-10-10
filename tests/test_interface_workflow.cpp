#include "AppPreferencesController.hpp"
#include "EditorProjectController.hpp"
#include "ImageInputController.hpp"
#include "MediaClipController.hpp"
#include "MediaBatchController.hpp"
#include "TargetSupportCatalog.hpp"

#include <QCoreApplication>
#include <QColor>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVariant>

#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>

namespace {

std::atomic<int> qmlBindingErrors{};
QtMessageHandler previousMessageHandler{};

void captureQmlMessages(QtMsgType type,
                        const QMessageLogContext& context,
                        const QString& message)
{
    if ((type == QtWarningMsg || type == QtCriticalMsg)
        && (message.contains(QStringLiteral("TypeError"))
            || message.contains(QStringLiteral("ReferenceError"))
            || message.contains(QStringLiteral("of null")))) {
        ++qmlBindingErrors;
    }
    if (previousMessageHandler != nullptr) {
        previousMessageHandler(type, context, message);
    } else if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        std::cerr << message.toStdString() << '\n';
    }
}

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

bool waitFor(const std::function<bool()>& predicate, int timeoutMilliseconds = 15'000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return predicate();
}

QString goldenSource(QStringView relative)
{
    return QDir(QStringLiteral(RETROVDP_GOLDEN_DIR)).filePath(relative.toString());
}

void testLiveWorkflow(TestContext& test, ImageInputController& controller)
{
    {
        ImageInputController blankController;
        blankController.newScreenImage();
        const QImage blankScreen = QImage::fromData(QByteArray::fromBase64(
            blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        bool allBlack = blankScreen.size() == QSize(256, 192);
        for (int y = 0; y < blankScreen.height() && allBlack; ++y) {
            for (int x = 0; x < blankScreen.width(); ++x) {
                if (blankScreen.pixelColor(x, y).rgb() != QColor(Qt::black).rgb()) {
                    allBlack = false;
                    break;
                }
            }
        }
        test.expect(blankController.hasConversion() && !blankController.hasImage()
                        && blankController.sourceName() == QStringLiteral("Untitled")
                        && blankController.screenImageBackgroundColor()
                            == QColor(Qt::black)
                        && !blankController.screenImageEdited() && allBlack
                        && blankController.sourceSpectrum16Colors().size() == 16
                        && !blankController.sourceSwatchColors().isEmpty()
                        && blankController.sourceUsedColors().contains(
                            QVariant::fromValue(QColor(Qt::black))),
                    "New should create an untitled chipset-valid 256x192 black Screen Image without a source file");
        blankController.setScreenImageForegroundColor(Qt::white);
        blankController.drawScreenImageLine(0.1, 0.1, 0.9, 0.1, 1, true);
        test.expect(blankController.screenImageEdited()
                        && blankController.canUndoScreenImage(),
                    "a new blank Screen Image should immediately support drawing and undo");

        ImageInputController yamahaController;
        yamahaController.setAutoUpdate(false);
        yamahaController.setTargetProfile(static_cast<int>(
            retrovdp::core::TargetProfileId::V9958));
        yamahaController.setConversionMode(static_cast<int>(
            retrovdp::core::ConversionMode::Screen12V9958));
        yamahaController.openUrl(QUrl::fromLocalFile(
            goldenSource(u"source/tiny-rgba.png")));
        const QImage yamahaSource = QImage::fromData(QByteArray::fromBase64(
            yamahaController.sourcePreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(yamahaSource.size() == QSize(256, 212)
                        && yamahaController.targetWidth() == 256
                        && yamahaController.targetHeight() == 212,
                    "V9958 source framing should use the selected mode's 256x212 geometry");
        yamahaController.updateConversion();
        const bool yamahaReady = waitFor([&] {
            return yamahaController.hasConversion() && !yamahaController.busy();
        }, 60'000);
        const QImage yamahaPreview = QImage::fromData(QByteArray::fromBase64(
            yamahaController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(yamahaReady && yamahaPreview.size() == QSize(256, 212)
                        && qAbs(yamahaController.targetPixelAspectRatio() - 1.0)
                            < 0.001
                        && yamahaController.conversionDetails().contains(
                            QStringLiteral("256×212"))
                        && yamahaController.conversionDetails().contains(
                            QStringLiteral("54272 target bytes")),
                    "V9958 Screen 12 should update a native-resolution YJK Screen Image");
        yamahaController.setConversionMode(static_cast<int>(
            retrovdp::core::ConversionMode::Screen7V9938));
        const QImage screen7Source = QImage::fromData(QByteArray::fromBase64(
            yamahaController.sourcePreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(screen7Source.size() == QSize(512, 212)
                        && yamahaController.targetWidth() == 512
                        && yamahaController.targetHeight() == 212
                        && qAbs(yamahaController.targetPixelAspectRatio() - 0.5)
                            < 0.001,
                    "512-pixel Yamaha modes should retain native pixels and half-width display aspect");
        yamahaController.setTargetProfile(static_cast<int>(
            retrovdp::core::TargetProfileId::Tms9918A));
        yamahaController.setAutoUpdate(true);

        ImageInputController selectionController;
        selectionController.newScreenImage();
        const QColor selectionColor(246, 52, 121);
        selectionController.setScreenImageForegroundColor(selectionColor);
        selectionController.beginScreenImageStroke(
            10.5 / 256.0, 10.5 / 192.0, 1, false, true, true);
        selectionController.endScreenImageStroke();
        selectionController.setScreenImageSelection(
            10.1 / 256.0, 10.1 / 192.0,
            10.1 / 256.0, 10.1 / 192.0);
        test.expect(selectionController.hasScreenImageSelection()
                        && selectionController.screenImageSelectionX() == 10
                        && selectionController.screenImageSelectionY() == 10
                        && selectionController.screenImageSelectionWidth() == 1
                        && selectionController.screenImageSelectionHeight() == 1,
                    "Screen Image selection should retain its exact inclusive pixel bounds");

        selectionController.copyScreenImage();
        selectionController.clearScreenImageSelection();
        const QString beforeFloatingPaste = selectionController.convertedPreview();
        selectionController.pasteScreenImage();
        test.expect(selectionController.screenImageFloating()
                        && !selectionController.screenImageFloatingMove()
                        && selectionController.screenImageFloatingWidth() == 1
                        && selectionController.screenImageFloatingHeight() == 1
                        && selectionController.convertedPreview() == beforeFloatingPaste,
                    "pasting a copied selection should float at its native size without changing the canvas");
        selectionController.placeScreenImageFloating(
            30.5 / 256.0, 40.5 / 192.0);
        const auto decodeSelectionPreview = [&selectionController] {
            return QImage::fromData(QByteArray::fromBase64(
                selectionController.convertedPreview()
                    .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        };
        QImage selectionPreview = decodeSelectionPreview();
        test.expect(!selectionController.screenImageFloating()
                        && selectionController.screenImageSelectionX() == 30
                        && selectionController.screenImageSelectionY() == 40
                        && selectionPreview.pixelColor(10, 10).rgb()
                            == selectionColor.rgb()
                        && selectionPreview.pixelColor(30, 40).rgb()
                            == selectionColor.rgb(),
                    "dropping a copied selection should paste it and keep the placed area selected");
        selectionController.undoScreenImage();
        test.expect(selectionController.convertedPreview() == beforeFloatingPaste,
                    "placing a copied Screen Image selection should undo as one edit");

        selectionController.setScreenImageSelection(
            10.1 / 256.0, 10.1 / 192.0,
            10.1 / 256.0, 10.1 / 192.0);
        selectionController.beginMoveScreenImageSelection();
        selectionController.cancelScreenImageFloating();
        test.expect(selectionController.convertedPreview() == beforeFloatingPaste,
                    "canceling a floating move should leave the Screen Image unchanged");
        selectionController.beginMoveScreenImageSelection();
        selectionController.placeScreenImageFloating(
            20.5 / 256.0, 20.5 / 192.0);
        selectionPreview = decodeSelectionPreview();
        test.expect(selectionPreview.pixelColor(10, 10).rgb()
                        == QColor(Qt::black).rgb()
                        && selectionPreview.pixelColor(20, 20).rgb()
                            == selectionColor.rgb(),
                    "moving a selection should clear its source only when the placement is dropped");
        selectionController.undoScreenImage();
        selectionPreview = decodeSelectionPreview();
        test.expect(selectionPreview.pixelColor(10, 10).rgb()
                        == selectionColor.rgb()
                        && selectionPreview.pixelColor(20, 20).rgb()
                            == QColor(Qt::black).rgb(),
                    "moving a Screen Image selection should undo as one edit");

        ImageInputController assetController;
        assetController.newScreenImage();
        assetController.setScreenImageForegroundColor(selectionColor);
        QVariantMap systemFont;
        for (const QVariant& candidate : assetController.screenImageFonts()) {
            if (candidate.toMap().value(QStringLiteral("kind"))
                    == QStringLiteral("system")) {
                systemFont = candidate.toMap();
                break;
            }
        }
        assetController.prepareScreenImageText(
            QStringLiteral("Type"), systemFont.value(QStringLiteral("key")).toString(), 16);
        test.expect(!systemFont.isEmpty(),
                    "Type should discover at least one system font");
        test.expect(assetController.screenImageFloating(),
                    "Type should prepare system text as floating placement art");
        test.expect(assetController.screenImageFloatingTopLeft()
                        && assetController.screenImageFloatingWidth() > 0
                        && assetController.screenImageFloatingHeight() > 0,
                    "Type should render a system font as transparent top-left-anchored placement art");
        assetController.cancelScreenImageFloating();

        const QString artistFontPath = QDir(assetController.tiArtistFontsPath())
            .filePath(QStringLiteral("INTERFACE-TEST-FONT"));
        QFile artistFontFile(artistFontPath);
        const bool artistFontWritten = artistFontFile.open(QIODevice::WriteOnly)
            && artistFontFile.write(
                "FONT:\nA\n1,1,8\n24,36,66,126,66,66,66,0\n") > 0;
        artistFontFile.close();
        assetController.reloadScreenImageFonts();
        QVariantMap artistFont;
        for (const QVariant& candidate : assetController.screenImageFonts()) {
            if (candidate.toMap().value(QStringLiteral("key")).toString()
                    == QStringLiteral("tia:") + artistFontPath) {
                artistFont = candidate.toMap();
                break;
            }
        }
        assetController.prepareScreenImageText(
            QStringLiteral("A"), artistFont.value(QStringLiteral("key")).toString(), 8);
        test.expect(artistFontWritten && !artistFont.isEmpty()
                        && artistFont.value(QStringLiteral("badge"))
                            == QStringLiteral("TIA")
                        && !artistFont.value(QStringLiteral("preview")).toString().isEmpty()
                        && assetController.screenImageFloatingWidth() == 8
                        && assetController.screenImageFloatingHeight() == 8,
                    "the app font library should discover and preview TI Artist FONT DV80 data");
        assetController.placeScreenImageFloating(32.0 / 256.0, 32.0 / 192.0);
        QImage assetPreview = QImage::fromData(QByteArray::fromBase64(
            assetController.convertedPreview().section(
                QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(assetPreview.pixelColor(35, 32).rgb() == selectionColor.rgb()
                        && assetPreview.pixelColor(32, 32).rgb()
                            == QColor(Qt::black).rgb(),
                    "TI Artist text should place from its top-left anchor with transparent gaps");
        assetController.undoScreenImage();
        QFile::remove(artistFontPath);
        assetController.reloadScreenImageFonts();

        QTemporaryDir clipArtDirectory(
            QDir::current().filePath(QStringLiteral("clip-art-test-XXXXXX")));
        QImage clipArtSource(2, 2, QImage::Format_RGBA8888);
        clipArtSource.fill(Qt::white);
        clipArtSource.setPixelColor(1, 1, Qt::black);
        const QString clipArtPath = clipArtDirectory.filePath(QStringLiteral("mark.png"));
        const bool clipArtWritten = clipArtSource.save(clipArtPath, "PNG");
        assetController.loadScreenImageClipArt(QUrl::fromLocalFile(clipArtPath));
        const QString processedClipArt = assetController.screenImageClipArtPreview(
            4, 4, 2, true, true);
        assetController.prepareScreenImageClipArt(4, 4, 2, true, true);
        test.expect(clipArtDirectory.isValid() && clipArtWritten
                        && assetController.screenImageClipArtSourceWidth() == 2
                        && assetController.screenImageClipArtSourceHeight() == 2
                        && !assetController.screenImageClipArtSourcePreview().isEmpty()
                        && !processedClipArt.isEmpty()
                        && assetController.screenImageFloating()
                        && !assetController.screenImageFloatingTopLeft()
                        && assetController.screenImageFloatingWidth() == 4
                        && assetController.screenImageFloatingHeight() == 4,
                    "Slide/ClipArt should preview resized B&W foreground/background mapping before placement");
        assetController.placeScreenImageFloating(50.0 / 256.0, 50.0 / 192.0);
        assetPreview = QImage::fromData(QByteArray::fromBase64(
            assetController.convertedPreview().section(
                QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(assetPreview.pixelColor(51, 51).rgb() == selectionColor.rgb()
                        && assetPreview.pixelColor(48, 48).rgb()
                            == QColor(Qt::black).rgb(),
                    "transparent mapped ClipArt should preserve the canvas behind its background pixels");

        const QColor outlineColor(237, 41, 99);
        const QColor fillColor(23, 211, 137);
        blankController.newScreenImage();
        blankController.setScreenImageForegroundColor(outlineColor);
        blankController.setScreenImageBackgroundColor(fillColor);
        blankController.drawScreenImageShape(
            16.5 / 256.0, 16.5 / 192.0, 47.5 / 256.0, 47.5 / 192.0,
            5, false, true, false, true);
        const QImage squareBrushRectangle = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(squareBrushRectangle.pixelColor(14, 14).rgb()
                        == outlineColor.rgb(),
                    "square brush should preserve a rectangle's sharp outer corner");

        blankController.newScreenImage();
        blankController.setScreenImageForegroundColor(outlineColor);
        blankController.setScreenImageBackgroundColor(fillColor);
        const double ellipseLeft = 32.5;
        const double ellipseTop = 32.5;
        const double ellipseRight = 95.5;
        const double ellipseBottom = 95.5;
        blankController.drawScreenImageShape(
            ellipseLeft / 256.0, ellipseTop / 192.0,
            ellipseRight / 256.0, ellipseBottom / 192.0,
            3, true, true, true, false);
        const QImage filledEllipse = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        const double ellipseCenterX = (ellipseLeft + ellipseRight) / 2.0;
        const double ellipseCenterY = (ellipseTop + ellipseBottom) / 2.0;
        const double ellipseRadiusX = (ellipseRight - ellipseLeft) / 2.0;
        const double ellipseRadiusY = (ellipseBottom - ellipseTop) / 2.0;
        bool ellipseInteriorCovered = !filledEllipse.isNull();
        for (int y = 0; y < filledEllipse.height() && ellipseInteriorCovered; ++y) {
            for (int x = 0; x < filledEllipse.width(); ++x) {
                const double dx = (x + 0.5 - ellipseCenterX) / ellipseRadiusX;
                const double dy = (y + 0.5 - ellipseCenterY) / ellipseRadiusY;
                if (dx * dx + dy * dy > 1.0) continue;
                const QRgb pixel = filledEllipse.pixelColor(x, y).rgb();
                if (pixel != outlineColor.rgb() && pixel != fillColor.rgb()) {
                    ellipseInteriorCovered = false;
                    break;
                }
            }
        }
        test.expect(ellipseInteriorCovered,
                    "filled ellipses should not leave source-colored seam pixels");

        blankController.newScreenImage();
        blankController.setScreenImageForegroundColor(outlineColor);
        blankController.drawScreenImageShape(
            ellipseLeft / 256.0, ellipseTop / 192.0,
            ellipseRight / 256.0, ellipseBottom / 192.0,
            1, true, true, false, false);
        const QImage circleOutline = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        bool circleHasNoAngularGaps = !circleOutline.isNull();
        constexpr double pi = 3.14159265358979323846;
        for (int step = 0; step < 1440 && circleHasNoAngularGaps; ++step) {
            const double angle = 2.0 * pi * step / 1440.0;
            const int expectedX = static_cast<int>(std::floor(
                ellipseCenterX + std::cos(angle) * ellipseRadiusX));
            const int expectedY = static_cast<int>(std::floor(
                ellipseCenterY + std::sin(angle) * ellipseRadiusY));
            bool foundOutline = false;
            for (int y = expectedY - 1; y <= expectedY + 1 && !foundOutline; ++y) {
                for (int x = expectedX - 1; x <= expectedX + 1; ++x) {
                    if (x >= 0 && y >= 0 && x < circleOutline.width()
                        && y < circleOutline.height()
                        && circleOutline.pixelColor(x, y).rgb()
                            == outlineColor.rgb()) {
                        foundOutline = true;
                        break;
                    }
                }
            }
            circleHasNoAngularGaps &= foundOutline;
        }
        test.expect(circleHasNoAngularGaps,
                    "hard-edge circle borders should remain continuous around the ellipse");

        blankController.newScreenImage();
        blankController.setScreenImageForegroundColor(outlineColor);
        blankController.drawScreenImageLine(
            17.5 / 256.0, 17.5 / 192.0, 22.5 / 256.0, 22.5 / 192.0,
            3, true, false);
        const QImage snappedLine = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        bool thickSnappedLineContained = !snappedLine.isNull();
        for (int y = 14; y <= 25 && thickSnappedLineContained; ++y) {
            for (int x = 14; x <= 25; ++x) {
                if (x >= 16 && x <= 23 && y >= 16 && y <= 23) continue;
                if (snappedLine.pixelColor(x, y).rgb() == outlineColor.rgb()) {
                    thickSnappedLineContained = false;
                    break;
                }
            }
        }
        test.expect(thickSnappedLineContained
                        && snappedLine.pixelColor(16, 16).rgb()
                            == outlineColor.rgb()
                        && snappedLine.pixelColor(23, 23).rgb()
                            == outlineColor.rgb(),
                    "character snapping should inset a thick line so its brush stays inside the cell");

        blankController.newScreenImage();
        blankController.setScreenImageForegroundColor(outlineColor);
        blankController.beginScreenImageStroke(
            40.5 / 256.0, 40.5 / 192.0, 1, false, true);
        blankController.continueScreenImageRay(80.5 / 256.0, 40.5 / 192.0);
        blankController.continueScreenImageRay(40.5 / 256.0, 80.5 / 192.0);
        blankController.endScreenImageStroke();
        const QImage screenRays = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(screenRays.pixelColor(60, 40).rgb() == outlineColor.rgb()
                        && screenRays.pixelColor(40, 60).rgb()
                            == outlineColor.rgb()
                        && screenRays.pixelColor(60, 60).rgb()
                            == QColor(Qt::black).rgb(),
                    "Screen Image Rays should keep the initial origin instead of chaining endpoints");
        blankController.undoScreenImage();
        const QImage undoneScreenRays = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(undoneScreenRays.pixelColor(60, 40).rgb()
                        == QColor(Qt::black).rgb()
                        && undoneScreenRays.pixelColor(40, 60).rgb()
                            == QColor(Qt::black).rgb(),
                    "a completed Rays session should create one undoable Screen Image change");

        const QColor firstSwapColor(21, 189, 93);
        const QColor secondSwapColor(231, 67, 145);
        blankController.newScreenImage();
        blankController.setScreenImageForegroundColor(firstSwapColor);
        blankController.drawScreenImageLine(
            20.5 / 256.0, 30.5 / 192.0, 40.5 / 256.0, 30.5 / 192.0,
            1, true);
        blankController.setScreenImageForegroundColor(secondSwapColor);
        blankController.drawScreenImageLine(
            60.5 / 256.0, 30.5 / 192.0, 80.5 / 256.0, 30.5 / 192.0,
            1, true);
        blankController.swapScreenImageColors(30.5 / 256.0, 30.5 / 192.0);
        const QImage swappedScreen = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(swappedScreen.pixelColor(30, 30).rgb()
                        == secondSwapColor.rgb()
                        && swappedScreen.pixelColor(70, 30).rgb()
                            == firstSwapColor.rgb()
                        && swappedScreen.pixelColor(100, 30).rgb()
                            == QColor(Qt::black).rgb()
                        && blankController.screenImageForegroundColor()
                            == firstSwapColor,
                    "Color Swap should exchange both exact colors and select the picked color");
        blankController.undoScreenImage();
        const QImage undoneColorSwap = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(undoneColorSwap.pixelColor(30, 30).rgb()
                        == firstSwapColor.rgb()
                        && undoneColorSwap.pixelColor(70, 30).rgb()
                            == secondSwapColor.rgb(),
                    "Color Swap should undo as one Screen Image edit");

        blankController.setScreenImageSelection(
            20.0 / 256.0, 29.0 / 192.0,
            40.0 / 256.0, 31.0 / 192.0);
        blankController.swapScreenImageColors(70.5 / 256.0, 30.5 / 192.0);
        const QImage selectionSwappedScreen = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(selectionSwappedScreen.pixelColor(30, 30).rgb()
                        == secondSwapColor.rgb()
                        && selectionSwappedScreen.pixelColor(70, 30).rgb()
                            == secondSwapColor.rgb()
                        && blankController.screenImageForegroundColor()
                            == secondSwapColor,
                    "Color Swap should sample the clicked color but change pixels only inside the active selection");
        blankController.undoScreenImage();
        const QImage undoneSelectionSwap = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(undoneSelectionSwap.pixelColor(30, 30).rgb()
                        == firstSwapColor.rgb()
                        && undoneSelectionSwap.pixelColor(70, 30).rgb()
                            == secondSwapColor.rgb(),
                    "a selection-limited Color Swap should undo as one Screen Image edit");

        blankController.invertScreenImage();
        const QImage selectionInvertedScreen = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        const QColor invertedFirstSwapColor(
            255 - firstSwapColor.red(), 255 - firstSwapColor.green(),
            255 - firstSwapColor.blue());
        test.expect(selectionInvertedScreen.pixelColor(30, 30).rgb()
                        == invertedFirstSwapColor.rgb()
                        && selectionInvertedScreen.pixelColor(70, 30).rgb()
                            == secondSwapColor.rgb(),
                    "Invert should change only pixels inside the active selection");
        blankController.undoScreenImage();
        const QImage undoneSelectionInvert = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(undoneSelectionInvert.pixelColor(30, 30).rgb()
                        == firstSwapColor.rgb()
                        && undoneSelectionInvert.pixelColor(70, 30).rgb()
                            == secondSwapColor.rgb(),
                    "a selection-limited Invert should undo as one Screen Image edit");

        const auto grayscale = [](QColor color) {
            const int gray = (77 * color.red() + 150 * color.green()
                              + 29 * color.blue() + 128) >> 8;
            return QColor(gray, gray, gray);
        };
        blankController.removeScreenImageColor();
        const QImage selectionGrayscaleScreen = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(selectionGrayscaleScreen.pixelColor(30, 30).rgb()
                        == grayscale(firstSwapColor).rgb()
                        && selectionGrayscaleScreen.pixelColor(70, 30).rgb()
                            == secondSwapColor.rgb(),
                    "Remove Color should grayscale only pixels inside the active selection");
        blankController.undoScreenImage();
        blankController.clearScreenImageSelection();
        blankController.removeScreenImageColor();
        const QImage fullGrayscaleScreen = QImage::fromData(
            QByteArray::fromBase64(blankController.convertedPreview()
                .section(QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(fullGrayscaleScreen.pixelColor(30, 30).rgb()
                        == grayscale(firstSwapColor).rgb()
                        && fullGrayscaleScreen.pixelColor(70, 30).rgb()
                            == grayscale(secondSwapColor).rgb(),
                    "Remove Color should grayscale the whole Screen Image when no selection is active");
        blankController.undoScreenImage();
    }

    controller.setPerceptualRedWeight(40);
    controller.setPerceptualGreenWeight(40);
    controller.setPerceptualBlueWeight(20);
    controller.restorePerceptualWeights();
    test.expect(controller.perceptualRedWeight() == 30
                    && controller.perceptualGreenWeight() == 52
                    && controller.perceptualBlueWeight() == 18,
                "perceptual weights should restore to the audited 30/52/18 defaults");
    controller.setDitherMode(1);
    test.expect(controller.errorDownLeft() == 3 && controller.errorDown() == 5
                    && controller.errorDownRight() == 1 && controller.errorRight() == 7
                    && controller.errorFarRight() == 0 && controller.errorDownTwo() == 0,
                "selecting a named dither preset should populate all six error weights");
    // Keep this end-to-end UI test fast in unoptimized developer builds. The
    // exhaustive non-zero shift search is covered independently by core tests.
    controller.applyPreset(1); // Pixel art: no dithering or color-shift search.
    controller.openUrl(QUrl::fromLocalFile(goldenSource(u"source/tiny-rgba.png")));
    test.expect(controller.hasImage(), "controller should load a source image");
    const QVariantList sourceColors = controller.sourceUsedColors();
    QSet<QRgb> uniqueSourceColors;
    bool validSourceColors = !sourceColors.isEmpty();
    const auto sourceColorLess = [](const QColor& left, const QColor& right) {
        const bool leftNeutral = left.hsvSaturation() <= 8;
        const bool rightNeutral = right.hsvSaturation() <= 8;
        if (leftNeutral != rightNeutral) return leftNeutral;
        if (leftNeutral) {
            if (left.value() != right.value()) return left.value() < right.value();
            return left.rgb() < right.rgb();
        }
        if (left.hsvHue() != right.hsvHue()) return left.hsvHue() < right.hsvHue();
        if (left.value() != right.value()) return left.value() < right.value();
        if (left.hsvSaturation() != right.hsvSaturation()) {
            return left.hsvSaturation() > right.hsvSaturation();
        }
        return left.rgb() < right.rgb();
    };
    bool sourceColorsSorted = true;
    QColor previousSourceColor;
    for (const QVariant& value : sourceColors) {
        const QColor color = value.value<QColor>();
        validSourceColors &= color.isValid();
        if (previousSourceColor.isValid()
            && sourceColorLess(color, previousSourceColor)) {
            sourceColorsSorted = false;
        }
        previousSourceColor = color;
        uniqueSourceColors.insert(color.rgb());
    }
    test.expect(validSourceColors
                    && uniqueSourceColors.size() == sourceColors.size()
                    && sourceColorsSorted,
                "source-image color choices should be unique and ordered by color");
    test.expect(waitFor([&] { return controller.hasConversion() && !controller.busy(); }),
                "loaded image should produce a debounced background preview");
    test.expect(controller.canReload(),
                "a file-backed source should enable Reload");
    const QString loadedSourceName = controller.sourceName();
    controller.reloadSource();
    test.expect(controller.hasImage() && controller.sourceName() == loadedSourceName
                    && waitFor([&] {
                           return controller.hasConversion() && !controller.busy();
                       }),
                "Reload should reopen the current source file");
    {
        ImageInputController drawingController;
        drawingController.setAutoUpdate(false);
        drawingController.openUrl(
            QUrl::fromLocalFile(goldenSource(u"source/tiny-rgba.png")));
        const QColor pencilColor(17, 34, 51);
        const QColor eraserColor(68, 85, 102);
        drawingController.setForegroundColor(pencilColor);
        drawingController.setBackgroundColor(eraserColor);
        const QString sourceBeforeDrawing = drawingController.sourcePreview();
        drawingController.beginSourceStroke(0.25, 0.5, 3, false);
        drawingController.continueSourceStroke(0.75, 0.5);
        test.expect(drawingController.sourcePreview() == sourceBeforeDrawing,
                    "active pencil stroke should defer source-preview regeneration");
        drawingController.endSourceStroke();
        const QString sourceAfterPencil = drawingController.sourcePreview();
        test.expect(sourceAfterPencil != sourceBeforeDrawing
                        && drawingController.sourceUsedColors().contains(
                            QVariant::fromValue(pencilColor))
                        && drawingController.canUndoDrawing()
                        && !drawingController.canRedoDrawing(),
                    "pencil stroke should paint the original source with the foreground color");
        drawingController.beginSourceStroke(0.5, 0.5, 3, true);
        drawingController.endSourceStroke();
        const QString sourceAfterEraser = drawingController.sourcePreview();
        test.expect(sourceAfterEraser != sourceAfterPencil
                        && drawingController.sourceUsedColors().contains(
                            QVariant::fromValue(eraserColor)),
                    "eraser stroke should paint the original source with the background color");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceAfterPencil
                        && drawingController.canUndoDrawing()
                        && drawingController.canRedoDrawing(),
                    "drawing undo should restore the image before the most recent stroke");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceBeforeDrawing
                        && !drawingController.canUndoDrawing()
                        && drawingController.canRedoDrawing(),
                    "each drawing stroke should be a separate undo transaction");
        drawingController.redoDrawing();
        drawingController.redoDrawing();
        test.expect(drawingController.sourcePreview() == sourceAfterEraser
                        && drawingController.canUndoDrawing()
                        && !drawingController.canRedoDrawing(),
                    "drawing redo should replay undone pencil and eraser strokes");
        drawingController.undoDrawing();
        drawingController.beginSourceStroke(0.5, 0.25, 2, false);
        drawingController.endSourceStroke();
        test.expect(!drawingController.canRedoDrawing(),
                    "a new drawing change should discard the redo branch");
        const QString sourceBeforeRectangle = drawingController.sourcePreview();
        drawingController.setForegroundColor(QColor(190, 40, 210));
        drawingController.drawSourceShape(0.1, 0.15, 0.9, 0.8, 2, false);
        const QString sourceAfterRectangle = drawingController.sourcePreview();
        test.expect(sourceAfterRectangle != sourceBeforeRectangle
                        && drawingController.canUndoDrawing(),
                    "rectangle tool should commit one drawing transaction");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceBeforeRectangle,
                    "rectangle drawing should be undoable as one change");
        drawingController.redoDrawing();
        test.expect(drawingController.sourcePreview() == sourceAfterRectangle,
                    "rectangle drawing should be redoable as one change");
        const QString sourceBeforeLine = drawingController.sourcePreview();
        drawingController.drawSourceLine(0.1, 0.1, 0.9, 0.8, 4, true);
        const QString sourceAfterLine = drawingController.sourcePreview();
        test.expect(sourceAfterLine != sourceBeforeLine
                        && drawingController.canUndoDrawing(),
                    "line tool should use the source-image brush and commit one change");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceBeforeLine,
                    "source-image lines should be undoable as one change");
        const QString sourceBeforeKLine = drawingController.sourcePreview();
        drawingController.beginSourceStroke(0.1, 0.1, 3, false, true);
        drawingController.continueSourceStroke(0.8, 0.1);
        drawingController.continueSourceStroke(0.8, 0.8);
        drawingController.endSourceStroke();
        test.expect(drawingController.sourcePreview() != sourceBeforeKLine,
                    "K-Line should draw connected source-image brush segments");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceBeforeKLine,
                    "a completed source-image K-Line should undo as one transaction");
        const QString sourceBeforeRays = drawingController.sourcePreview();
        drawingController.beginSourceStroke(0.25, 0.25, 3, false, true);
        drawingController.continueSourceRay(0.75, 0.25);
        drawingController.continueSourceRay(0.25, 0.75);
        drawingController.endSourceStroke();
        test.expect(drawingController.sourcePreview() != sourceBeforeRays,
                    "Rays should draw fixed-origin source-image brush segments");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceBeforeRays,
                    "a completed source-image Rays session should undo as one transaction");
        const QColor sourceSwapFirst(13, 177, 241);
        const QColor sourceSwapSecond(247, 91, 19);
        drawingController.setForegroundColor(sourceSwapFirst);
        drawingController.drawSourceLine(0.1, 0.9, 0.2, 0.9, 3, true);
        drawingController.setForegroundColor(sourceSwapSecond);
        drawingController.drawSourceLine(0.8, 0.9, 0.9, 0.9, 3, true);
        const QString sourceBeforeSwap = drawingController.sourcePreview();
        drawingController.swapSourceColors(0.15, 0.9);
        const QImage sourceAfterSwap = QImage::fromData(QByteArray::fromBase64(
            drawingController.sourcePreview().section(
                QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(sourceAfterSwap.pixelColor(38, 172).rgb()
                        == sourceSwapSecond.rgb()
                        && sourceAfterSwap.pixelColor(217, 172).rgb()
                            == sourceSwapFirst.rgb()
                        && drawingController.foregroundColor() == sourceSwapFirst,
                    "Source Color Swap should exchange exact visible colors and select the picked color");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceBeforeSwap,
                    "Source Color Swap should undo as one drawing edit");
        drawingController.setForegroundColor(QColor(30, 210, 120));
        const QColor shapeFillColor(15, 225, 95);
        drawingController.setBackgroundColor(shapeFillColor);
        drawingController.drawSourceShape(0.2, 0.2, 0.8, 0.75, 2, true,
                                          false, true);
        test.expect(drawingController.sourcePreview() != sourceAfterRectangle
                        && drawingController.sourceUsedColors().contains(
                            QVariant::fromValue(shapeFillColor)),
                    "ellipse tool should support a background-color fill");
        drawingController.drawSourceShape(0.25, 0.25, 0.75, 0.7, 3, false,
                                          true, false);
        test.expect(drawingController.canUndoDrawing(),
                    "hard-edge shapes should participate in drawing history");

        drawingController.reloadSource();
        const QString sourceBeforeAdvancedEdits = drawingController.sourcePreview();
        const QImage sourceBeforeAdvancedImage = QImage::fromData(
            QByteArray::fromBase64(sourceBeforeAdvancedEdits.section(
                QLatin1Char(','), 1).toLatin1()), "PNG");
        drawingController.setSourceSelection(0.1, 0.1, 0.2, 0.2);
        test.expect(drawingController.hasSourceSelection()
                        && drawingController.sourceSelectionX() == 25
                        && drawingController.sourceSelectionY() == 19
                        && drawingController.sourceSelectionWidth() == 27
                        && drawingController.sourceSelectionHeight() == 20,
                    "source selection should use the same normalized rectangle workflow as Screen Image");
        drawingController.invertSourceImage();
        const QImage sourceAfterSelectionInvert = QImage::fromData(
            QByteArray::fromBase64(drawingController.sourcePreview().section(
                QLatin1Char(','), 1).toLatin1()), "PNG");
        const QColor selectedBefore = sourceBeforeAdvancedImage.pixelColor(30, 25);
        const QColor selectedAfter = sourceAfterSelectionInvert.pixelColor(30, 25);
        test.expect(selectedAfter.red() == 255 - selectedBefore.red()
                        && selectedAfter.green() == 255 - selectedBefore.green()
                        && selectedAfter.blue() == 255 - selectedBefore.blue()
                        && sourceAfterSelectionInvert.pixelColor(0, 0).rgb()
                            == sourceBeforeAdvancedImage.pixelColor(0, 0).rgb(),
                    "source invert should affect only the active selection");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceBeforeAdvancedEdits,
                    "source selection transforms should be undoable");

        drawingController.clearSourceSelection();
        drawingController.mirrorSourceImage();
        const QImage mirroredSource = QImage::fromData(QByteArray::fromBase64(
            drawingController.sourcePreview().section(
                QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(mirroredSource.pixelColor(0, 0).rgb()
                        == sourceBeforeAdvancedImage.pixelColor(
                            sourceBeforeAdvancedImage.width() - 1, 0).rgb(),
                    "source mirror should reverse the source canvas horizontally");
        drawingController.undoDrawing();
        drawingController.flipSourceImage();
        const QImage flippedSource = QImage::fromData(QByteArray::fromBase64(
            drawingController.sourcePreview().section(
                QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(flippedSource.pixelColor(0, 0).rgb()
                        == sourceBeforeAdvancedImage.pixelColor(
                            0, sourceBeforeAdvancedImage.height() - 1).rgb(),
                    "source flip should reverse the source canvas vertically");
        drawingController.undoDrawing();

        drawingController.setSourceSelection(0.1, 0.1, 0.2, 0.2);
        drawingController.beginMoveSourceSelection();
        test.expect(drawingController.sourceFloating()
                        && drawingController.sourceFloatingMove()
                        && drawingController.sourceFloatingWidth() == 27
                        && drawingController.sourceFloatingHeight() == 20
                        && !drawingController.sourceFloatingPreview().isEmpty(),
                    "a source selection should become a movable floating image");
        drawingController.placeSourceFloating(0.75, 0.75);
        test.expect(!drawingController.sourceFloating()
                        && drawingController.sourceSelectionX() != 25
                        && drawingController.sourceSelectionY() != 19
                        && drawingController.sourcePreview() != sourceBeforeAdvancedEdits,
                    "placing a floating source selection should move it as one edit");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceBeforeAdvancedEdits,
                    "moving a source selection should be undoable as one edit");

        drawingController.reloadScreenImageFonts();
        const QVariantList sourceFonts = drawingController.sourceImageFonts();
        bool sourceFontsAreSystemOnly = !sourceFonts.isEmpty();
        for (const QVariant& entry : sourceFonts) {
            const QVariantMap font = entry.toMap();
            sourceFontsAreSystemOnly &= font.value(QStringLiteral("kind")).toString()
                    == QStringLiteral("system")
                && font.value(QStringLiteral("key")).toString().startsWith(
                    QStringLiteral("system:"));
        }
        test.expect(sourceFontsAreSystemOnly,
                    "source text should offer only Windows/system fonts");
        if (!sourceFonts.isEmpty()) {
            const QString fontKey = sourceFonts.front().toMap()
                                        .value(QStringLiteral("key")).toString();
            drawingController.prepareSourceText(QStringLiteral("A"), fontKey, 12);
            test.expect(drawingController.sourceFloating()
                            && drawingController.sourceFloatingTopLeft()
                            && !drawingController.sourceFloatingPreview().isEmpty(),
                        "system-font text should be prepared for placement on the source image");
            drawingController.placeSourceFloating(0.05, 0.05);
            test.expect(!drawingController.sourceFloating()
                            && drawingController.sourcePreview()
                                != sourceBeforeAdvancedEdits,
                        "prepared system-font text should be placeable on the source image");
            drawingController.undoDrawing();
        }

        drawingController.setSourceSelection(0.1, 0.1, 0.2, 0.2);
        drawingController.copySourceImage();
        drawingController.pasteSourceImage();
        test.expect(drawingController.sourceFloating()
                        && !drawingController.sourceFloatingMove()
                        && drawingController.sourceFloatingWidth() == 27
                        && drawingController.sourceFloatingHeight() == 20,
                    "source selection copy and paste should create a floating placement");
        drawingController.cancelSourceFloating();
        drawingController.clearSourceSelection();
        drawingController.setBackgroundColor(QColor(23, 45, 67));
        const QString sourceBeforeClear = drawingController.sourcePreview();
        drawingController.clearSourceImage();
        const QImage clearedSource = QImage::fromData(QByteArray::fromBase64(
            drawingController.sourcePreview().section(
                QLatin1Char(','), 1).toLatin1()), "PNG");
        test.expect(clearedSource.pixelColor(0, 0).rgb()
                        == QColor(23, 45, 67).rgb(),
                    "source clear should fill the canvas with the source background color");
        drawingController.undoDrawing();
        test.expect(drawingController.sourcePreview() == sourceBeforeClear,
                    "clearing the source canvas should be undoable");

        drawingController.reloadSource();
        test.expect(!drawingController.canUndoDrawing()
                        && !drawingController.canRedoDrawing(),
                    "loading a source should begin a fresh drawing history");
    }
    {
        const QString drawingPath = goldenSource(u"source/transparency-rgba.png");
        const QColor paintColor(251, 252, 253);

        ImageInputController softController;
        softController.setAutoUpdate(false);
        softController.openUrl(QUrl::fromLocalFile(drawingPath));
        softController.setForegroundColor(paintColor);
        const QImage sourceBefore = QImage::fromData(QByteArray::fromBase64(
            softController.sourcePreview().section(QLatin1Char(','), 1).toLatin1()), "PNG");
        softController.beginSourceStroke(0.5, 0.5, 8, false, false);
        softController.endSourceStroke();
        const QImage softImage = QImage::fromData(QByteArray::fromBase64(
            softController.sourcePreview().section(QLatin1Char(','), 1).toLatin1()), "PNG");

        ImageInputController hardController;
        hardController.setAutoUpdate(false);
        hardController.openUrl(QUrl::fromLocalFile(drawingPath));
        hardController.setForegroundColor(paintColor);
        hardController.beginSourceStroke(0.5, 0.5, 8, false, true);
        hardController.endSourceStroke();
        const QImage hardImage = QImage::fromData(QByteArray::fromBase64(
            hardController.sourcePreview().section(QLatin1Char(','), 1).toLatin1()), "PNG");

        bool softHasBlend = false;
        bool hardChanged = false;
        bool hardOnlyExact = true;
        for (int y = 0; y < sourceBefore.height(); ++y) {
            for (int x = 0; x < sourceBefore.width(); ++x) {
                const QRgb original = sourceBefore.pixel(x, y);
                const QRgb soft = softImage.pixel(x, y);
                const QRgb hard = hardImage.pixel(x, y);
                softHasBlend |= soft != original && soft != paintColor.rgba();
                if (hard != original) {
                    hardChanged = true;
                    hardOnlyExact &= hard == paintColor.rgba();
                }
            }
        }
        test.expect(!sourceBefore.isNull() && sourceBefore.size() == softImage.size()
                        && sourceBefore.size() == hardImage.size()
                        && softHasBlend && hardChanged && hardOnlyExact,
                    "hard-edge brush should remain an exact overwrite after source framing");

        const QColor fillColor(19, 213, 147);
        hardController.setForegroundColor(QColor(245, 31, 73));
        hardController.setBackgroundColor(fillColor);
        hardController.drawSourceShape(0.2, 0.2, 0.8, 0.8, 6,
                                       false, false, true);
        const QImage filledImage = QImage::fromData(QByteArray::fromBase64(
            hardController.sourcePreview().section(QLatin1Char(','), 1).toLatin1()), "PNG");
        bool fillInteriorIsExact = !filledImage.isNull();
        for (int y = 76; y < 116; ++y) {
            for (int x = 100; x < 156; ++x) {
                fillInteriorIsExact &= filledImage.pixelColor(x, y).rgb()
                    == fillColor.rgb();
            }
        }
        test.expect(fillInteriorIsExact,
                    "shape fill should be fully opaque with no source pixels leaking through");
        hardController.applyPreset(1);
        const QImage reframedFilledImage = QImage::fromData(QByteArray::fromBase64(
            hardController.sourcePreview().section(QLatin1Char(','), 1).toLatin1()), "PNG");
        bool fillSurvivesReframing = !reframedFilledImage.isNull();
        for (int y = 76; y < 116; ++y) {
            for (int x = 100; x < 156; ++x) {
                fillSurvivesReframing &= reframedFilledImage.pixelColor(x, y).rgb()
                    == fillColor.rgb();
            }
        }
        test.expect(fillSurvivesReframing,
                    "prepared-canvas drawing should remain exact when framing changes");
        hardController.updateConversion();
        test.expect(waitFor([&] {
                        return hardController.hasConversion() && !hardController.busy();
                    }),
                    "conversion should accept the already-prepared drawing canvas");
    }
    test.expect(controller.convertedPreview().startsWith(QStringLiteral("data:image/png;base64,")),
                "converted preview should be exposed as displayable PNG data");
    const auto previewPayload = controller.convertedPreview().section(QLatin1Char(','), 1);
    const QImage decodedPreview = QImage::fromData(
        QByteArray::fromBase64(previewPayload.toLatin1()), "PNG");
    test.expect(decodedPreview.size() == QSize(256, 192),
                "converted preview PNG should retain the target dimensions");
    const QString screenBeforeEdit = controller.convertedPreview();
    const QColor screenPencilColor(241, 37, 113);
    const QColor screenBackgroundColor(9, 43, 77);
    controller.setScreenImageForegroundColor(screenPencilColor);
    controller.setScreenImageBackgroundColor(screenBackgroundColor);
    controller.beginScreenImageStroke(0.2, 0.3, 5, false, true);
    controller.continueScreenImageStroke(0.8, 0.3);
    controller.endScreenImageStroke();
    const QString screenAfterStroke = controller.convertedPreview();
    test.expect(screenAfterStroke != screenBeforeEdit
                    && controller.canUndoScreenImage()
                    && !controller.canRedoScreenImage(),
                "Screen Image pencil strokes should create independent undo history");
    controller.undoScreenImage();
    test.expect(controller.convertedPreview() == screenBeforeEdit
                    && controller.canRedoScreenImage(),
                "Screen Image undo should restore the previous converted canvas");
    controller.redoScreenImage();
    test.expect(controller.convertedPreview() == screenAfterStroke,
                "Screen Image redo should restore the edited converted canvas");
    const QString screenBeforeLine = controller.convertedPreview();
    controller.drawScreenImageLine(0.1, 0.1, 0.9, 0.8, 4, true);
    test.expect(controller.convertedPreview() != screenBeforeLine,
                "Screen Image line tool should use the configured brush");
    controller.undoScreenImage();
    test.expect(controller.convertedPreview() == screenBeforeLine,
                "Screen Image line should be undoable as one change");
    const QString screenBeforeKLine = controller.convertedPreview();
    controller.beginScreenImageStroke(0.1, 0.1, 3, false, true);
    controller.continueScreenImageStroke(0.8, 0.1);
    controller.continueScreenImageStroke(0.8, 0.8);
    controller.endScreenImageStroke();
    test.expect(controller.convertedPreview() != screenBeforeKLine,
                "Screen Image K-Line should draw connected brush segments");
    controller.undoScreenImage();
    test.expect(controller.convertedPreview() == screenBeforeKLine,
                "a completed Screen Image K-Line should undo as one transaction");
    const QString screenBeforeApplyingRules = controller.convertedPreview();
    controller.applyScreenImageEdits();
    const QImage chipsetAppliedScreen = QImage::fromData(QByteArray::fromBase64(
        controller.convertedPreview().section(QLatin1Char(','), 1).toLatin1()), "PNG");
    bool bitmapRowsRespectTwoColors = !chipsetAppliedScreen.isNull();
    for (int y = 0; y < chipsetAppliedScreen.height(); ++y) {
        for (int tile = 0; tile < chipsetAppliedScreen.width() / 8; ++tile) {
            QSet<QRgb> colors;
            for (int x = tile * 8; x < tile * 8 + 8; ++x)
                colors.insert(chipsetAppliedScreen.pixel(x, y));
            bitmapRowsRespectTwoColors &= colors.size() <= 2;
        }
    }
    test.expect(bitmapRowsRespectTwoColors && !controller.screenImageEdited()
                    && controller.statusMessage().contains(
                        QStringLiteral("chipset rules"), Qt::CaseInsensitive),
                "Apply should rebuild the complete Screen Image under the active chipset rules");
    controller.undoScreenImage();
    test.expect(controller.convertedPreview() == screenBeforeApplyingRules
                    && controller.screenImageEdited(),
                "applying chipset rules should be one undoable Screen Image edit");
    controller.drawScreenImageShape(0.25, 0.2, 0.75, 0.8, 3,
                                    true, false, true);
    const QString screenAfterShape = controller.convertedPreview();
    test.expect(screenAfterShape != screenAfterStroke,
                "Screen Image ellipse should support soft edges and background fill");
    controller.nudgeScreenImage(1, 0);
    test.expect(controller.convertedPreview() != screenAfterShape,
                "Screen Image movement should shift pixels and fill the exposed edge");
    controller.undoScreenImage();
    controller.mirrorScreenImage();
    controller.mirrorScreenImage();
    test.expect(controller.convertedPreview() == screenAfterShape,
                "two horizontal Screen Image mirrors should restore the canvas");
    controller.flipScreenImage();
    controller.undoScreenImage();
    test.expect(controller.convertedPreview() == screenAfterShape,
                "Screen Image flip should be undoable");
    controller.copyScreenImage();
    controller.clearScreenImage();
    const QString clearedScreen = controller.convertedPreview();
    controller.pasteScreenImage();
    test.expect(controller.convertedPreview() == screenAfterShape
                    && controller.convertedPreview() != clearedScreen,
                "Screen Image copy and paste should replace the canvas as one edit");
    controller.undoScreenImage();
    test.expect(controller.convertedPreview() == clearedScreen,
                "pasting into Screen Image should participate in undo history");
    controller.undoScreenImage();
    controller.pickScreenImageColor(0.5, 0.5, true);
    test.expect(controller.screenImageForegroundColor().isValid(),
                "Screen Image eyedropper should update its foreground selector");
    controller.setExportFormat(2); // RAW tables
    QTemporaryDir editedScreenExport(
        QDir::current().filePath(QStringLiteral("edited-screen-export-XXXXXX")));
    controller.exportToDirectory(QUrl::fromLocalFile(editedScreenExport.path()));
    test.expect(editedScreenExport.isValid()
                    && QFileInfo::exists(editedScreenExport.filePath(
                        QStringLiteral("TINY-RGBA.TIAP")))
                    && QFileInfo::exists(editedScreenExport.filePath(
                        QStringLiteral("TINY-RGBA.TIAC"))),
                "edited Screen Image should rebuild valid hardware target tables for export");
    test.expect(controller.conversionDetails().contains(QStringLiteral("256×192")),
                "conversion details should state target dimensions");
    test.expect(!controller.outputSummary().isEmpty(),
                "successful conversion should provide a generated-file summary");

    const auto sourcePayload = controller.sourcePreview().section(QLatin1Char(','), 1);
    const QImage framedSource = QImage::fromData(
        QByteArray::fromBase64(sourcePayload.toLatin1()), "PNG");
    test.expect(framedSource.size() == QSize(256, 192),
                "source preview should show the exact framed conversion input");
    controller.setAutoUpdate(false);
    const QString previousConversion = controller.convertedPreview();
    const QString previousSource = controller.sourcePreview();
    controller.nudgeSource(1, 0);
    const auto paletteColors = controller.backgroundPaletteColors();
    controller.setBackgroundColor(QColor(paletteColors.at(3).toString()));
    test.expect(controller.conversionPending() && !controller.busy()
                    && controller.convertedPreview() == previousConversion
                    && controller.sourcePreview() != previousSource,
                "manual mode should reframe immediately without launching conversion");
    controller.pickBackgroundColor(0.5, 0.5);
    test.expect(paletteColors.contains(controller.backgroundColor().name().toUpper()),
                "eyedropper selection should be constrained to the conversion palette");
    controller.setBackgroundColor(QColor(paletteColors.at(1).toString()));
    controller.setPowerPaintFraming(true);
    const auto powerPaintPayload = controller.sourcePreview().section(QLatin1Char(','), 1);
    const QImage powerPaintSource = QImage::fromData(
        QByteArray::fromBase64(powerPaintPayload.toLatin1()), "PNG");
    test.expect(powerPaintSource.size() == QSize(256, 192)
                    && powerPaintSource.pixelColor(255, 191) == QColor(Qt::black),
                "PowerPaint framing should retain a 256x192 preview with a black padded border");
    controller.setPowerPaintFraming(false);
    controller.centerSource();
    controller.updateConversion();
    test.expect(waitFor([&] {
                    return controller.hasConversion() && !controller.busy()
                        && !controller.conversionPending();
                }),
                "manual Update should run the pending conversion");
    controller.setAutoUpdate(true);

    controller.setConversionMode(3);
    controller.setConversionMode(4);
    controller.setConversionMode(0);
    test.expect(waitFor([&] { return controller.hasConversion() && !controller.busy(); }),
                "rapid changes should settle on one current conversion");
    test.expect(controller.conversionMode() == 0
                    && controller.conversionDetails().contains(QStringLiteral("Bitmap 9918A")),
                "stale preview jobs should not replace the newest requested mode");

    controller.setConversionMode(8);
    const bool scanlineReady = waitFor(
        [&] { return controller.hasConversion() && !controller.busy(); }, 60'000);
    if (!scanlineReady) {
        std::cerr << "Scanline status: " << controller.statusMessage().toStdString()
                  << "; error: " << controller.errorMessage().toStdString()
                  << "; busy=" << controller.busy()
                  << "; pending=" << controller.conversionPending()
                  << "; auto=" << controller.autoUpdate()
                  << "; mode=" << controller.conversionMode() << '\n';
    }
    test.expect(scanlineReady,
                "scanline-palette mode should produce a background preview");
    test.expect(controller.scanlinePaletteAvailable()
                    && controller.palettePreview().startsWith(QStringLiteral("data:image/png;base64,")),
                "scanline-palette mode should expose its optional palette visualization");
    controller.setConversionMode(0);
    test.expect(waitFor([&] { return controller.hasConversion() && !controller.busy(); }),
                "controller should return to the requested export mode");

    const int previousErrorAccumulation = controller.errorAccumulationMode();
    const int changedErrorAccumulation = previousErrorAccumulation == 0 ? 1 : 0;
    const int previousOrderedMapSize = controller.orderedDitherMapSize();
    const int changedOrderedMapSize = previousOrderedMapSize == 2 ? 4 : 2;
    const int previousDitherMode = controller.ditherMode();
    const int previousErrorRight = controller.errorRight();
    const int changedErrorRight = previousErrorRight == 16 ? 15 : previousErrorRight + 1;
    const int previousPaletteSelection = controller.paletteSelectionMode();
    const int previousStaticColorCount = controller.scanlineStaticColorCount();
    const QColor previousWorkingColor(controller.workingPaletteColors().at(0).toString());
    controller.setGamma(1.4);
    controller.setErrorAccumulationMode(changedErrorAccumulation);
    controller.setOrderedDitherMapSize(changedOrderedMapSize);
    controller.setErrorRight(changedErrorRight);
    controller.setPerceptualRedWeight(31);
    controller.setPaletteSelectionMode(previousPaletteSelection == 0 ? 1 : 0);
    controller.setScanlineStaticColorCount(2);
    controller.setWorkingPaletteColor(0, QColor(QStringLiteral("#123456")));
    controller.setLivePreview(true);
    test.expect(controller.canUndo(), "changing a setting should enable undo");
    QSettings persisted;
    const double persistedGamma =
        persisted.value(QStringLiteral("conversion/gamma"), -1.0).toDouble();
    const int persistedErrorAccumulation =
        persisted.value(QStringLiteral("conversion/errorAccumulation"), -1).toInt();
    const int persistedOrderedMapSize =
        persisted.value(QStringLiteral("conversion/orderedDitherMapSize"), -1).toInt();
    const int persistedErrorRight =
        persisted.value(QStringLiteral("conversion/errorRight"), -1).toInt();
    const double persistedPerceptualRed =
        persisted.value(QStringLiteral("conversion/perceptualRedWeight"), -1.0).toDouble();
    const int persistedPaletteSelection =
        persisted.value(QStringLiteral("conversion/paletteSelection"), -1).toInt();
    const int persistedStaticColorCount =
        persisted.value(QStringLiteral("conversion/scanlineStaticColorCount"), -1).toInt();
    const QStringList persistedWorkingPalette =
        persisted.value(QStringLiteral("conversion/workingPalette")).toStringList();
    const bool persistedLivePreview =
        persisted.value(QStringLiteral("conversion/livePreview"), false).toBool();
    if (!qFuzzyCompare(persistedGamma, 1.4)) {
        std::cerr << "Persisted gamma was " << persistedGamma
                  << "; settings status " << persisted.status() << '\n';
    }
    test.expect(qFuzzyCompare(persistedGamma, 1.4)
                    && persistedErrorAccumulation == changedErrorAccumulation
                    && persistedOrderedMapSize == changedOrderedMapSize
                    && persistedErrorRight == changedErrorRight
                    && qFuzzyCompare(persistedPerceptualRed, 0.31)
                    && persistedPaletteSelection == controller.paletteSelectionMode()
                    && persistedStaticColorCount == 2
                    && persistedWorkingPalette.size() == 15
                    && persistedWorkingPalette.front() == QStringLiteral("#123456").toUpper()
                    && persistedLivePreview && controller.livePreview()
                    && controller.ditherMode() == 7,
                "advanced parity and live-preview settings should persist through QSettings");
    controller.undoSettings();
    test.expect(!qFuzzyCompare(controller.gamma(), 1.4)
                    && controller.errorAccumulationMode() == previousErrorAccumulation
                    && controller.orderedDitherMapSize() == previousOrderedMapSize
                    && controller.ditherMode() == previousDitherMode
                    && controller.errorRight() == previousErrorRight
                    && controller.paletteSelectionMode() == previousPaletteSelection
                    && controller.scanlineStaticColorCount() == previousStaticColorCount
                    && QColor(controller.workingPaletteColors().at(0).toString())
                        == previousWorkingColor,
                "undo should restore the prior advanced dithering settings");
    controller.setGamma(1.2);
    test.expect(controller.canUndo(),
                "a new settings change immediately after undo should create a new undo step");
    controller.undoSettings();
    test.expect(!qFuzzyCompare(controller.gamma(), 1.2),
                "the new post-undo settings step should be reversible");
    controller.setConversionMode(0);
    controller.applyPreset(1);
    test.expect(waitFor([&] { return controller.hasConversion() && !controller.busy(); }),
                "preview should settle after undo and preset changes");
}

void testExportWorkflow(TestContext& test, ImageInputController& controller)
{
    controller.setExportFormat(2); // RAW tables
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("interface-export-XXXXXX")));
    test.expect(directory.isValid(), "interface export test directory should be available");
    controller.exportToDirectory(QUrl::fromLocalFile(directory.path()));
    test.expect(QFileInfo::exists(directory.filePath(QStringLiteral("TINY-RGBA.TIAP")))
                    && QFileInfo::exists(directory.filePath(QStringLiteral("TINY-RGBA.TIAC"))),
                "interface export should write the complete generated manifest");
    controller.exportToDirectory(QUrl::fromLocalFile(directory.path()));
    test.expect(!controller.overwriteMessage().isEmpty(),
                "existing files should produce a clear overwrite prompt state");
    controller.cancelOverwrite();
    test.expect(controller.overwriteMessage().isEmpty(),
                "cancelled overwrite should clear the prompt without changing files");
}

void testApplicationPreferences(TestContext& test, ImageInputController& controller)
{
    AppPreferencesController preferences(&controller);
    test.expect(preferences.previewLayout() == 1
                    && preferences.sidePanelVisible()
                    && preferences.sidePanelMode() == 0
                    && preferences.restoreWindowGeometry()
                    && !preferences.rememberWorkspaceMode()
                    && preferences.rememberConversionSettings()
                    && preferences.defaultPreset() == 0
                    && preferences.defaultExportFormat() == 0
                    && preferences.conversionWorkers() == 0
                    && preferences.availableConversionWorkers() >= 1
                    && preferences.automaticConversionWorkers() >= 1
                    && preferences.ffmpegPath().isEmpty()
                    && preferences.ffprobePath().isEmpty()
                    && !preferences.mediaToolsReady(),
                "application preferences should begin with documented interface, behavior, and default values");

    preferences.setPreviewLayout(2);
    preferences.setSidePanelVisible(false);
    preferences.setSidePanelMode(1);
    preferences.setRestoreWindowGeometry(false);
    preferences.setRememberWorkspaceMode(true);
    preferences.setLastWorkspaceMode(2);
    preferences.setRememberConversionSettings(false);
    preferences.setDefaultPreset(3);
    preferences.setDefaultExportFormat(8);
    const int requestedWorkers = std::min(2, preferences.availableConversionWorkers());
    preferences.setConversionWorkers(requestedWorkers);
    preferences.setFfmpegPath(QStringLiteral("configured-ffmpeg"));
    preferences.setFfprobePath(QStringLiteral("configured-ffprobe"));
    preferences.saveWindowGeometry(80, 60, 1180, 740);

    AppPreferencesController restored(&controller);
    test.expect(restored.previewLayout() == 2
                    && !restored.sidePanelVisible()
                    && restored.sidePanelMode() == 1
                    && !restored.restoreWindowGeometry()
                    && restored.rememberWorkspaceMode()
                    && restored.lastWorkspaceMode() == 2
                    && !restored.rememberConversionSettings()
                    && restored.defaultPreset() == 3
                    && restored.defaultExportFormat() == 8
                    && restored.conversionWorkers() == requestedWorkers
                    && restored.ffmpegPath() == QStringLiteral("configured-ffmpeg")
                    && restored.ffprobePath() == QStringLiteral("configured-ffprobe")
                    && restored.hasWindowGeometry()
                    && restored.windowX() == 80 && restored.windowY() == 60
                    && restored.windowWidth() == 1180
                    && restored.windowHeight() == 740,
                "application preferences should round-trip view, workspace, geometry, behavior, and default choices through QSettings");

    restored.resetInterfaceSettings();
    test.expect(restored.previewLayout() == 1
                    && restored.sidePanelVisible()
                    && restored.sidePanelMode() == 0
                    && restored.restoreWindowGeometry()
                    && !restored.rememberWorkspaceMode()
                    && !restored.hasWindowGeometry()
                    && !restored.rememberConversionSettings()
                    && restored.conversionWorkers() == requestedWorkers
                    && restored.defaultPreset() == 3,
                "Reset Interface should reset only the interface preference section");

    controller.setAutoUpdate(false);
    controller.setLivePreview(true);
    restored.resetBehaviorSettings();
    test.expect(controller.autoUpdate() && !controller.livePreview()
                    && restored.rememberConversionSettings()
                    && restored.conversionWorkers() == 0
                    && restored.defaultPreset() == 3,
                "Reset Behavior should restore update, preview, and conversion-memory behavior without changing defaults");

    restored.resetDefaultSettings();
    test.expect(restored.defaultPreset() == 0
                    && restored.defaultExportFormat() == 0,
                "Reset Defaults should restore only startup conversion and export defaults");

    restored.resetMediaToolSettings();
    test.expect(restored.ffmpegPath().isEmpty()
                    && restored.ffprobePath().isEmpty()
                    && !restored.mediaToolsReady(),
                "Reset Media Tools should restore automatic executable detection");

    restored.setRememberConversionSettings(false);
    restored.setDefaultPreset(1);
    restored.setDefaultExportFormat(8);
    controller.setGamma(2.2);
    restored.applyStartupPreferences();
    test.expect(controller.ditherMode() == 0
                    && qFuzzyCompare(controller.gamma(), 1.0)
                    && controller.exportFormat() == 8,
                "disabling remembered conversion settings should apply the selected startup preset and export format");
    restored.restoreAllDefaults();
}

void testEditorProjectRecipe(TestContext& test)
{
    ImageInputController startupImage;
    startupImage.setAutoUpdate(false);
    startupImage.setTargetProfile(static_cast<int>(
        retrovdp::core::TargetProfileId::SegaGenesis));
    EditorProjectController startupProject(&startupImage);
    const QVariantList startupTargets = startupProject.supportedTargets();
    test.expect(startupProject.activeTarget()
                        == static_cast<int>(retrovdp::core::TargetProfileId::Tms9918A)
                    && startupTargets.size() == 2
                    && startupTargets.at(0).toMap().value(QStringLiteral("id")).toString()
                        == QStringLiteral("tms9918a"),
                "a new project should replace a restored target outside its selected target set with a valid default");

    ImageInputController fixtureImage;
    fixtureImage.setAutoUpdate(false);
    EditorProjectController fixtureProject(&fixtureImage);
    const QString fixturePath = QDir(QStringLiteral(RETROVDP_FIXTURE_DIR))
                                    .filePath(QStringLiteral("screen-image-v1.rvdp.json"));
    const bool fixtureLoaded = fixtureProject.loadRecipe(QUrl::fromLocalFile(fixturePath));
    test.expect(fixtureLoaded
                    && fixtureProject.projectName() == QStringLiteral("Fixture Project")
                    && fixtureProject.workspaceMode() == 0
                    && fixtureProject.previewTarget() == 0
                    && fixtureImage.targetProfile() == 0
                    && fixtureImage.conversionMode() == 0
                    && qFuzzyCompare(fixtureImage.gamma(), 1.25)
                    && fixtureImage.exportFormat() == 2,
                "GUI-facing project services should load the canonical version-1 recipe fixture");
    fixtureProject.configureProject(QStringLiteral("F18A Campaign"), false, true);
    const QVariantMap f18aTargetInfo = fixtureProject.activeTargetInfo();
    test.expect(fixtureProject.projectName() == QStringLiteral("F18A Campaign")
                    && !fixtureProject.tms9918aEnabled()
                    && fixtureProject.f18aEnabled()
                    && fixtureProject.activeTarget() == 1
                    && fixtureProject.supportedTargets().size() == 1
                    && fixtureProject.screenImageModeAvailable()
                    && fixtureProject.characterModeAvailable()
                    && fixtureProject.spriteModeAvailable()
                    && fixtureProject.editScope() == 1
                    && f18aTargetInfo.value(QStringLiteral("name")).toString()
                        == QStringLiteral("F18A")
                    && f18aTargetInfo.value(
                           QStringLiteral("spritePerItemSize")).toBool()
                    && f18aTargetInfo.value(
                           QStringLiteral("spriteMaximumColorDepth")).toInt() == 3,
                "project configuration should own its name, target set, active target, and mode capabilities");
    fixtureProject.createProject(QStringLiteral("Fresh Campaign"), true, false);
    test.expect(fixtureProject.projectName() == QStringLiteral("Fresh Campaign")
                    && fixtureProject.tms9918aEnabled()
                    && !fixtureProject.f18aEnabled()
                    && fixtureProject.activeTarget() == 0
                    && fixtureProject.editScope() == 0
                    && fixtureProject.workspaceMode() == 0
                    && fixtureProject.recipePath().isEmpty(),
                "New Project should reset project state and select a configured target");

    ImageInputController smsImage;
    smsImage.setAutoUpdate(false);
    EditorProjectController smsProject(&smsImage);
    smsProject.configureProjectWithTargets(
        QStringLiteral("Master System Campaign"), false, false,
        QStringList{QStringLiteral("sega-sms-vdp")});
    smsProject.setSpriteGlobalSize(16);
    const QVariantMap smsTargetInfo = smsProject.activeTargetInfo();
    test.expect(smsProject.segaSmsEnabled()
                    && smsProject.activeTarget()
                        == static_cast<int>(retrovdp::core::TargetProfileId::SegaMasterSystem)
                    && smsProject.supportedTargets().size() == 1,
                "a project should activate the implemented Master System target by itself");
    test.expect(retrovdp::core::supportsConversionMode(
                    retrovdp::core::TargetProfileId::SegaMasterSystem,
                    static_cast<retrovdp::core::ConversionMode>(smsImage.conversionMode())),
                "an activated Master System target should retain a compatible conversion mode");
    test.expect(smsTargetInfo.value(QStringLiteral("characterPatternsPerSet")).toInt()
                        == 448
                    && smsTargetInfo.value(QStringLiteral("spriteMaximumVisible")).toInt()
                        == 64
                    && smsTargetInfo.value(QStringLiteral("spriteMaximumColorDepth")).toInt()
                        == 4
                    && smsProject.editScope() == 1
                    && smsProject.characterSetNames().size() == 1
                    && smsProject.spritePatternsPerSet() == 64
                    && smsProject.spritePatternWidth(16) == 8
                    && smsProject.spritePatternHeight(16) == 16
                    && smsProject.activeSpriteColorDepth() == 4
                    && !smsProject.canRotateSpritePattern(),
                "Master System target metadata should expose its tile and sprite limits");
    smsProject.setCharacterForegroundColorIndex(7);
    smsProject.paintCharacterPixel(0, 447, 3, 5, true);
    const QVariantList smsRows = smsProject.characterPatternRows(0, 447);
    test.expect(smsRows.size() == 8
                    && smsRows[3].toMap().value(QStringLiteral("indexed")).toBool()
                    && smsRows[3].toMap().value(QStringLiteral("pixels"))
                           .toList()[5].toInt() == 7,
                "Master System character editing should retain native 4bpp indexes across all 448 slots");
    smsProject.setSpriteDrawingColorIndex(15);
    smsProject.paintSpritePixel(0, 63, 16, 15, 7, true);
    const QVariantList smsSprite = smsProject.spritePatternPixels(0, 63, 16);
    test.expect(smsSprite.size() == 128 && smsSprite[127].toInt() == 15,
                "Master System sprite editing should use 64 rectangular 8x16 4bpp entries");

    ImageInputController genesisImage;
    genesisImage.setAutoUpdate(false);
    EditorProjectController genesisProject(&genesisImage);
    genesisProject.configureProjectWithTargets(
        QStringLiteral("Genesis Campaign"), false, false,
        QStringList{QStringLiteral("sega-genesis-vdp")});
    const QVariantMap genesisTargetInfo = genesisProject.activeTargetInfo();
    test.expect(genesisProject.segaGenesisEnabled()
                    && genesisProject.activeTarget()
                        == static_cast<int>(retrovdp::core::TargetProfileId::SegaGenesis)
                    && genesisProject.supportedTargets().size() == 1
                    && genesisProject.characterPatternsPerSet() == 2048
                    && genesisProject.characterMapColumns() == 40
                    && genesisProject.characterMapRows() == 28
                    && genesisProject.spritePatternsPerSet() == 80
                    && genesisTargetInfo.value(QStringLiteral("characterColorDepth")).toInt()
                        == 4
                    && genesisTargetInfo.value(
                           QStringLiteral("characterPaletteBankCount")).toInt() == 4
                    && genesisTargetInfo.value(
                           QStringLiteral("spritePaletteBankCount")).toInt() == 4
                    && genesisTargetInfo.value(
                           QStringLiteral("spriteMaximumPerScanline")).toInt() == 20,
                "a Genesis project should expose native H40 character, palette, and sprite limits");
    genesisImage.setConversionMode(static_cast<int>(
        retrovdp::core::ConversionMode::Mode5GenesisH32));
    const QVariantMap genesisH32Info = genesisProject.activeTargetInfo();
    test.expect(genesisProject.characterMapColumns() == 32
                    && genesisProject.characterMapRows() == 28
                    && genesisH32Info.value(
                           QStringLiteral("characterMapColumns")).toInt() == 32
                    && genesisProject.spritePatternsPerSet() == 64
                    && genesisH32Info.value(
                           QStringLiteral("spriteMaximumPerScanline")).toInt() == 16,
                "Genesis H32 editor limits should follow the active display mode");
    genesisImage.setConversionMode(static_cast<int>(
        retrovdp::core::ConversionMode::Mode5GenesisH40));
    genesisProject.setCharacterPaletteBank(3);
    genesisProject.setCharacterForegroundColorIndex(14);
    genesisProject.paintCharacterPixel(0, 2047, 7, 7, true);
    const QVariantList genesisRows = genesisProject.characterPatternRows(0, 2047);
    test.expect(genesisProject.characterPaletteBank() == 3
                    && genesisRows.size() == 8
                    && genesisRows[7].toMap().value(QStringLiteral("indexed")).toBool()
                    && genesisRows[7].toMap().value(QStringLiteral("pixels"))
                           .toList()[7].toInt() == 14,
                "Genesis character editing should retain 4bpp indexes through all 2048 tile slots");
    genesisProject.setActiveCharacterPlane(1);
    genesisProject.setActiveCharacterPattern(12);
    genesisProject.moveCharacterTile(genesisProject.activeCharacterEditor(), 8, 16);
    genesisProject.setActiveCharacterTilePalette(2);
    genesisProject.setActiveCharacterTileFlipX(true);
    genesisProject.setActiveCharacterTilePriority(true);
    genesisProject.setActiveCharacterPlane(2);
    genesisProject.setActiveCharacterPattern(13);
    genesisProject.moveCharacterTile(genesisProject.activeCharacterEditor(), 16, 8);
    genesisProject.setActiveCharacterTilePalette(1);
    genesisProject.setActiveCharacterTileFlipY(true);
    genesisProject.setGenesisCompositePreview(true);
    const QVariantList genesisMapSlots = genesisProject.characterEditorSlots();
    test.expect(genesisMapSlots.size() == 3
                    && genesisMapSlots.at(1).toMap().value(QStringLiteral("plane")).toInt() == 1
                    && genesisMapSlots.at(1).toMap().value(QStringLiteral("palette")).toInt() == 2
                    && genesisMapSlots.at(1).toMap().value(QStringLiteral("flipX")).toBool()
                    && genesisMapSlots.at(1).toMap().value(QStringLiteral("priority")).toBool()
                    && genesisMapSlots.at(2).toMap().value(QStringLiteral("plane")).toInt() == 2
                    && genesisMapSlots.at(2).toMap().value(QStringLiteral("flipY")).toBool(),
                "Genesis character tiling should author Plane A, Plane B, and Window cells with native attributes");
    genesisProject.setActiveSprite(79);
    genesisProject.setActiveSpriteSize(2432);
    genesisProject.setActiveSpritePaletteBank(2);
    genesisProject.setActiveSpritePriority(true);
    genesisProject.setSpriteDrawingColorIndex(15);
    genesisProject.paintSpritePixel(0, 79, 2432, 31, 23, true);
    const QVariantList genesisSprite = genesisProject.spritePatternPixels(0, 79, 2432);
    test.expect(genesisProject.spritePatternWidth(2432) == 24
                    && genesisProject.spritePatternHeight(2432) == 32
                    && genesisProject.activeSpriteColorDepth() == 4
                    && genesisProject.activeSpritePaletteBank() == 2
                    && !genesisProject.canRotateSpritePattern()
                    && genesisSprite.size() == 768
                    && genesisSprite[767].toInt() == 15,
                "Genesis sprite editing should support all rectangular 8-to-32-pixel 4bpp sizes");

    QTemporaryDir genesisDirectory(
        QDir::current().filePath(QStringLiteral("genesis-recipe-XXXXXX")));
    const QString genesisRecipe = genesisDirectory.filePath(
        QStringLiteral("genesis-roundtrip.rvdp.json"));
    const bool genesisSaved = genesisDirectory.isValid()
        && genesisProject.saveRecipe(QUrl::fromLocalFile(genesisRecipe));
    genesisProject.configureProject(QStringLiteral("Changed"), true, false);
    const bool genesisLoaded = genesisProject.loadRecipe(
        QUrl::fromLocalFile(genesisRecipe));
    const QVariantList restoredGenesisRows = genesisProject.characterPatternRows(0, 2047);
    const QVariantList restoredGenesisSprite = genesisProject.spritePatternPixels(
        0, 79, 2432);
    test.expect(genesisSaved && genesisLoaded
                    && genesisProject.projectName() == QStringLiteral("Genesis Campaign")
                    && genesisProject.segaGenesisEnabled()
                    && !genesisProject.tms9918aEnabled()
                    && genesisProject.activeCharacterPlane() == 2
                    && genesisProject.activeCharacterTilePalette() == 1
                    && genesisProject.activeCharacterTileFlipY()
                    && genesisProject.genesisCompositePreview()
                    && genesisProject.activeSprite() == 79
                    && genesisProject.activeSpriteSize() == 2432
                    && genesisProject.activeSpritePaletteBank() == 2
                    && genesisProject.activeSpritePriority()
                    && restoredGenesisRows[7].toMap()
                           .value(QStringLiteral("pixels")).toList()[7].toInt() == 14
                    && restoredGenesisSprite.size() == 768
                    && restoredGenesisSprite[767].toInt() == 15,
                "Genesis character and sprite data should round-trip through project recipes");

    const bool genesisAssetsExported = genesisProject.exportGenesisCharacterAssets(
        QUrl::fromLocalFile(genesisDirectory.path()));
    QFile planeB(genesisDirectory.filePath(
        QStringLiteral("GENESIS_CAMPAIGN.PLANE_B.MAP")));
    QFile windowMap(genesisDirectory.filePath(
        QStringLiteral("GENESIS_CAMPAIGN.WINDOW.MAP")));
    const bool mapsOpened = planeB.open(QIODevice::ReadOnly)
        && windowMap.open(QIODevice::ReadOnly);
    const QByteArray planeBBytes = mapsOpened ? planeB.readAll() : QByteArray{};
    const QByteArray windowBytes = mapsOpened ? windowMap.readAll() : QByteArray{};
    const int planeBOffset = (2 * 64 + 1) * 2;
    const int windowOffset = (1 * 64 + 2) * 2;
    test.expect(genesisAssetsExported && mapsOpened
                    && QFileInfo(genesisDirectory.filePath(
                           QStringLiteral("GENESIS_CAMPAIGN.TILES"))).size() == 65536
                    && planeBBytes.size() == 4096 && windowBytes.size() == 4096
                    && static_cast<unsigned char>(planeBBytes.at(planeBOffset)) == 0xc8
                    && static_cast<unsigned char>(planeBBytes.at(planeBOffset + 1)) == 0x0c
                    && static_cast<unsigned char>(windowBytes.at(windowOffset)) == 0x30
                    && static_cast<unsigned char>(windowBytes.at(windowOffset + 1)) == 0x0d
                    && QFileInfo(genesisDirectory.filePath(
                           QStringLiteral("GENESIS_CAMPAIGN.PAL"))).size() == 128
                    && QFileInfo(genesisDirectory.filePath(
                           QStringLiteral("GENESIS_CAMPAIGN.REG"))).size() == 24,
                "Genesis Character export should write native big-endian tile words for all three maps");

    ImageInputController hucImage;
    hucImage.setAutoUpdate(false);
    EditorProjectController hucProject(&hucImage);
    hucProject.configureProjectWithTargets(
        QStringLiteral("HuC6270 Campaign"), false, false,
        QStringList{QStringLiteral("huc6270")});
    const QVariantMap hucInfo = hucProject.activeTargetInfo();
    hucProject.setCharacterPaletteBank(15);
    hucProject.setActiveSprite(63);
    hucProject.setActiveSpriteSize(3264);
    hucProject.setActiveSpritePaletteBank(15);
    hucProject.setSpriteDrawingColorIndex(15);
    hucProject.paintSpritePixel(0, 63, 3264, 63, 31, true);
    const QVariantList hucSprite = hucProject.spritePatternPixels(0, 63, 3264);
    test.expect(hucProject.huc6270Enabled()
                    && hucProject.activeTarget()
                        == static_cast<int>(retrovdp::core::TargetProfileId::HuC6270)
                    && hucProject.supportedTargets().size() == 1
                    && hucProject.characterPatternsPerSet() == 1920
                    && hucProject.characterPaletteBank() == 15
                    && hucInfo.value(QStringLiteral("characterPaletteBankCount")).toInt() == 16
                    && hucInfo.value(QStringLiteral("spritePaletteBankCount")).toInt() == 16
                    && hucInfo.value(QStringLiteral("spriteMaximumPerScanline")).toInt() == 16
                    && hucProject.spritePatternsPerSet() == 64
                    && hucProject.spritePatternWidth(3264) == 32
                    && hucProject.spritePatternHeight(3264) == 64
                    && hucProject.activeSpriteColorDepth() == 4
                    && hucProject.activeSpritePaletteBank() == 15
                    && !hucProject.canRotateSpritePattern()
                    && hucSprite.size() == 2048
                    && hucSprite[2047].toInt() == 15,
                "a HuC6270 project should expose native tile palettes and compound 4bpp sprites");

    ImageInputController vicIiImage;
    vicIiImage.setAutoUpdate(false);
    EditorProjectController vicIiProject(&vicIiImage);
    vicIiProject.configureProjectWithTargets(
        QStringLiteral("VIC-II Campaign"), false, false,
        QStringList{QStringLiteral("vic-ii")});
    const QVariantMap vicIiInfo = vicIiProject.activeTargetInfo();
    vicIiProject.setActiveSprite(7);
    vicIiProject.setSpriteDrawingColorIndex(3);
    vicIiProject.paintSpritePixel(0, 7, 2421, 20, 23, true);
    const QVariantList vicIiSprite = vicIiProject.spritePatternPixels(0, 7, 2421);
    test.expect(vicIiProject.vicIiEnabled()
                    && vicIiProject.activeTarget()
                        == static_cast<int>(retrovdp::core::TargetProfileId::VicII)
                    && vicIiProject.characterMapColumns() == 40
                    && vicIiProject.characterMapRows() == 25
                    && vicIiProject.spritePatternWidth(2421) == 24
                    && vicIiProject.spritePatternHeight(2421) == 21
                    && vicIiProject.activeSpriteColorDepth() == 2
                    && vicIiInfo.value(QStringLiteral("spriteMaximumPerScanline")).toInt() == 8
                    && vicIiSprite.size() == 504
                    && vicIiSprite[503].toInt() == 3,
                "a VIC-II project should expose 40x25 character memory and native 24x21 sprites");

    ImageInputController vicImage;
    vicImage.setAutoUpdate(false);
    EditorProjectController vicProject(&vicImage);
    vicProject.configureProjectWithTargets(
        QStringLiteral("VIC Campaign"), false, false,
        QStringList{QStringLiteral("vic")});
    test.expect(vicProject.vicEnabled()
                    && vicProject.activeTarget()
                        == static_cast<int>(retrovdp::core::TargetProfileId::Vic)
                    && vicProject.characterMapColumns() == 22
                    && vicProject.characterMapRows() == 23
                    && !vicProject.spriteModeAvailable(),
                "a VIC-20 project should expose its 22x23 character display without synthetic sprites");

    ImageInputController gameBoyImage;
    gameBoyImage.setAutoUpdate(false);
    EditorProjectController gameBoyProject(&gameBoyImage);
    gameBoyProject.configureProjectWithTargets(
        QStringLiteral("Game Boy Campaign"), false, false,
        QStringList{QStringLiteral("game-boy-ppu")});
    gameBoyProject.setSpriteDrawingColorIndex(3);
    gameBoyProject.paintSpritePixel(0, 0, 8, 0, 0, true);
    const QVariantList gameBoySprite = gameBoyProject.spritePatternPixels(0, 0, 8);
    QTemporaryDir gameBoyDirectory;
    const bool gameBoyExported = gameBoyDirectory.isValid()
        && gameBoyProject.exportNintendoEditorAssets(
            QUrl::fromLocalFile(gameBoyDirectory.path()));
    test.expect(gameBoyProject.gameBoyEnabled()
                    && gameBoyProject.activeTarget()
                        == static_cast<int>(retrovdp::core::TargetProfileId::GameBoy)
                    && gameBoyProject.characterPatternsPerSet() == 384
                    && gameBoyProject.characterMapColumns() == 32
                    && gameBoyProject.activeSpriteColorDepth() == 2
                    && gameBoyProject.spritePatternsPerSet() == 40
                    && gameBoySprite.at(0).toInt() == 3
                    && gameBoyExported
                    && QFileInfo(gameBoyDirectory.filePath(
                           QStringLiteral("GAME_BOY_CAMPAIGN.CHR"))).size() == 6144
                    && QFileInfo(gameBoyDirectory.filePath(
                           QStringLiteral("GAME_BOY_CAMPAIGN.OAM"))).size() == 160,
                "Game Boy editors and native export should retain 2bpp tiles, maps, and 40-entry OAM");

    ImageInputController gameBoyColorImage;
    gameBoyColorImage.setAutoUpdate(false);
    EditorProjectController gameBoyColorProject(&gameBoyColorImage);
    gameBoyColorProject.configureProjectWithTargets(
        QStringLiteral("Game Boy Color Campaign"), false, false,
        QStringList{QStringLiteral("game-boy-color-ppu")});
    gameBoyColorProject.setCharacterPaletteBank(7);
    gameBoyColorProject.setActiveSpritePaletteBank(7);
    QTemporaryDir gameBoyColorDirectory;
    const bool gameBoyColorExported = gameBoyColorDirectory.isValid()
        && gameBoyColorProject.exportNintendoEditorAssets(
            QUrl::fromLocalFile(gameBoyColorDirectory.path()));
    test.expect(gameBoyColorProject.gameBoyColorEnabled()
                    && gameBoyColorProject.characterPatternsPerSet() == 768
                    && gameBoyColorProject.characterPaletteBank() == 7
                    && gameBoyColorProject.activeSpritePaletteBank() == 7
                    && gameBoyColorProject.characterPaletteColors().size() == 4
                    && gameBoyColorProject.spritePaletteColors().size() == 4
                    && gameBoyColorExported
                    && QFileInfo(gameBoyColorDirectory.filePath(
                           QStringLiteral("GAME_BOY_COLOR_CAMPAIGN.ATTR"))).size() == 1024
                    && QFileInfo(gameBoyColorDirectory.filePath(
                           QStringLiteral("GAME_BOY_COLOR_CAMPAIGN.PAL"))).size() == 128,
                "Game Boy Color editors and export should retain VRAM-bank attributes and color palettes");

    ImageInputController superNesImage;
    superNesImage.setAutoUpdate(false);
    EditorProjectController superNesProject(&superNesImage);
    superNesProject.configureProjectWithTargets(
        QStringLiteral("Super NES Campaign"), false, false,
        QStringList{QStringLiteral("super-nes-ppu")});
    superNesProject.setActiveSprite(127);
    superNesProject.setActiveSpriteSize(16);
    superNesProject.setActiveSpritePaletteBank(7);
    QTemporaryDir superNesDirectory;
    const bool superNesExported = superNesDirectory.isValid()
        && superNesProject.exportNintendoEditorAssets(
            QUrl::fromLocalFile(superNesDirectory.path()));
    superNesImage.setConversionMode(static_cast<int>(
        retrovdp::core::ConversionMode::SuperNesMode3Background));
    superNesProject.setCharacterForegroundColorIndex(255);
    superNesProject.paintCharacterPixel(0, 0, 0, 0, true);
    const QVariantList superNesMode3Rows = superNesProject.characterPatternRows(0, 0);
    test.expect(superNesProject.superNesEnabled()
                    && superNesProject.characterPatternsPerSet() == 1024
                    && superNesProject.spritePatternsPerSet() == 128
                    && superNesProject.spritePatternWidth(16) == 16
                    && superNesProject.activeSpritePaletteBank() == 7
                    && superNesExported
                    && QFileInfo(superNesDirectory.filePath(
                           QStringLiteral("SUPER_NES_CAMPAIGN.CHR"))).size() == 32768
                    && QFileInfo(superNesDirectory.filePath(
                           QStringLiteral("SUPER_NES_CAMPAIGN.OAM"))).size() == 544
                    && QFileInfo(superNesDirectory.filePath(
                           QStringLiteral("SUPER_NES_CAMPAIGN.SPR"))).size() == 16384
                    && QFileInfo(superNesDirectory.filePath(
                           QStringLiteral("SUPER_NES_CAMPAIGN.PAL"))).size() == 512
                    && superNesProject.activeTargetInfo()
                           .value(QStringLiteral("characterColorDepth")).toInt() == 8
                    && superNesProject.characterPaletteColors().size() == 256
                    && superNesMode3Rows.at(0).toMap()
                           .value(QStringLiteral("pixels")).toList().at(0).toInt() == 255,
                "Super NES editors and export should retain mode-selected planar tiles, CGRAM, and 128-entry OAM");

    ImageInputController recipeImage;
    recipeImage.setAutoUpdate(false);
    recipeImage.openUrl(QUrl::fromLocalFile(goldenSource(u"source/tiny-rgba.png")));
    recipeImage.setGamma(1.75);
    recipeImage.setForegroundColor(QColor(QStringLiteral("#123456")));

    EditorProjectController project(&recipeImage);
    project.configureProjectWithTargets(
        QStringLiteral("Demo Campaign"), true, true,
        QStringList{QStringLiteral("v9938"), QStringLiteral("sega-sms-vdp"),
                    QStringLiteral("game-boy-ppu")});
    project.setWorkspaceMode(2);
    project.setActiveTarget(1);
    project.addSpriteSet();
    project.setActiveSprite(3);
    project.setSpritePlacementMode(true);
    project.setPlacementWidth(320);
    project.setPlacementHeight(200);
    project.moveSprite(3, 91, 47);
    project.setSpritePlacementMode(false);
    project.setActiveTarget(0);
    project.setActiveSpriteSize(16);
    project.setSpriteDrawingColorIndex(12);
    project.paintSpritePixel(1, 3, 16, 0, 0, true);
    project.setActiveTarget(1);
    project.setActiveSpriteSize(16);
    project.moveSprite(3, 91, 47);
    project.setActiveSpriteColorDepth(3);
    project.setSpriteDrawingColorIndex(5);
    project.paintSpritePixel(1, 3, 16, 0, 1, true);
    project.setSpritePlacementMode(true);
    project.setActiveCharacterSet(2);
    project.setActiveCharacterPattern(255);
    project.setCharacterForegroundColorIndex(2);
    project.setCharacterBackgroundColorIndex(3);
    project.paintCharacterPixel(2, 255, 0, 0, true);
    project.paintCharacterPixel(2, 255, 0, 7, true);
    project.paintCharacterPixel(2, 255, 1, 4, true);
    project.paintCharacterPixel(2, 255, 1, 4, false);
    const QVariantList editedRows = project.characterPatternRows(2, 255);
    test.expect(project.characterPaletteColors().size() == 16
                    && editedRows.size() == 8
                    && editedRows.at(0).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x81
                    && editedRows.at(0).toMap().value(QStringLiteral("color")).toInt()
                        == 0x23
                    && editedRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0,
                "character pattern model should expose 8 bitmap/color rows and support pencil and eraser edits");

    ImageInputController extractionImage;
    extractionImage.setAutoUpdate(false);
    extractionImage.newScreenImage();
    extractionImage.setScreenImageForegroundColor(QColor(Qt::white));
    extractionImage.setScreenImageBackgroundColor(QColor(Qt::black));
    extractionImage.drawScreenImageLine(0.0, 0.0, 7.0 / 256.0,
                                        7.0 / 192.0, 1, true);
    extractionImage.drawScreenImageLine(8.0 / 256.0, 0.0, 15.0 / 256.0,
                                        0.0, 1, true);
    extractionImage.setScreenImageSelection(
        0.0, 0.0, 15.0 / 256.0, 7.0 / 192.0);
    EditorProjectController extractionProject(&extractionImage);
    test.expect(extractionProject.characterPatternWidth() == 8
                    && extractionProject.characterPatternHeight() == 8
                    && extractionProject.characterPatternsPerSet() == 256
                    && extractionProject.characterSetCount() == 3
                    && extractionProject.characterMapColumns() == 32
                    && extractionProject.characterMapRows() == 24
                    && extractionProject.screenImageSelectionCharacterAligned(),
                "character extraction geometry should come from the active target and recognize aligned Screen Image selections");
    const bool extracted = extractionProject.extractScreenImagePatterns(
        0, 0, 2, 1, 10, false);
    const QVariantList extractedFirst = extractionProject.characterPatternRows(0, 10);
    const QVariantList extractedSecond = extractionProject.characterPatternRows(0, 11);
    const QString extractedPreview = extractionProject.characterPatternPreview(
        0, 10, 2, 1, false);
    const QImage extractedPreviewImage = QImage::fromData(QByteArray::fromBase64(
        extractedPreview.section(QLatin1Char(','), 1).toLatin1()), "PNG");
    test.expect(extracted && extractionProject.canUndoCharacter()
                    && extractedFirst.at(0).toMap()
                           .value(QStringLiteral("pattern")).toInt() != 0
                    && extractedSecond.at(0).toMap()
                           .value(QStringLiteral("color")).toInt() != 0xf1
                    && extractedPreviewImage.size() == QSize(16, 8),
                "Screen Image extraction should populate an active-set region and render it in Pattern Previewer");
    extractionProject.undoCharacterEdit();
    const QVariantList undoneExtractedFirst =
        extractionProject.characterPatternRows(0, 10);
    const QVariantList undoneExtractedSecond =
        extractionProject.characterPatternRows(0, 11);
    test.expect(undoneExtractedFirst.at(0).toMap()
                        .value(QStringLiteral("pattern")).toInt() == 0
                    && undoneExtractedSecond.at(0).toMap()
                           .value(QStringLiteral("color")).toInt() == 0xf1,
                "multi-pattern extraction should undo as one character edit");

    ImageInputController genesisExtractionImage;
    genesisExtractionImage.setAutoUpdate(false);
    EditorProjectController genesisExtractionProject(&genesisExtractionImage);
    genesisExtractionProject.configureProjectWithTargets(
        QStringLiteral("Layer Import"), false, false,
        QStringList{QStringLiteral("sega-genesis-vdp")});
    genesisExtractionImage.newScreenImage();
    genesisExtractionProject.setActiveCharacterPlane(1);
    const bool genesisExtracted = genesisExtractionProject.extractScreenImagePatterns(
        0, 0, 2, 1, 20, false);
    const QVariantList genesisExtractedSlots =
        genesisExtractionProject.characterEditorSlots();
    int importedPlaneBCells = 0;
    for (const QVariant& value : genesisExtractedSlots) {
        const QVariantMap slot = value.toMap();
        if (slot.value(QStringLiteral("plane")).toInt() == 1
            && slot.value(QStringLiteral("loaded")).toBool()
            && (slot.value(QStringLiteral("patternIndex")).toInt() == 20
                || slot.value(QStringLiteral("patternIndex")).toInt() == 21)) {
            ++importedPlaneBCells;
        }
    }
    test.expect(genesisExtracted && importedPlaneBCells == 2,
                "Genesis Screen Image extraction should place the selected region directly into the active plane map");

    EditorProjectController historyProject(&recipeImage);
    historyProject.beginCharacterEdit(0, 0);
    historyProject.paintCharacterPixel(0, 0, 0, 0, true);
    historyProject.paintCharacterPixel(0, 0, 1, 2, true);
    historyProject.endCharacterEdit();
    auto historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(historyProject.canUndoCharacter()
                    && historyRows.at(0).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x80
                    && historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x20,
                "one character drawing stroke should group all painted pixels into one history entry");
    historyProject.undoCharacterEdit();
    historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(historyProject.canRedoCharacter()
                    && historyRows.at(0).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0
                    && historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0,
                "character undo should revert a complete drawing stroke");
    historyProject.redoCharacterEdit();

    EditorProjectController characterLineProject(&recipeImage);
    characterLineProject.drawCharacterLine(0, 0, 0, 0, 7, 7, true);
    auto lineRows = characterLineProject.characterPatternRows(0, 0);
    bool characterDiagonal = lineRows.size() == 8;
    for (int row = 0; row < lineRows.size(); ++row) {
        characterDiagonal &= lineRows.at(row).toMap()
                                 .value(QStringLiteral("pattern")).toInt()
            == (0x80 >> row);
    }
    test.expect(characterDiagonal && characterLineProject.canUndoCharacter(),
                "character line should rasterize an inclusive one-pixel diagonal");
    characterLineProject.undoCharacterEdit();
    lineRows = characterLineProject.characterPatternRows(0, 0);
    bool characterLineUndone = true;
    for (const QVariant& row : lineRows) {
        characterLineUndone &= row.toMap()
                                  .value(QStringLiteral("pattern")).toInt() == 0;
    }
    test.expect(characterLineUndone,
                "a standalone character line should undo as one change");
    characterLineProject.beginCharacterEdit(0, 0);
    characterLineProject.drawCharacterLine(0, 0, 0, 0, 0, 3, true);
    characterLineProject.redoCharacterEdit();
    lineRows = characterLineProject.characterPatternRows(0, 0);
    test.expect(lineRows.at(0).toMap()
                        .value(QStringLiteral("pattern")).toInt() == 0xf0
                    && !characterLineProject.canRedoCharacter(),
                "redo during an active character chain should safely keep the new edit");
    characterLineProject.undoCharacterEdit();
    characterLineProject.beginCharacterEdit(0, 0);
    characterLineProject.drawCharacterLine(0, 0, 0, 0, 0, 7, true);
    characterLineProject.drawCharacterLine(0, 0, 0, 7, 7, 7, true);
    characterLineProject.endCharacterEdit();
    lineRows = characterLineProject.characterPatternRows(0, 0);
    bool characterKLine = lineRows.at(0).toMap()
                              .value(QStringLiteral("pattern")).toInt() == 0xff;
    for (int row = 1; row < lineRows.size(); ++row) {
        characterKLine &= lineRows.at(row).toMap()
                              .value(QStringLiteral("pattern")).toInt() == 0x01;
    }
    characterLineProject.undoCharacterEdit();
    lineRows = characterLineProject.characterPatternRows(0, 0);
    bool characterKLineUndone = true;
    for (const QVariant& row : lineRows) {
        characterKLineUndone &= row.toMap()
                                   .value(QStringLiteral("pattern")).toInt() == 0;
    }
    test.expect(characterKLine && characterKLineUndone,
                "character K-Line segments should share one undo transaction");
    characterLineProject.beginCharacterEdit(0, 0);
    characterLineProject.drawCharacterLine(0, 0, 3, 3, 3, 7, true);
    characterLineProject.drawCharacterLine(0, 0, 3, 3, 7, 3, true);
    characterLineProject.endCharacterEdit();
    lineRows = characterLineProject.characterPatternRows(0, 0);
    bool characterRays = true;
    for (int row = 0; row < lineRows.size(); ++row) {
        const int expected = row == 3 ? 0x1f
            : (row > 3 ? 0x10 : 0x00);
        characterRays &= lineRows.at(row).toMap()
                             .value(QStringLiteral("pattern")).toInt() == expected;
    }
    characterLineProject.undoCharacterEdit();
    lineRows = characterLineProject.characterPatternRows(0, 0);
    bool characterRaysUndone = true;
    for (const QVariant& row : lineRows) {
        characterRaysUndone &= row.toMap()
                                  .value(QStringLiteral("pattern")).toInt() == 0;
    }
    test.expect(characterRays && characterRaysUndone,
                "character Rays should share a fixed origin and one undo transaction");

    historyProject.mirrorActiveCharacterPattern();
    historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(historyRows.at(0).toMap().value(QStringLiteral("pattern")).toInt()
                    == 0x01
                    && historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x04,
                "horizontal mirror should reverse every pattern row");
    historyProject.undoCharacterEdit();
    historyProject.flipActiveCharacterPattern();
    historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(historyRows.at(6).toMap().value(QStringLiteral("pattern")).toInt()
                    == 0x20
                    && historyRows.at(7).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x80,
                "vertical flip should reverse pattern and row-color order");
    historyProject.undoCharacterEdit();
    historyProject.rotateActiveCharacterPattern();
    historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(historyRows.at(0).toMap().value(QStringLiteral("pattern")).toInt()
                    == 0x01
                    && historyRows.at(2).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x02,
                "clockwise rotation should rotate the 8 by 8 bitmap");
    historyProject.undoCharacterEdit();
    historyProject.blankActiveCharacterPattern();
    historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(historyRows.at(0).toMap().value(QStringLiteral("pattern")).toInt()
                    == 0
                    && historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0,
                "blank should clear the bitmap while remaining undoable");
    historyProject.undoCharacterEdit();

    historyProject.setCharacterPanActive(true);
    historyProject.nudgeCharacterPan(1, 1);
    historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(historyProject.characterPanActive()
                    && historyProject.characterPanX() == 1
                    && historyProject.characterPanY() == 1
                    && historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x40
                    && historyRows.at(2).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x10,
                "24 by 24 panning should expose a clipped live 8 by 8 center viewport");
    historyProject.centerCharacterPan();
    historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(historyProject.characterPanX() == 0
                    && historyProject.characterPanY() == 0
                    && historyRows.at(0).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x80,
                "the panning center control should restore the original live position");
    historyProject.nudgeCharacterPan(1, 1);
    historyProject.setCharacterPanActive(false);
    historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(!historyProject.characterPanActive()
                    && historyProject.characterPanX() == 0
                    && historyProject.characterPanY() == 0
                    && historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x40,
                "finishing Pan should commit the final viewport and reset its virtual origin");
    historyProject.undoCharacterEdit();
    historyRows = historyProject.characterPatternRows(0, 0);
    const bool panUndoRestoredOriginal =
        historyRows.at(0).toMap().value(QStringLiteral("pattern")).toInt() == 0x80
        && historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt() == 0x20;
    historyProject.undoCharacterEdit();
    historyRows = historyProject.characterPatternRows(0, 0);
    test.expect(panUndoRestoredOriginal
                    && historyRows.at(0).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0
                    && historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0,
                "all Pan movement should create one undo step between original and final positions");

    QClipboard* clipboard = QGuiApplication::clipboard();
    const QString previousClipboardText = clipboard != nullptr ? clipboard->text() : QString{};
    historyProject.redoCharacterEdit();
    historyProject.redoCharacterEdit();
    const bool copiedPattern = clipboard != nullptr
        && historyProject.copyActiveCharacterPattern();
    const QJsonDocument clipboardDocument = clipboard != nullptr
        ? QJsonDocument::fromJson(clipboard->text().toUtf8()) : QJsonDocument{};
    const QJsonObject clipboardObject = clipboardDocument.object();
    test.expect(copiedPattern && historyProject.canPasteCharacterPattern()
                    && clipboardObject.value(QStringLiteral("format")).toString()
                        == QStringLiteral("retrovdp.character-pattern")
                    && clipboardObject.value(QStringLiteral("version")).toInt() == 1
                    && clipboardObject.value(QStringLiteral("bitmap")).toArray().size()
                        == 8
                    && clipboardObject.value(QStringLiteral("colors")).toArray().size()
                        == 8,
                "Copy should publish readable, versioned character-pattern JSON to the system clipboard");
    historyProject.setActiveCharacterPattern(1);
    const bool pastedPattern = historyProject.pasteActiveCharacterPattern();
    historyRows = historyProject.characterPatternRows(0, 1);
    test.expect(pastedPattern
                    && historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x40
                    && historyRows.at(2).toMap().value(QStringLiteral("pattern")).toInt()
                        == 0x10,
                "Paste should recreate bitmap and row-color data in another active pattern");
    historyProject.undoCharacterEdit();
    historyRows = historyProject.characterPatternRows(0, 1);
    test.expect(historyRows.at(1).toMap().value(QStringLiteral("pattern")).toInt()
                    == 0
                    && historyProject.canRedoCharacter(),
                "pasting a clipboard pattern should create one undoable character edit");
    if (clipboard != nullptr) {
        clipboard->setText(QStringLiteral("not pattern json"));
        test.expect(!historyProject.canPasteCharacterPattern(),
                    "Paste should reject unrelated clipboard text");
        clipboard->setText(previousClipboardText);
    }

    EditorProjectController spriteProject(&recipeImage);
    spriteProject.setWorkspaceMode(2);
    spriteProject.setActiveSpriteSize(8);
    QVariantList spritePlacements = spriteProject.activeSpritePlacements();
    test.expect(spriteProject.spriteDrawingColorIndex() == 15
                    && spritePlacements.at(0).toMap()
                           .value(QStringLiteral("color")).toInt() == 15,
                "a new 9918A sprite should show the same default opaque color that its pixels render");
    spriteProject.setSpriteDrawingColorIndex(12);
    spritePlacements = spriteProject.activeSpritePlacements();
    test.expect(spriteProject.spriteDrawingColorIndex() == 12
                    && spritePlacements.at(0).toMap()
                           .value(QStringLiteral("color")).toInt() == 12,
                "the 9918A sprite color selector should accept every opaque hardware palette index and update the sprite attribute");
    spriteProject.setActiveSprite(1);
    const bool selectedSpriteColorSynchronized =
        spriteProject.spriteDrawingColorIndex() == 15;
    spriteProject.setActiveSprite(0);
    test.expect(selectedSpriteColorSynchronized
                    && spriteProject.spriteDrawingColorIndex() == 12,
                "changing the active sprite should synchronize the toolbar swatch with that sprite's hardware color");
    spriteProject.beginSpriteEdit(0, 0, 8);
    spriteProject.paintSpritePixel(0, 0, 8, 0, 0, true);
    spriteProject.paintSpritePixel(0, 0, 8, 1, 2, true);
    spriteProject.endSpriteEdit();
    QVariantList spritePixels = spriteProject.spritePatternPixels(0, 0, 8);
    test.expect(spriteProject.canUndoSprite() && spritePixels.size() == 64
                    && spritePixels.at(0).toInt() == 1
                    && spritePixels.at(10).toInt() == 1,
                "sprite pencil strokes should edit the active size bank and group into one undo entry");
    spriteProject.undoSpriteEdit();
    spritePixels = spriteProject.spritePatternPixels(0, 0, 8);
    test.expect(spriteProject.canRedoSprite() && spritePixels.at(0).toInt() == 0
                    && spritePixels.at(10).toInt() == 0,
                "sprite undo should revert an entire drawing stroke");
    spriteProject.redoSpriteEdit();

    EditorProjectController spriteLineProject(&recipeImage);
    spriteLineProject.drawSpriteLine(0, 0, 8, 0, 0, 7, 7, true);
    auto linePixels = spriteLineProject.spritePatternPixels(0, 0, 8);
    bool spriteDiagonal = linePixels.size() == 64;
    for (int pixel = 0; pixel < linePixels.size(); ++pixel) {
        const int row = pixel / 8;
        const int column = pixel % 8;
        spriteDiagonal &= linePixels.at(pixel).toInt()
            == (row == column ? 1 : 0);
    }
    test.expect(spriteDiagonal && spriteLineProject.canUndoSprite(),
                "sprite line should rasterize an inclusive one-pixel diagonal");
    spriteLineProject.undoSpriteEdit();
    linePixels = spriteLineProject.spritePatternPixels(0, 0, 8);
    bool spriteLineUndone = true;
    for (const QVariant& pixel : linePixels)
        spriteLineUndone &= pixel.toInt() == 0;
    test.expect(spriteLineUndone,
                "a standalone sprite line should undo as one change");
    spriteLineProject.beginSpriteEdit(0, 0, 8);
    spriteLineProject.drawSpriteLine(0, 0, 8, 0, 0, 0, 3, true);
    spriteLineProject.redoSpriteEdit();
    linePixels = spriteLineProject.spritePatternPixels(0, 0, 8);
    test.expect(linePixels.at(0).toInt() == 1
                    && linePixels.at(1).toInt() == 1
                    && linePixels.at(2).toInt() == 1
                    && linePixels.at(3).toInt() == 1
                    && !spriteLineProject.canRedoSprite(),
                "redo during an active sprite chain should safely keep the new edit");
    spriteLineProject.undoSpriteEdit();
    spriteLineProject.beginSpriteEdit(0, 0, 8);
    spriteLineProject.drawSpriteLine(0, 0, 8, 0, 0, 0, 7, true);
    spriteLineProject.drawSpriteLine(0, 0, 8, 0, 7, 7, 7, true);
    spriteLineProject.endSpriteEdit();
    linePixels = spriteLineProject.spritePatternPixels(0, 0, 8);
    bool spriteKLine = true;
    for (int pixel = 0; pixel < linePixels.size(); ++pixel) {
        const int row = pixel / 8;
        const int column = pixel % 8;
        spriteKLine &= linePixels.at(pixel).toInt()
            == (row == 0 || column == 7 ? 1 : 0);
    }
    spriteLineProject.undoSpriteEdit();
    linePixels = spriteLineProject.spritePatternPixels(0, 0, 8);
    bool spriteKLineUndone = true;
    for (const QVariant& pixel : linePixels)
        spriteKLineUndone &= pixel.toInt() == 0;
    test.expect(spriteKLine && spriteKLineUndone,
                "sprite K-Line segments should share one undo transaction");
    spriteLineProject.beginSpriteEdit(0, 0, 8);
    spriteLineProject.drawSpriteLine(0, 0, 8, 3, 3, 3, 7, true);
    spriteLineProject.drawSpriteLine(0, 0, 8, 3, 3, 7, 3, true);
    spriteLineProject.endSpriteEdit();
    linePixels = spriteLineProject.spritePatternPixels(0, 0, 8);
    bool spriteRays = true;
    for (int pixel = 0; pixel < linePixels.size(); ++pixel) {
        const int row = pixel / 8;
        const int column = pixel % 8;
        const bool expected = (row == 3 && column >= 3)
            || (column == 3 && row >= 3);
        spriteRays &= linePixels.at(pixel).toInt() == (expected ? 1 : 0);
    }
    spriteLineProject.undoSpriteEdit();
    linePixels = spriteLineProject.spritePatternPixels(0, 0, 8);
    bool spriteRaysUndone = true;
    for (const QVariant& pixel : linePixels)
        spriteRaysUndone &= pixel.toInt() == 0;
    test.expect(spriteRays && spriteRaysUndone,
                "sprite Rays should share a fixed origin and one undo transaction");

    spriteProject.setEditScope(1);
    spriteProject.setActiveSpriteSize(16);
    spriteProject.setActiveSpriteColorDepth(3);
    spriteProject.setSpriteDrawingColorIndex(6);
    spriteProject.paintSpritePixel(0, 0, 16, 0, 0, true);
    const QVariantList enhanced16 = spriteProject.spritePatternPixels(0, 0, 16);
    spriteProject.setEditScope(0);
    const QVariantList baseline16 = spriteProject.spritePatternPixels(0, 0, 16);
    test.expect(enhanced16.size() == 256 && enhanced16.at(0).toInt() == 6
                    && baseline16.at(0).toInt() == 0
                    && spriteProject.spriteGlobalSize() == 8
                    && spriteProject.spriteDrawingColorIndex() == 12,
                "F18A sprite pixels, per-sprite size, and color depth should remain non-destructive overrides of the 9918A baseline");
    spriteProject.setEditScope(1);
    const bool copiedSprite = clipboard != nullptr
        && spriteProject.copyActiveSpritePattern();
    const QJsonObject spriteClipboardObject = clipboard != nullptr
        ? QJsonDocument::fromJson(clipboard->text().toUtf8()).object()
        : QJsonObject{};
    spriteProject.setActiveSprite(1);
    spriteProject.setActiveSpriteSize(16);
    spriteProject.setActiveSpriteColorDepth(3);
    const bool pastedSprite = copiedSprite
        && spriteProject.pasteActiveSpritePattern();
    const QVariantList pastedSpritePixels =
        spriteProject.spritePatternPixels(0, 1, 16);
    test.expect(copiedSprite && pastedSprite
                    && spriteClipboardObject.value(QStringLiteral("format")).toString()
                        == QStringLiteral("retrovdp.sprite-pattern")
                    && spriteClipboardObject.value(QStringLiteral("size")).toInt() == 16
                    && spriteClipboardObject.value(QStringLiteral("colorDepth")).toInt() == 3
                    && pastedSpritePixels.at(0).toInt() == 6,
                "sprite Copy/Paste should exchange readable, versioned JSON while validating size and color depth");
    if (clipboard != nullptr) clipboard->setText(previousClipboardText);
    spriteProject.moveSprite(0, 37, 21);
    spriteProject.moveSprite(1, 37, 21);
    const QVariantList overlapping = spriteProject.activeSpritePlacements();
    test.expect(overlapping.at(0).toMap().value(QStringLiteral("x")).toInt() == 37
                    && overlapping.at(1).toMap().value(QStringLiteral("x")).toInt() == 37
                    && overlapping.at(0).toMap().value(QStringLiteral("y")).toInt() == 21
                    && overlapping.at(1).toMap().value(QStringLiteral("y")).toInt() == 21,
                "sprite placement should retain one-pixel coordinates and permit overlap");

    EditorProjectController spriteBankProject(&recipeImage);
    QVariantList spriteEditors = spriteBankProject.spriteEditorSlots();
    test.expect(spriteEditors.size() == 1
                    && !spriteEditors.at(0).toMap()
                            .value(QStringLiteral("loaded")).toBool()
                    && spriteEditors.at(0).toMap()
                           .value(QStringLiteral("size")).toInt() == 8,
                "a new sprite tray should begin with one empty 8 by 8 editor");
    spriteBankProject.setSpriteGlobalSize(16);
    spriteEditors = spriteBankProject.spriteEditorSlots();
    test.expect(spriteEditors.size() == 2
                    && spriteBankProject.activeSpriteEditor() == 1
                    && !spriteEditors.at(1).toMap()
                            .value(QStringLiteral("loaded")).toBool()
                    && spriteEditors.at(1).toMap()
                           .value(QStringLiteral("size")).toInt() == 16,
                "visiting an unused sprite bank should create and select one empty editor for that bank");
    spriteBankProject.selectSpritePattern(0, 16);
    spriteBankProject.setSpriteGlobalSize(8);
    spriteBankProject.selectSpritePattern(0, 8);
    spriteBankProject.setSpriteGlobalSize(16);
    spriteEditors = spriteBankProject.spriteEditorSlots();
    test.expect(spriteEditors.size() == 2
                    && spriteEditors.at(0).toMap()
                           .value(QStringLiteral("loaded")).toBool()
                    && spriteEditors.at(1).toMap()
                           .value(QStringLiteral("loaded")).toBool()
                    && spriteEditors.at(0).toMap()
                           .value(QStringLiteral("size")).toInt() == 8
                    && spriteEditors.at(1).toMap()
                           .value(QStringLiteral("size")).toInt() == 16
                    && !spriteEditors.at(0).toMap()
                            .value(QStringLiteral("activeForPlacement")).toBool()
                    && spriteEditors.at(1).toMap()
                           .value(QStringLiteral("activeForPlacement")).toBool(),
                "8 by 8 and 16 by 16 editor slots should retain independent pattern-bank identity while 9918A placement activates one global size");
    spriteBankProject.moveSpriteEditorTile(1, 80, 40);
    spriteBankProject.setActiveSpriteEditor(0);
    spriteBankProject.moveSpriteEditorTile(0, 20, 10);
    spriteEditors = spriteBankProject.spriteEditorSlots();
    test.expect(spriteEditors.at(0).toMap().value(QStringLiteral("x")).toInt() == 20
                    && spriteEditors.at(0).toMap()
                           .value(QStringLiteral("y")).toInt() == 10
                    && spriteEditors.at(1).toMap()
                           .value(QStringLiteral("x")).toInt() == 80
                    && spriteEditors.at(1).toMap()
                           .value(QStringLiteral("y")).toInt() == 40,
                "8 by 8 and 16 by 16 sprite banks should retain independent screen positions");
    spriteBankProject.setEditScope(1);
    spriteBankProject.addSpriteEditor();
    spriteBankProject.selectSpritePattern(1, 16);
    spriteEditors = spriteBankProject.spriteEditorSlots();
    int activePlacementBanks = 0;
    for (const QVariant& editor : spriteEditors) {
        if (editor.toMap().value(QStringLiteral("activeForPlacement")).toBool()) {
            ++activePlacementBanks;
        }
    }
    test.expect(spriteEditors.size() == 3 && activePlacementBanks == 2,
                "F18A placement should allow 8 by 8 and 16 by 16 editor banks to contribute at the same time");

    QVariantList editorSlots = project.characterEditorSlots();
    test.expect(editorSlots.size() == 1 && project.activeCharacterEditor() == 0
                    && editorSlots.at(0).toMap().value(QStringLiteral("loaded")).toBool()
                    && editorSlots.at(0).toMap().value(QStringLiteral("patternIndex")).toInt()
                        == 255,
                "character trays should begin with one loaded active editor");
    project.addCharacterEditor();
    project.setCharacterTilingMode(true);
    project.moveCharacterTile(1, 17, 13);
    editorSlots = project.characterEditorSlots();
    test.expect(editorSlots.size() == 2 && project.activeCharacterEditor() == 1
                    && !editorSlots.at(1).toMap().value(QStringLiteral("loaded")).toBool()
                    && editorSlots.at(1).toMap().value(QStringLiteral("tileX")).toInt()
                        == 16
                    && editorSlots.at(1).toMap().value(QStringLiteral("tileY")).toInt()
                        == 16,
                "adding a character editor should create and select an empty slot");
    project.setActiveCharacterPattern(42);
    editorSlots = project.characterEditorSlots();
    test.expect(editorSlots.at(1).toMap().value(QStringLiteral("loaded")).toBool()
                    && editorSlots.at(1).toMap().value(QStringLiteral("setIndex")).toInt()
                        == 2
                    && editorSlots.at(1).toMap().value(QStringLiteral("patternIndex")).toInt()
                        == 42,
                "choosing a pattern should load it into the active empty editor");
    project.setActiveCharacterEditor(0);
    project.moveCharacterEditor(1, 0);
    editorSlots = project.characterEditorSlots();
    test.expect(project.activeCharacterEditor() == 1
                    && editorSlots.at(0).toMap().value(QStringLiteral("patternIndex")).toInt()
                        == 42
                    && editorSlots.at(1).toMap().value(QStringLiteral("patternIndex")).toInt()
                        == 255,
                "reordering should retain the same active editor and its pattern");
    project.moveCharacterEditor(0, 1);
    project.addCharacterEditor();
    project.setActiveCharacterPattern(42);
    editorSlots = project.characterEditorSlots();
    test.expect(editorSlots.size() == 3
                    && editorSlots.at(1).toMap().value(QStringLiteral("patternIndex")).toInt()
                        == 42
                    && editorSlots.at(2).toMap().value(QStringLiteral("patternIndex")).toInt()
                        == 42
                    && editorSlots.at(1).toMap().value(QStringLiteral("tileX")).toInt()
                        != editorSlots.at(2).toMap().value(QStringLiteral("tileX")).toInt(),
                "duplicate pattern tiles should share pattern identity but retain independent placement");
    project.removeActiveCharacterEditor();
    project.setActiveCharacterEditor(0);
    test.expect(project.characterEditorSlots().size() == 2
                    && project.activeCharacterPattern() == 255,
                "removing should preserve at least one editor and select a valid survivor");

    QTemporaryDir directory(
        QDir::current().filePath(QStringLiteral("interface-recipe-XXXXXX")));
    const QString recipePath = directory.filePath(QStringLiteral("roundtrip.rvdp.json"));
    const bool recipeSaved = directory.isValid()
        && project.saveRecipe(QUrl::fromLocalFile(recipePath));
    if (!recipeSaved) {
        std::cerr << "Recipe save failed: "
                  << project.errorMessage().toStdString() << '\n';
    }
    test.expect(recipeSaved && QFileInfo::exists(recipePath),
                "editor project should save a versioned recipe");

    project.setWorkspaceMode(0);
    project.setF18aEnabled(false);
    project.configureProjectWithTargets(project.projectName(), true, true, {});
    project.setActiveSpriteSet(0);
    project.setActiveSprite(0);
    project.setSpritePlacementMode(false);
    project.setPlacementWidth(256);
    project.setCharacterForegroundColorIndex(15);
    project.setCharacterBackgroundColorIndex(1);
    project.setCharacterTilingMode(false);
    project.setActiveCharacterEditor(1);
    project.moveCharacterTile(1, 80, 80);
    project.removeActiveCharacterEditor();
    project.paintCharacterPixel(2, 255, 0, 0, false);
    recipeImage.setGamma(1.0);
    recipeImage.setForegroundColor(Qt::white);

    const bool loaded = project.loadRecipe(QUrl::fromLocalFile(recipePath));
    if (!loaded) {
        std::cerr << "Recipe load failed: "
                  << project.errorMessage().toStdString() << '\n';
    }
    const QVariantList placements = project.activeSpritePlacements();
    const QVariantMap movedSprite = placements.size() > 3
        ? placements.at(3).toMap() : QVariantMap{};
    const QVariantList restoredRows = project.characterPatternRows(2, 255);
    const QVariantList restoredEditors = project.characterEditorSlots();
    const QVariantList restoredSpritePixels = project.spritePatternPixels(1, 3, 16);
    test.expect(loaded && project.projectName() == QStringLiteral("Demo Campaign")
                    && project.tms9918aEnabled()
                    && project.v9938Enabled()
                    && project.segaSmsEnabled()
                    && project.gameBoyEnabled()
                    && project.plannedTargetIds().isEmpty()
                    && project.workspaceMode() == 2 && project.f18aEnabled()
                    && project.previewTarget() == 1 && project.editScope() == 1
                    && project.spriteSetNames().size() == 2
                    && project.activeSpriteSet() == 1 && project.activeSprite() == 3
                    && project.spritePlacementMode() && project.placementWidth() == 320
                    && project.placementHeight() == 200
                    && movedSprite.value(QStringLiteral("x")).toInt() == 91
                    && movedSprite.value(QStringLiteral("y")).toInt() == 47
                    && movedSprite.value(QStringLiteral("size")).toInt() == 16
                    && movedSprite.value(QStringLiteral("colorDepth")).toInt() == 3
                    && project.activeSpriteSize() == 16
                    && project.spriteGlobalSize() == 16
                    && restoredSpritePixels.size() == 256
                    && restoredSpritePixels.at(0).toInt() == 1
                    && restoredSpritePixels.at(1).toInt() == 5
                    && project.activeCharacterSet() == 2
                    && project.activeCharacterPattern() == 255
                    && project.activeCharacterEditor() == 0
                    && project.characterTilingMode()
                    && restoredEditors.size() == 2
                    && restoredEditors.at(1).toMap()
                           .value(QStringLiteral("patternIndex")).toInt() == 42
                    && restoredEditors.at(1).toMap()
                           .value(QStringLiteral("tileX")).toInt() == 16
                    && restoredEditors.at(1).toMap()
                           .value(QStringLiteral("tileY")).toInt() == 16
                    && project.characterForegroundColorIndex() == 2
                    && project.characterBackgroundColorIndex() == 3
                    && restoredRows.size() == 8
                    && restoredRows.at(0).toMap()
                           .value(QStringLiteral("pattern")).toInt() == 0x81
                    && restoredRows.at(0).toMap()
                           .value(QStringLiteral("color")).toInt() == 0x23
                    && qAbs(recipeImage.gamma() - 1.75) < 0.001
                    && recipeImage.foregroundColor() == QColor(QStringLiteral("#123456"))
                    && recipeImage.sourceName() == QStringLiteral("tiny-rgba.png"),
                "recipe load should restore conversion, profile, set, placement, and source state");
}

void testResponsiveQml(TestContext& test, ImageInputController& controller)
{
    const QDir qmlDirectory(QStringLiteral(RETROVDP_QML_DIR));
    const QDir iconDirectory(qmlDirectory.filePath(QStringLiteral("../assets/icons")));
    for (const int size : {16, 24, 32, 48, 64, 128, 256, 512, 1024}) {
        const QImage icon(iconDirectory.filePath(
            QStringLiteral("RetroVDPStudio-%1.png").arg(size)));
        test.expect(!icon.isNull()
                        && icon.size() == QSize(size, size)
                        && icon.pixelColor(0, 0).alpha() == 0,
                    "generated application PNG should have its declared dimensions and a transparent background");
    }
    test.expect(QFileInfo::exists(
                    iconDirectory.filePath(QStringLiteral("RetroVDPStudio.ico")))
                    && QFileInfo::exists(
                        iconDirectory.filePath(QStringLiteral("RetroVDPStudio.icns"))),
                "native Windows and macOS application icons should be generated");
    const QIcon applicationIcon(
        iconDirectory.filePath(QStringLiteral("RetroVDPStudio-256.png")));
    test.expect(!applicationIcon.isNull(),
                "application icon PNG should load as a native Qt icon");
    const QImage watermarkImage(
        iconDirectory.filePath(QStringLiteral("RetroVDPStudio-watermark-512.png")));
    test.expect(!watermarkImage.isNull()
                    && watermarkImage.size() == QSize(512, 512)
                    && watermarkImage.pixelColor(0, 0).alpha() == 0,
                "preview watermark should have a transparent background");
    QGuiApplication::setWindowIcon(applicationIcon);

    AppPreferencesController appPreferences(&controller);
    EditorProjectController editorProject(&controller);
    MediaClipController mediaClip;
    MediaBatchController mediaBatch(&mediaClip, &appPreferences);
    TargetSupportCatalog targetSupportCatalog(
        qmlDirectory.absoluteFilePath(
            QStringLiteral("../../data/target-support/targets.json")));
    editorProject.setActiveTarget(0);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("imageInput"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("appPreferences"), &appPreferences);
    engine.rootContext()->setContextProperty(QStringLiteral("editorProject"), &editorProject);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaClip"), &mediaClip);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaBatch"), &mediaBatch);
    engine.rootContext()->setContextProperty(QStringLiteral("targetSupportCatalog"),
                                             &targetSupportCatalog);
    const QString mainQml = QDir(QStringLiteral(RETROVDP_QML_DIR))
                                .filePath(QStringLiteral("Main.qml"));
    engine.load(QUrl::fromLocalFile(mainQml));
    test.expect(engine.rootObjects().size() == 1,
                "Phase 6 QML should load as one application window");
    if (engine.rootObjects().isEmpty()) return;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    test.expect(window != nullptr, "Phase 6 root should be a QQuickWindow");
    if (window == nullptr) return;

    QObject* newAction = window->findChild<QObject*>(QStringLiteral("newAction"));
    QObject* newMenuItem = window->findChild<QObject*>(QStringLiteral("newMenuItem"));
    test.expect(newAction != nullptr && newMenuItem != nullptr
                    && newAction->property("enabled").toBool()
                    && newAction->property("text").toString().contains(
                        QStringLiteral("New")),
                "File should expose an enabled New command for a blank Screen Image");

    QObject* openMediaClipAction = window->findChild<QObject*>(
        QStringLiteral("openMediaClipAction"));
    QObject* mediaClipWorkspace = window->findChild<QObject*>(
        QStringLiteral("mediaClipWorkspace"));
    test.expect(openMediaClipAction != nullptr && mediaClipWorkspace != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("mediaSourceMonitor")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("mediaFilmstrip")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("mediaPlayPauseButton")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("openMediaFrameAsSourceButton")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("convertMediaClipButton")) != nullptr,
                "File should expose the media clip source monitor, controls, and virtualized filmstrip");

    QObject* preferencesAction = window->findChild<QObject*>(
        QStringLiteral("preferencesAction"));
    const bool preferencesOpened = preferencesAction != nullptr
        && QMetaObject::invokeMethod(preferencesAction, "trigger");
    QObject* preferencesDialog = window->findChild<QObject*>(
        QStringLiteral("preferencesDialog"));
    test.expect(preferencesOpened && preferencesDialog != nullptr
                    && waitFor([&] {
                        return preferencesDialog->property("visible").toBool();
                    })
                    && window->findChild<QObject*>(
                           QStringLiteral("interfacePreferencesGroup")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("behaviorPreferencesGroup")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("conversionWorkersComboBox")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("defaultPreferencesGroup")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("mediaToolsPreferencesGroup")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("testMediaToolsButton")) != nullptr,
                "Preferences should expose Interface, Behavior, Defaults, and Media Tools sections");

    appPreferences.setPreviewLayout(2);
    appPreferences.setSidePanelMode(1);
    appPreferences.setSidePanelVisible(false);
    test.expect(waitFor([&] {
                    return window->property("previewLayout").toInt() == 2
                        && window->property("conversionPanelMode").toInt() == 1
                        && !window->property("conversionPanelVisible").toBool();
                }),
                "interface preferences should update the active view and Side Panel immediately");

    QObject* resetInterfaceButton = window->findChild<QObject*>(
        QStringLiteral("resetInterfacePreferencesButton"));
    QObject* resetBehaviorButton = window->findChild<QObject*>(
        QStringLiteral("resetBehaviorPreferencesButton"));
    QObject* resetDefaultsButton = window->findChild<QObject*>(
        QStringLiteral("resetDefaultPreferencesButton"));
    controller.setAutoUpdate(false);
    controller.setLivePreview(true);
    appPreferences.setRememberConversionSettings(false);
    appPreferences.setDefaultPreset(3);
    appPreferences.setDefaultExportFormat(8);
    const bool interfaceReset = resetInterfaceButton != nullptr
        && QMetaObject::invokeMethod(resetInterfaceButton, "click");
    const bool behaviorReset = resetBehaviorButton != nullptr
        && QMetaObject::invokeMethod(resetBehaviorButton, "click");
    const bool defaultsReset = resetDefaultsButton != nullptr
        && QMetaObject::invokeMethod(resetDefaultsButton, "click");
    test.expect(interfaceReset && behaviorReset && defaultsReset
                    && waitFor([&] {
                        return window->property("previewLayout").toInt() == 1
                            && window->property("conversionPanelMode").toInt() == 0
                            && window->property("conversionPanelVisible").toBool()
                            && controller.autoUpdate() && !controller.livePreview()
                            && appPreferences.rememberConversionSettings()
                            && appPreferences.defaultPreset() == 0
                            && appPreferences.defaultExportFormat() == 0;
                    }),
                "each Settings section should expose a working reset button");
    if (preferencesDialog != nullptr)
        QMetaObject::invokeMethod(preferencesDialog, "close");

    const auto visiblePane = [window](const QString& objectName) -> QObject* {
        for (QObject* pane : window->findChildren<QObject*>(objectName)) {
            if (pane->property("visible").toBool()) return pane;
        }
        return nullptr;
    };

    test.expect(!window->icon().isNull(),
                "application window should load the RetroVDP Studio icon");
    const auto hasConfiguredWatermark = [&](const QString& paneName) {
        QObject* pane = visiblePane(paneName);
        if (pane == nullptr) return false;
        QObject* viewport = pane->findChild<QObject*>(paneName + QStringLiteral("Viewport"));
        QObject* watermark = pane->findChild<QObject*>(paneName + QStringLiteral("Watermark"));
        if (viewport == nullptr || watermark == nullptr) return false;
        const qreal expectedSize = qMin(viewport->property("width").toReal(),
                                        viewport->property("height").toReal())
            / 2.0;
        return watermark->property("source").toUrl().isValid()
            && qAbs(watermark->property("opacity").toReal() - 0.12) <= 0.001
            && qAbs(watermark->property("width").toReal() - expectedSize) <= 1.0
            && qAbs(watermark->property("height").toReal() - expectedSize) <= 1.0;
    };
    test.expect(hasConfiguredWatermark(QStringLiteral("sourcePreview"))
                    && hasConfiguredWatermark(QStringLiteral("convertedPreview")),
                "both preview panes should carry a subdued half-size logo watermark");

    test.expect(window->findChild<QObject*>(QStringLiteral("mainMenuBar")) != nullptr,
                "application should expose its commands through a menu bar");
    QObject* projectBar = window->findChild<QObject*>(QStringLiteral("projectBar"));
    QObject* projectBarContent = window->findChild<QObject*>(
        QStringLiteral("projectBarContent"));
    QObject* projectBarName = window->findChild<QObject*>(QStringLiteral("projectBarName"));
    QObject* activeTargetCombo = window->findChild<QObject*>(QStringLiteral("activeTargetCombo"));
    QObject* projectSettingsAction = window->findChild<QObject*>(
        QStringLiteral("projectSettingsAction"));
    const bool activeTargetComboSynchronized = activeTargetCombo != nullptr
        && waitFor([&] {
               return activeTargetCombo->property("currentIndex").toInt() >= 0
                   && activeTargetCombo->property("currentValue").toInt()
                       == editorProject.activeTarget()
                   && activeTargetCombo->property("currentText").toString()
                       == editorProject.activeTargetInfo()
                              .value(QStringLiteral("name")).toString();
           });
    test.expect(projectBar != nullptr && projectBarName != nullptr
                    && projectBarName->property("text").toString()
                        == editorProject.projectName()
                    && activeTargetCombo != nullptr
                    && activeTargetCombo->property("count").toInt() == 2
                    && activeTargetComboSynchronized
                    && projectBar->property("height").toReal()
                        <= activeTargetCombo->property("implicitHeight").toReal() + 10.0
                    && projectBarContent != nullptr
                    && projectBarContent->property("y").toReal() >= 0.0
                    && projectBarContent->property("y").toReal()
                        + projectBarContent->property("height").toReal()
                        <= projectBar->property("height").toReal()
                    && projectSettingsAction != nullptr,
                "the project bar should expose project identity and configured active targets");
    const bool projectSettingsOpened = projectSettingsAction != nullptr
        && QMetaObject::invokeMethod(projectSettingsAction, "trigger");
    QObject* projectDialog = window->findChild<QObject*>(QStringLiteral("projectDialog"));
    QObject* targetSupportButton = window->findChild<QObject*>(
        QStringLiteral("targetSupportButton"));
    QObject* targetSupportDialog = window->findChild<QObject*>(
        QStringLiteral("targetSupportDialog"));
    QObject* tmsProjectTarget = window->findChild<QObject*>(
        QStringLiteral("tms9918aProjectTarget"));
    QObject* f18aProjectTarget = window->findChild<QObject*>(
        QStringLiteral("f18aProjectTarget"));
    QObject* v9938ProjectTarget = window->findChild<QObject*>(
        QStringLiteral("v9938ProjectTarget"));
    QObject* segaSmsProjectTarget = window->findChild<QObject*>(
        QStringLiteral("segaSmsProjectTarget"));
    QObject* segaGenesisProjectTarget = window->findChild<QObject*>(
        QStringLiteral("segaGenesisProjectTarget"));
    QObject* huc6270ProjectTarget = window->findChild<QObject*>(
        QStringLiteral("huc6270ProjectTarget"));
    QObject* vicIiProjectTarget = window->findChild<QObject*>(
        QStringLiteral("vicIiProjectTarget"));
    QObject* vicProjectTarget = window->findChild<QObject*>(
        QStringLiteral("vicProjectTarget"));
    test.expect(projectSettingsOpened && projectDialog != nullptr
                    && waitFor([&] { return projectDialog->property("visible").toBool(); })
                    && window->findChild<QObject*>(QStringLiteral("projectNameField")) != nullptr
                    && targetSupportButton == nullptr
                    && targetSupportDialog != nullptr
                    && tmsProjectTarget != nullptr
                    && f18aProjectTarget != nullptr
                    && v9938ProjectTarget != nullptr
                    && window->findChild<QObject*>(QStringLiteral("trs80ProjectTarget")) != nullptr,
                "Project Settings should edit the project name and grouped target roadmap without catalog navigation");
    test.expect(tmsProjectTarget != nullptr
                    && tmsProjectTarget->property("text").toString()
                        == QStringLiteral("TMS9918A")
                    && f18aProjectTarget != nullptr
                    && f18aProjectTarget->property("text").toString()
                        == QStringLiteral("F18A")
                    && v9938ProjectTarget != nullptr
                    && v9938ProjectTarget->property("text").toString()
                        == QStringLiteral("Yamaha V9938")
                    && segaSmsProjectTarget != nullptr
                    && segaSmsProjectTarget->property("enabled").toBool()
                    && segaSmsProjectTarget->property("text").toString()
                        == QStringLiteral("Sega Master System 315-5124 / 315-5246")
                    && segaGenesisProjectTarget != nullptr
                    && segaGenesisProjectTarget->property("enabled").toBool()
                    && segaGenesisProjectTarget->property("text").toString()
                        == QStringLiteral("Sega Genesis / Mega Drive 315-5313 / YM7101")
                    && huc6270ProjectTarget != nullptr
                    && huc6270ProjectTarget->property("enabled").toBool()
                    && huc6270ProjectTarget->property("text").toString()
                        == QStringLiteral("NEC / Hudson HuC6270")
                    && vicIiProjectTarget != nullptr
                    && vicIiProjectTarget->property("enabled").toBool()
                    && vicIiProjectTarget->property("text").toString()
                        == QStringLiteral("MOS VIC-II")
                    && vicProjectTarget != nullptr
                    && vicProjectTarget->property("enabled").toBool()
                    && vicProjectTarget->property("text").toString()
                        == QStringLiteral("MOS VIC (VIC-20)"),
                "implemented project targets should be enabled and omit roadmap status text");
    const bool reducedProjectToOneTarget = f18aProjectTarget != nullptr
        && QMetaObject::invokeMethod(f18aProjectTarget, "click")
        && waitFor([&] {
            return tmsProjectTarget != nullptr
                && tmsProjectTarget->property("checked").toBool()
                && !tmsProjectTarget->property("enabled").toBool()
                && !f18aProjectTarget->property("checked").toBool();
        });
    test.expect(reducedProjectToOneTarget,
                "Project Settings should prevent the final selected target from being unchecked");
    if (projectDialog != nullptr) QMetaObject::invokeMethod(projectDialog, "close");
    QObject* supportedTargetsAction = window->findChild<QObject*>(
        QStringLiteral("supportedTargetsAction"));
    QObject* supportedTargetsMenuItem = window->findChild<QObject*>(
        QStringLiteral("supportedTargetsMenuItem"));
    test.expect(supportedTargetsAction != nullptr
                    && supportedTargetsMenuItem != nullptr
                    && targetSupportDialog != nullptr
                    && targetSupportDialog->property("catalogReady").toBool()
                    && targetSupportDialog->property("targetCount").toInt() == 20
                    && targetSupportDialog->property("categoryCount").toInt() == 5
                    && targetSupportDialog->property("plannedTargetCount").toInt() == 8,
                "Help should expose all 20 bundled Supported Targets entries immediately");
    QObject* aboutAction = window->findChild<QObject*>(QStringLiteral("aboutAction"));
    const bool aboutOpened = aboutAction != nullptr
        && QMetaObject::invokeMethod(aboutAction, "trigger");
    QObject* aboutDialog = window->findChild<QObject*>(QStringLiteral("aboutDialog"));
    QObject* aboutIcon = window->findChild<QObject*>(
        QStringLiteral("aboutApplicationIcon"));
    QObject* aboutVersion = window->findChild<QObject*>(
        QStringLiteral("aboutVersionLabel"));
    test.expect(aboutOpened && aboutDialog != nullptr
                    && waitFor([&] { return aboutDialog->property("visible").toBool(); })
                    && aboutIcon != nullptr
                    && aboutIcon->property("width").toReal() == 64.0
                    && aboutIcon->property("height").toReal() == 64.0
                    && aboutVersion != nullptr
                    && aboutVersion->property("text").toString()
                        == QStringLiteral("Version %1").arg(
                            QCoreApplication::applicationVersion()),
                "About should present the compiled application version and a 64 by 64 application icon");
    if (aboutDialog != nullptr) QMetaObject::invokeMethod(aboutDialog, "close");
    const QObject* exitAction =
        window->findChild<QObject*>(QStringLiteral("exitAction"));
    const QObject* reloadAction =
        window->findChild<QObject*>(QStringLiteral("reloadAction"));
    test.expect(exitAction != nullptr
                    && exitAction->property("text").toString() == QStringLiteral("E&xit")
                    && window->findChild<QObject*>(QStringLiteral("exitMenuSeparator"))
                        != nullptr
                    && window->findChild<QObject*>(QStringLiteral("exitMenuItem")) != nullptr,
                "File menu should end with a separated Exit command");
    test.expect(reloadAction != nullptr && reloadAction->property("enabled").toBool()
                    && window->findChild<QObject*>(QStringLiteral("reloadMenuItem")) != nullptr,
                "File menu should expose Reload for a file-backed source");
    test.expect(window->property("previewLayout").toInt() == 1,
                "horizontal split should remain the default preview layout");
    test.expect(window->property("conversionPanelVisible").toBool()
                    && window->property("conversionPanelMode").toInt() == 0,
                "Side Panel should default to a visible adjacent panel");
    const QObject* screenImageModeItem =
        window->findChild<QObject*>(QStringLiteral("screenImageModeMenuItem"));
    const QObject* characterModeItem =
        window->findChild<QObject*>(QStringLiteral("characterEditorModeMenuItem"));
    const QObject* spriteModeItem =
        window->findChild<QObject*>(QStringLiteral("spriteEditorModeMenuItem"));
    QObject* modeMenu = window->findChild<QObject*>(QStringLiteral("modeMenu"));
    const QObject* showSidePanelItem =
        window->findChild<QObject*>(QStringLiteral("showSidePanelMenuItem"));
    const QObject* sidePanelPlacementMenu =
        window->findChild<QObject*>(QStringLiteral("sidePanelPlacementMenu"));
    const QObject* exportAction =
        window->findChild<QObject*>(QStringLiteral("exportAction"));
    test.expect(window->property("workspaceMode").toInt() == 0
                    && screenImageModeItem != nullptr
                    && screenImageModeItem->property("checked").toBool()
                    && characterModeItem != nullptr
                    && !characterModeItem->property("checked").toBool()
                    && spriteModeItem != nullptr
                    && !spriteModeItem->property("checked").toBool()
                    && modeMenu != nullptr && modeMenu->property("enabled").toBool(),
                "Mode menu should start with Screen Image exclusively selected");
    test.expect(showSidePanelItem != nullptr
                    && showSidePanelItem->property("text").toString().contains(
                        QStringLiteral("Side Panel"))
                    && sidePanelPlacementMenu != nullptr
                    && sidePanelPlacementMenu->property("title").toString().contains(
                        QStringLiteral("Side Panel")),
                "View should use Side Panel terminology");
    test.expect(window->findChild<QObject*>(QStringLiteral("loadRecipeAction")) != nullptr
                    && window->findChild<QObject*>(QStringLiteral("saveRecipeAction")) != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("screenImageActiveTargetGroup")) != nullptr,
                "File and Screen Image controls should expose the shared recipe and active-target workflow");

    const auto hasDestinationShell = [](QObject* pane, const QString& paneName,
                                        const QString& expectedTitle) {
        if (pane == nullptr) return false;
        const QObject* title = pane->findChild<QObject*>(paneName + QStringLiteral("Title"));
        const QObject* toolbar = pane->findChild<QObject*>(
            paneName + QStringLiteral("DrawingToolbarSlot"));
        const QObject* viewport = pane->findChild<QObject*>(
            paneName + QStringLiteral("Viewport"));
        const QObject* controls = pane->findChild<QObject*>(
            paneName + QStringLiteral("Controls"));
        const QObject* zoom = pane->findChild<QObject*>(
            paneName + QStringLiteral("ZoomControls"));
        const QObject* zoomMenuButton = pane->findChild<QObject*>(
            paneName + QStringLiteral("ZoomMenuButton"));
        return title != nullptr && title->property("text").toString() == expectedTitle
            && toolbar != nullptr && viewport != nullptr && controls != nullptr
            && zoom != nullptr && zoomMenuButton != nullptr
            && !zoomMenuButton->property("enabled").toBool();
    };

    window->setProperty("workspaceMode", 1);
    test.expect(waitFor([&] {
                    return hasDestinationShell(
                               visiblePane(QStringLiteral("characterEditorWorkspace")),
                               QStringLiteral("characterEditorWorkspace"),
                               QStringLiteral("Character Editor"))
                        && visiblePane(QStringLiteral("characterEditorSidePanel")) != nullptr
                        && characterModeItem->property("checked").toBool()
                        && !screenImageModeItem->property("checked").toBool()
                        && exportAction != nullptr
                        && !exportAction->property("enabled").toBool();
                }),
                "Character Editor mode should replace the destination and Side Panel safely");
    QObject* characterPane = visiblePane(QStringLiteral("characterEditorWorkspace"));
    QObject* characterZoomMenu = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceZoomMenuButton"))
        : nullptr;
    QObject* characterZoomIn = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceZoomInButton"))
        : nullptr;
    QObject* characterZoomOut = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceZoomOutButton"))
        : nullptr;
    QObject* characterWheelZoom = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceWheelZoomHandler"))
        : nullptr;
    QObject* characterToolbar = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceDrawingToolbarSlot"))
        : nullptr;
    QObject* characterPencil = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterPatternPencilButton"))
        : nullptr;
    QObject* characterExtractionButton = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceImportSourceButton"))
        : nullptr;
    QObject* characterPreviewerButton = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterPatternPreviewerButton"))
        : nullptr;
    QObject* characterExtractionDialog = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterExtractionDialog"))
        : nullptr;
    QObject* characterPreviewerDialog = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterPatternPreviewDialog"))
        : nullptr;
    QObject* characterExtractionRegionWidth = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterExtractionRegionWidth"))
        : nullptr;
    QObject* characterPreviewWidth = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("patternPreviewWidth"))
        : nullptr;
    QObject* characterEraser = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterPatternEraserButton"))
        : nullptr;
    QObject* characterLine = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterPatternLineButton"))
        : nullptr;
    QObject* characterKLine = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterPatternKLineButton"))
        : nullptr;
    QObject* characterRays = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterPatternRaysButton"))
        : nullptr;
    QObject* characterColors = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterPatternColorControl"))
        : nullptr;
    QObject* characterRotate = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("rotateCharacterPatternButton"))
        : nullptr;
    QObject* characterMirror = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("mirrorCharacterPatternButton"))
        : nullptr;
    QObject* characterFlip = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("flipCharacterPatternButton"))
        : nullptr;
    QObject* characterBlank = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("blankCharacterPatternButton"))
        : nullptr;
    QObject* characterCopy = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("copyCharacterPatternButton"))
        : nullptr;
    QObject* characterPaste = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("pasteCharacterPatternButton"))
        : nullptr;
    QObject* characterSetGridButton = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceSetGridButton"))
        : nullptr;
    QObject* characterTilingButton = characterToolbar != nullptr
        ? characterToolbar->findChild<QObject*>(
              QStringLiteral("characterTilingButton"))
        : nullptr;
    QObject* patternEditorModeIcon = characterTilingButton != nullptr
        ? characterTilingButton->findChild<QObject*>(
              QStringLiteral("patternEditorModeIcon"))
        : nullptr;
    QObject* tilingScreenModeIcon = characterTilingButton != nullptr
        ? characterTilingButton->findChild<QObject*>(
              QStringLiteral("tilingScreenModeIcon"))
        : nullptr;
    QObject* characterDetailsTrailing = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceDetailsTrailing"))
        : nullptr;
    QObject* characterControls = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceControls"))
        : nullptr;
    QObject* characterSelectionRow = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterPatternSelectionRow"))
        : nullptr;
    QObject* characterSetCombo = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterSetComboBox"))
        : nullptr;
    QObject* characterPatternSpin = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterPatternSpinBox"))
        : nullptr;
    QObject* characterPatternGrid = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterPatternGrid"))
        : nullptr;
    QObject* characterEditorTray = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterPatternEditorTray"))
        : nullptr;
    QObject* characterEditorGrid = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterPatternEditorGrid"))
        : nullptr;
    QObject* characterTilingView = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterTilingView"))
        : nullptr;
    QObject* characterTilingGrid = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterTilingScreenGrid"))
        : nullptr;
    QObject* addCharacterEditor = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("addCharacterEditorButton"))
        : nullptr;
    QObject* removeCharacterEditor = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("removeCharacterEditorButton"))
        : nullptr;
    QObject* characterLowerToolbar = characterPane != nullptr
        ? characterPane->findChild<QObject*>(
              QStringLiteral("characterEditorWorkspaceCustomLowerToolbar"))
        : nullptr;
    QObject* characterUndo = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("undoCharacterEditButton"))
        : nullptr;
    QObject* characterRedo = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("redoCharacterEditButton"))
        : nullptr;
    QObject* characterPan = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterPanButton"))
        : nullptr;
    QObject* characterPanLeft = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterPanLeftButton"))
        : nullptr;
    QObject* characterPanUp = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterPanUpButton"))
        : nullptr;
    QObject* characterPanCenter = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterPanCenterButton"))
        : nullptr;
    QObject* characterPanDown = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterPanDownButton"))
        : nullptr;
    QObject* characterPanRight = characterPane != nullptr
        ? characterPane->findChild<QObject*>(QStringLiteral("characterPanRightButton"))
        : nullptr;
    if (characterPane != nullptr) {
        characterPane->setProperty("fitToView", false);
        characterPane->setProperty("manualZoom", 1.0);
        const QVariant zoomFactor{2.0};
        QMetaObject::invokeMethod(characterPane, "zoomBy",
                                  Q_ARG(QVariant, zoomFactor));
    }
    test.expect(characterPane != nullptr
                    && characterPatternGrid != nullptr
                    && characterEditorTray != nullptr
                    && characterEditorGrid != nullptr
                    && characterEditorTray->property("renderedEditorCount").toInt()
                        == 1
                    && !characterPane->property("zoomInteractive").toBool()
                    && qAbs(characterPane->property("effectiveZoom").toReal() - 1.0)
                        < 0.001
                    && qAbs(characterPane->property("manualZoom").toReal() - 1.0)
                        < 0.001
                    && characterZoomMenu != nullptr
                    && characterZoomMenu->property("text").toString().contains(
                           QStringLiteral("100%"))
                    && !characterZoomMenu->property("enabled").toBool()
                    && characterZoomIn != nullptr
                    && !characterZoomIn->property("enabled").toBool()
                    && characterZoomOut != nullptr
                    && !characterZoomOut->property("enabled").toBool()
                    && characterWheelZoom != nullptr
                    && !characterWheelZoom->property("enabled").toBool()
                    && addCharacterEditor != nullptr
                    && removeCharacterEditor != nullptr
                    && !removeCharacterEditor->property("enabled").toBool()
                    && characterToolbar != nullptr
                    && characterExtractionButton != nullptr
                    && characterExtractionButton->property("enabled").toBool()
                        == controller.hasConversion()
                    && characterPreviewerButton != nullptr
                    && characterPreviewerButton->property("visible").toBool()
                    && characterExtractionDialog != nullptr
                    && characterPreviewerDialog != nullptr
                    && characterExtractionRegionWidth != nullptr
                    && characterPreviewWidth != nullptr
                    && characterPencil != nullptr && characterEraser != nullptr
                    && characterLine != nullptr && characterKLine != nullptr
                    && characterRays != nullptr
                    && characterColors != nullptr
                    && characterRotate != nullptr && characterRotate->property("enabled").toBool()
                    && characterMirror != nullptr && characterMirror->property("enabled").toBool()
                    && characterFlip != nullptr && characterFlip->property("enabled").toBool()
                    && characterBlank != nullptr && characterBlank->property("enabled").toBool()
                    && characterCopy != nullptr && characterCopy->property("enabled").toBool()
                    && characterPaste != nullptr
                    && characterPaste->property("enabled").toBool()
                        == editorProject.canPasteCharacterPattern()
                    && characterSetGridButton != nullptr
                    && !characterSetGridButton->property("visible").toBool()
                    && characterTilingButton != nullptr
                    && !characterTilingButton->property("checked").toBool()
                    && characterTilingButton->property("modeHint").toString()
                        == QStringLiteral("Change to Tiling Screen")
                    && patternEditorModeIcon != nullptr
                    && tilingScreenModeIcon != nullptr
                    && patternEditorModeIcon->property("z").toReal()
                        > tilingScreenModeIcon->property("z").toReal()
                    && characterTilingView != nullptr
                    && !characterTilingView->property("visible").toBool()
                    && characterPencil->property("implicitWidth").toInt() == 26
                    && characterEraser->property("implicitWidth").toInt() == 26
                    && characterColors->property("implicitWidth").toInt() == 26
                    && characterDetailsTrailing != nullptr
                    && characterDetailsTrailing->property("text").toString().contains(
                           QStringLiteral("Editing for TMS9918A"))
                    && characterControls != nullptr
                    && characterDetailsTrailing->property("y").toReal()
                        < characterControls->property("y").toReal()
                    && characterSelectionRow != nullptr
                    && characterSetCombo != nullptr
                    && qRound(characterSetCombo->property("width").toReal()) == 55
                    && characterSetCombo->property("currentIndex").toInt()
                        == editorProject.activeCharacterSet()
                    && characterPatternSpin != nullptr
                    && qRound(characterPatternSpin->property("width").toReal()) == 48
                    && characterPatternSpin->property("value").toInt()
                        == editorProject.activeCharacterPattern()
                    && characterSelectionRow->property("y").toReal()
                        < characterPatternGrid->property("y").toReal()
                    && characterPane->findChild<QObject*>(
                           QStringLiteral("characterSetTabs")) == nullptr
                    && characterLowerToolbar != nullptr
                    && characterLowerToolbar->property("visible").toBool()
                    && characterUndo != nullptr && characterRedo != nullptr
                    && characterPan != nullptr && characterPan->property("enabled").toBool()
                    && characterPanLeft != nullptr && characterPanUp != nullptr
                    && characterPanCenter != nullptr && characterPanDown != nullptr
                    && characterPanRight != nullptr
                    && !characterPanLeft->property("enabled").toBool()
                    && !characterPanUp->property("enabled").toBool()
                    && !characterPanCenter->property("enabled").toBool()
                    && !characterPanDown->property("enabled").toBool()
                    && !characterPanRight->property("enabled").toBool()
                    && window->findChild<QObject*>(
                           QStringLiteral("characterEditorSidePanelActiveTargetGroup"))
                        != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("characterEditorSidePanelImportFromSourceButton"))
                        != nullptr
                    && window->findChild<QObject*>(
                           QStringLiteral("characterEditorSidePanelPatternPreviewerButton"))
                        != nullptr,
                "Character Editor should expose its pattern tools, reusable drawing tray, target-described pattern set, and active-target summary");
    const bool extractionDialogInvoked = characterExtractionDialog != nullptr
        && QMetaObject::invokeMethod(characterExtractionDialog,
                                     "openForScreenImage");
    test.expect(extractionDialogInvoked && waitFor([&] {
                    return characterExtractionDialog->property("opened").toBool();
                })
                    && characterExtractionRegionWidth->property("value").toInt() >= 1,
                "Character extraction should open a target-aware Screen Image region dialog");
    if (characterExtractionDialog != nullptr)
        QMetaObject::invokeMethod(characterExtractionDialog, "close");
    const bool previewDialogInvoked = characterPreviewerDialog != nullptr
        && QMetaObject::invokeMethod(characterPreviewerDialog, "openPreview");
    test.expect(previewDialogInvoked && waitFor([&] {
                    return characterPreviewerDialog->property("opened").toBool();
                })
                    && characterPreviewWidth->property("value").toInt() >= 1,
                "Pattern Previewer should open with configurable pattern dimensions");
    if (characterPreviewerDialog != nullptr)
        QMetaObject::invokeMethod(characterPreviewerDialog, "close");
    const bool panToggledOn = QMetaObject::invokeMethod(characterPan, "click");
    test.expect(panToggledOn && waitFor([&] {
                    return editorProject.characterPanActive()
                        && characterPan->property("checked").toBool()
                        && characterPanLeft->property("enabled").toBool()
                        && characterPanUp->property("enabled").toBool()
                        && characterPanDown->property("enabled").toBool()
                        && characterPanRight->property("enabled").toBool()
                        && !characterPencil->property("enabled").toBool()
                        && !characterRotate->property("enabled").toBool();
                }),
                "Pan should enable four-way positioning and suspend destructive pattern tools");
    const bool panRightInvoked = QMetaObject::invokeMethod(characterPanRight, "click");
    test.expect(panRightInvoked && waitFor([&] {
                    return editorProject.characterPanX() == 1
                        && characterPanCenter->property("enabled").toBool();
                }),
                "the lower position controls should move through the virtual grid");
    const bool panCentered = QMetaObject::invokeMethod(characterPanCenter, "click");
    test.expect(panCentered && waitFor([&] {
                    return editorProject.characterPanX() == 0
                        && editorProject.characterPanY() == 0
                        && !characterPanCenter->property("enabled").toBool();
                }),
                "the lower center control should restore the live pattern origin");
    const bool panToggledOff = QMetaObject::invokeMethod(characterPan, "click");
    test.expect(panToggledOff && waitFor([&] {
                    return !editorProject.characterPanActive()
                        && !characterPan->property("checked").toBool()
                        && editorProject.characterPanX() == 0
                        && editorProject.characterPanY() == 0
                        && characterPencil->property("enabled").toBool()
                        && characterRotate->property("enabled").toBool();
                }),
                "finishing Pan should reset its controls and restore drawing tools");
    const auto currentPreviewScale = [&] {
        QVariant scale;
        if (characterEditorTray == nullptr
            || !QMetaObject::invokeMethod(characterEditorTray,
                                          "currentActivePreviewScale",
                                          Q_RETURN_ARG(QVariant, scale))) {
            return 0;
        }
        return scale.toInt();
    };
    const auto changePreviewScale = [&](const char* method, int expected) {
        return characterEditorTray != nullptr
            && QMetaObject::invokeMethod(characterEditorTray, method)
            && currentPreviewScale() == expected;
    };
    const bool previewScaleCycle = characterEditorTray != nullptr
        && currentPreviewScale() == 4
        && changePreviewScale("decreaseActivePreviewScale", 3)
        && changePreviewScale("decreaseActivePreviewScale", 2)
        && changePreviewScale("decreaseActivePreviewScale", 1)
        && changePreviewScale("decreaseActivePreviewScale", 1)
        && changePreviewScale("increaseActivePreviewScale", 2)
        && changePreviewScale("increaseActivePreviewScale", 3)
        && changePreviewScale("increaseActivePreviewScale", 4)
        && changePreviewScale("increaseActivePreviewScale", 4);
    test.expect(previewScaleCycle,
                "pattern previews should cycle through bounded 1x, 2x, 3x, and 4x sizes");
    editorProject.setCharacterTilingMode(true);
    test.expect(waitFor([&] {
                    return characterTilingView != nullptr
                        && characterTilingView->property("visible").toBool()
                        && characterTilingView->property("renderedTileCount").toInt()
                            == 1
                        && characterTilingGrid != nullptr
                        && qRound(characterTilingGrid->property("width").toReal())
                            == 256
                        && qRound(characterTilingGrid->property("height").toReal())
                            == 192
                        && characterTilingButton->property("checked").toBool()
                        && characterTilingButton->property("modeHint").toString()
                            == QStringLiteral("Change to Pattern Editor")
                        && tilingScreenModeIcon->property("z").toReal()
                            > patternEditorModeIcon->property("z").toReal()
                        && !characterEditorGrid->property("visible").toBool();
                }),
                "Tiling should replace the editor tray with a 256 by 192 character grid");
    const qreal initialTilingHeight = characterEditorTray->property("height").toReal();
    const QVariant tilingZoomFactor{1.25};
    const bool tilingZoomInvoked = QMetaObject::invokeMethod(
        characterPane, "zoomBy", Q_ARG(QVariant, tilingZoomFactor));
    test.expect(tilingZoomInvoked && waitFor([&] {
                    return characterPane->property("zoomInteractive").toBool()
                        && characterZoomMenu->property("enabled").toBool()
                        && characterZoomIn->property("enabled").toBool()
                        && characterZoomOut->property("enabled").toBool()
                        && characterWheelZoom->property("enabled").toBool()
                        && qAbs(characterPane->property("effectiveZoom").toReal()
                                - 1.25) < 0.001
                        && qRound(characterTilingGrid->property("width").toReal())
                            == 320
                        && qRound(characterTilingGrid->property("height").toReal())
                            == 240
                        && characterEditorTray->property("height").toReal()
                            > initialTilingHeight;
                }),
                "Character zoom should scale the screen grid and grow the Tiling panel height");
    const QVariant tilingZoomResetFactor{0.8};
    QMetaObject::invokeMethod(characterPane, "zoomBy",
                              Q_ARG(QVariant, tilingZoomResetFactor));
    const bool tilingToggledOff = QMetaObject::invokeMethod(
        characterTilingButton, "click");
    test.expect(tilingToggledOff && waitFor([&] {
                    return !editorProject.characterTilingMode()
                        && characterEditorGrid->property("visible").toBool()
                        && !characterTilingView->property("visible").toBool()
                        && characterTilingButton->property("modeHint").toString()
                            == QStringLiteral("Change to Tiling Screen")
                        && patternEditorModeIcon->property("z").toReal()
                            > tilingScreenModeIcon->property("z").toReal()
                        && !characterPane->property("zoomInteractive").toBool()
                        && qAbs(characterPane->property("effectiveZoom").toReal() - 1.0)
                            < 0.001;
                }),
                "deselecting the Tiling button should return to Pattern Edit mode");
    const bool tilingToggledOn = QMetaObject::invokeMethod(
        characterTilingButton, "click");
    test.expect(tilingToggledOn && waitFor([&] {
                    return editorProject.characterTilingMode()
                        && characterTilingView->property("visible").toBool()
                        && characterTilingButton->property("modeHint").toString()
                            == QStringLiteral("Change to Pattern Editor")
                        && tilingScreenModeIcon->property("z").toReal()
                            > patternEditorModeIcon->property("z").toReal();
                }),
                "selecting the Tiling button should restore screen placement mode");
    editorProject.addCharacterEditor();
    test.expect(waitFor([&] {
                    return characterEditorTray != nullptr
                        && characterEditorTray->property("renderedEditorCount").toInt()
                            == 2
                        && characterEditorTray->property("activeEditorEmpty").toBool()
                        && characterTilingView->property("renderedTileCount").toInt()
                            == 2;
                }),
                "the shared add tool should create an active empty tile in Tiling view");
    const int sharedPattern = editorProject.activeCharacterPattern();
    editorProject.setActiveCharacterPattern(sharedPattern);
    editorProject.moveCharacterTile(1, 13, 19);
    const QVariantList tiledSlots = editorProject.characterEditorSlots();
    test.expect(waitFor([&] {
                    return characterTilingView->property("relatedTileCount").toInt()
                            == 2
                        && characterEditorTray->property("renderedEditorCount").toInt()
                            == 1;
                })
                    && tiledSlots.size() == 2
                    && tiledSlots.at(1).toMap().value(QStringLiteral("tileX")).toInt()
                        == 16
                    && tiledSlots.at(1).toMap().value(QStringLiteral("tileY")).toInt()
                        == 16,
                "duplicate pattern tiles should highlight together, snap independently, and share one Pattern Editor");
    editorProject.setCharacterTilingMode(false);
    test.expect(waitFor([&] {
                    return characterEditorGrid->property("visible").toBool()
                        && !characterTilingView->property("visible").toBool()
                        && editorProject.activeCharacterEditor() == 1
                        && editorProject.activeCharacterPattern() == sharedPattern
                        && characterEditorTray->property("renderedEditorCount").toInt()
                            == 1;
                }),
                "switching presentations should preserve the active tile while filtering duplicate pattern editors");
    editorProject.removeActiveCharacterEditor();
    window->setProperty("workspaceMode", 2);
    test.expect(waitFor([&] {
                    return hasDestinationShell(
                               visiblePane(QStringLiteral("spriteEditorWorkspace")),
                               QStringLiteral("spriteEditorWorkspace"),
                               QStringLiteral("Sprite Editor"))
                        && visiblePane(QStringLiteral("spriteEditorSidePanel")) != nullptr
                        && spriteModeItem->property("checked").toBool()
                        && !characterModeItem->property("checked").toBool();
                }),
                "Sprite Editor mode should replace the destination and Side Panel safely");
    QObject* spritePane = visiblePane(QStringLiteral("spriteEditorWorkspace"));
    QObject* spriteSetView = visiblePane(QStringLiteral("spriteSetView"));
    QObject* spriteEditorTray = visiblePane(
        QStringLiteral("spritePatternEditorTray"));
    QObject* spritePencil = visiblePane(QStringLiteral("spritePatternPencilButton"));
    QObject* spriteLine = visiblePane(QStringLiteral("spritePatternLineButton"));
    QObject* spriteKLine = visiblePane(QStringLiteral("spritePatternKLineButton"));
    QObject* spriteRays = visiblePane(QStringLiteral("spritePatternRaysButton"));
    QObject* spriteModeButton = visiblePane(QStringLiteral("spritePlacementModeButton"));
    QObject* spriteColorControl = visiblePane(
        QStringLiteral("spritePatternColorControl"));
    QObject* spriteColorSwatch = visiblePane(QStringLiteral("spriteColorSwatch"));
    QObject* spriteTransparencySwatch = visiblePane(
        QStringLiteral("spriteTransparencySwatch"));
    QObject* spritePalettePopup = window->findChild<QObject*>(
        QStringLiteral("spritePatternPalettePopup"));
    QObject* spriteGlobalSizeControl = visiblePane(
        QStringLiteral("spriteEditorSidePanelGlobalSpriteSizeComboBox"));
    QObject* spriteSizeControl = window->findChild<QObject*>(
        QStringLiteral("spriteEditorSidePanelActiveSpriteSizeComboBox"));
    QObject* spriteDepthControl = window->findChild<QObject*>(
        QStringLiteral("spriteEditorSidePanelSpriteColorDepthComboBox"));
    test.expect(spritePane != nullptr && spriteSetView != nullptr
                    && spriteEditorTray != nullptr
                    && spriteEditorTray->property("renderedEditorCount").toInt() == 1
                    && spriteSetView->property("visibleEditorSlots").toList().size() == 1
                    && !spriteSetView->property("visibleEditorSlots").toList()
                            .at(0).toMap().value(QStringLiteral("loaded")).toBool()
                    && spriteSetView->property("sprite8Expanded").toBool()
                    && !spriteSetView->property("sprite16Expanded").toBool()
                    && spritePencil != nullptr && spriteLine != nullptr
                    && spriteKLine != nullptr && spriteRays != nullptr
                    && spriteModeButton != nullptr
                    && spriteColorControl != nullptr
                    && spriteColorSwatch != nullptr
                    && spriteTransparencySwatch != nullptr
                    && spritePalettePopup != nullptr
                    && spriteGlobalSizeControl != nullptr
                    && spriteSizeControl != nullptr
                    && !spriteSizeControl->property("visible").toBool()
                    && spriteDepthControl != nullptr
                    && !spriteDepthControl->property("visible").toBool(),
                "Sprite Editor should show only the active target's applicable sprite controls");
    const bool sprite16BankActivated = spriteSetView != nullptr
        && QMetaObject::invokeMethod(spriteSetView, "activateSpriteBank",
                                     Q_ARG(QVariant, QVariant(16)));
    test.expect(sprite16BankActivated && waitFor([&] {
                    const QVariantList visibleEditors =
                        spriteSetView->property("visibleEditorSlots").toList();
                    return editorProject.spriteGlobalSize() == 16
                        && visibleEditors.size() == 1
                        && !visibleEditors.at(0).toMap()
                                .value(QStringLiteral("loaded")).toBool()
                        && spriteSetView->property("sprite16Expanded").toBool()
                        && !spriteSetView->property("sprite8Expanded").toBool();
                }),
                "a new 9918A sprite bank should activate with one empty editor waiting for a pattern");
    editorProject.selectSpritePattern(0, 16);
    test.expect(waitFor([&] {
                    const QVariantList visibleEditors =
                        spriteSetView->property("visibleEditorSlots").toList();
                    return visibleEditors.size() == 1
                        && visibleEditors.at(0).toMap()
                               .value(QStringLiteral("loaded")).toBool()
                        && visibleEditors.at(0).toMap()
                               .value(QStringLiteral("spriteIndex")).toInt() == 0;
                }),
                "selecting a 16 by 16 pattern should fill the bank's existing empty editor");
    if (spriteSetView != nullptr) {
        QMetaObject::invokeMethod(spriteSetView, "activateSpriteBank",
                                  Q_ARG(QVariant, QVariant(8)));
    }
    test.expect(waitFor([&] {
                    const QVariantList visibleEditors =
                        spriteSetView->property("visibleEditorSlots").toList();
                    return editorProject.spriteGlobalSize() == 8
                        && visibleEditors.size() == 1
                        && !visibleEditors.at(0).toMap()
                                .value(QStringLiteral("loaded")).toBool()
                        && spriteEditorTray
                               ->property("renderedEditorCount").toInt() == 1;
                }),
                "returning to an unused 8 by 8 bank should restore its empty editor");
    editorProject.selectSpritePattern(0, 8);
    if (spriteSetView != nullptr) {
        QMetaObject::invokeMethod(spriteSetView, "activateSpriteBank",
                                  Q_ARG(QVariant, QVariant(16)));
    }
    test.expect(waitFor([&] {
                    const QVariantList visibleEditors =
                        spriteSetView->property("visibleEditorSlots").toList();
                    return editorProject.spriteGlobalSize() == 16
                        && visibleEditors.size() == 1
                        && visibleEditors.at(0).toMap()
                               .value(QStringLiteral("loaded")).toBool()
                        && visibleEditors.at(0).toMap()
                               .value(QStringLiteral("size")).toInt() == 16
                        && spriteEditorTray
                               ->property("renderedEditorCount").toInt() == 1;
                }),
                "switching to 16 by 16 should restore its previously loaded editor");
    if (spriteSetView != nullptr) {
        QMetaObject::invokeMethod(spriteSetView, "activateSpriteBank",
                                  Q_ARG(QVariant, QVariant(8)));
    }
    test.expect(waitFor([&] {
                    const QVariantList visibleEditors =
                        spriteSetView->property("visibleEditorSlots").toList();
                    return editorProject.spriteGlobalSize() == 8
                        && editorProject.activeSpriteEditor() == 0
                        && visibleEditors.size() == 1
                        && visibleEditors.at(0).toMap()
                               .value(QStringLiteral("loaded")).toBool()
                        && visibleEditors.at(0).toMap()
                               .value(QStringLiteral("size")).toInt() == 8
                        && spriteEditorTray
                               ->property("renderedEditorCount").toInt() == 1;
                }),
                "switching back to 8 by 8 should restore its loaded editor without a second pattern click");
    const bool spriteColorPickerInvoked = spriteColorControl != nullptr
        && QMetaObject::invokeMethod(spriteColorControl, "openColorPicker");
    test.expect(spriteColorPickerInvoked && waitFor([&] {
                    return spritePalettePopup->property("visible").toBool()
                        && spritePalettePopup->property("pickerTitle").toString()
                            == QStringLiteral("TMS9918A sprite color")
                        && spriteColorControl->property("colorHint").toString()
                            .contains(QStringLiteral("Choose Sprite Color"))
                        && spritePalettePopup
                               ->property("maximumSelectableColorIndex").toInt() == 15;
                }),
                "the 9918A Sprite Color picker should expose all fifteen opaque hardware colors and explain the transparency shortcut");
    const bool spriteColorChosen = spritePane != nullptr
        && QMetaObject::invokeMethod(spritePane, "chooseActivePaletteColor",
                                     Q_ARG(QVariant, QVariant(2)));
    const QColor expectedSpriteColor =
        editorProject.characterPaletteColors().at(2).value<QColor>();
    test.expect(spriteColorChosen && waitFor([&] {
                    const QVariantList placements =
                        editorProject.activeSpritePlacements();
                    return editorProject.spriteDrawingColorIndex() == 2
                        && placements.at(editorProject.activeSprite()).toMap()
                               .value(QStringLiteral("color")).toInt() == 2
                        && spriteColorSwatch->property("color").value<QColor>()
                            == expectedSpriteColor;
                }),
                "choosing a 9918A sprite color should keep the toolbar swatch and sprite attribute synchronized");
    editorProject.setActiveTarget(1);
    const bool targetControlsSwitched = waitFor([&] {
                    return spriteSetView != nullptr && spritePalettePopup
                               ->property("maximumSelectableColorIndex").toInt() == 1
                        && visiblePane(QStringLiteral(
                               "spriteEditorSidePanelActiveSpriteSizeComboBox")) != nullptr
                        && visiblePane(QStringLiteral(
                               "spriteEditorSidePanelSpriteColorDepthComboBox")) != nullptr
                        && !spriteGlobalSizeControl->property("visible").toBool()
                        && spriteSetView->property("sprite8Expanded").toBool()
                        && spriteSetView->property("sprite16Expanded").toBool();
                });
    test.expect(targetControlsSwitched,
                "switching the Project Bar target should replace global sprite settings with per-sprite size and color-depth controls");
    QObject* sprite8Bank = visiblePane(QStringLiteral("sprite8PatternBank"));
    QObject* sprite16Bank = visiblePane(QStringLiteral("sprite16PatternBank"));
    QObject* sprite8Grid = visiblePane(QStringLiteral("sprite8PatternGrid"));
    QObject* sprite16Grid = visiblePane(QStringLiteral("sprite16PatternGrid"));
    test.expect(sprite8Bank != nullptr && sprite16Bank != nullptr
                    && sprite8Grid != nullptr && sprite16Grid != nullptr
                    && qAbs(sprite8Grid->property("cellWidth").toReal()
                            * 2.0
                            - sprite16Grid->property("cellWidth").toReal())
                        <= 2.0,
                "8 by 8 sprite pattern boxes should use one quarter the area of 16 by 16 boxes while both banks expand normally");
    editorProject.setActiveTarget(0);
    QMetaObject::invokeMethod(spritePalettePopup, "close");
    const bool spritePlacementToggled = spriteModeButton != nullptr
        && QMetaObject::invokeMethod(spriteModeButton, "click");
    const bool placementVisible = waitFor([&] {
                    return visiblePane(QStringLiteral("spritePlacementWorkspace")) != nullptr;
                });
    if (!placementVisible) {
        const auto placements = window->findChildren<QObject*>(
            QStringLiteral("spritePlacementWorkspace"));
        std::cerr << "Placement workspace state: instances=" << placements.size()
                  << " mode=" << editorProject.spritePlacementMode() << '\n';
    }
    QObject* spritePlacementBox = visiblePane(QStringLiteral("spritePlacementBox"));
    const QVariant spriteZoomFactor{1.25};
    const bool spriteZoomInvoked = spritePane != nullptr
        && QMetaObject::invokeMethod(spritePane, "zoomBy",
                                     Q_ARG(QVariant, spriteZoomFactor));
    test.expect(spritePlacementToggled && placementVisible
                    && editorProject.activeSpritePlacements().size() == 32
                    && spritePlacementBox != nullptr && spriteZoomInvoked
                    && waitFor([&] {
                        return spritePane->property("zoomInteractive").toBool()
                            && qRound(spritePlacementBox->property("width").toReal())
                                == 320
                            && qRound(spritePlacementBox->property("height").toReal())
                                == 240;
                    }),
                "Sprite placement should overlap 32 sprites on a zoomable pixel-coordinate screen");
    const bool spriteEditorToggled = QMetaObject::invokeMethod(
        spriteModeButton, "click");
    test.expect(spriteEditorToggled && waitFor([&] {
                    return !editorProject.spritePlacementMode()
                        && spriteEditorTray != nullptr
                        && spriteEditorTray
                               ->property("renderedEditorCount").toInt() == 1
                        && !spritePane->property("zoomInteractive").toBool()
                        && qAbs(spritePane->property("effectiveZoom").toReal() - 1.0)
                            < 0.001;
                }),
                "the shared Sprite Editor/Placement control should return to pixel editing and reset workspace zoom");
    window->setProperty("workspaceMode", 0);
    test.expect(waitFor([&] {
                    return visiblePane(QStringLiteral("convertedPreview")) != nullptr
                        && visiblePane(QStringLiteral("screenImageSidePanel")) != nullptr
                        && screenImageModeItem->property("checked").toBool();
                }),
                "Screen Image mode should restore conversion preview and controls");

    const auto verifyCollapsedSection = [&](const QString& toggleName,
                                            const QString& groupName,
                                            const QString& backgroundName,
                                            const char* existsMessage,
                                            const char* colorMessage,
                                            const char* collapsedMessage,
                                            const char* expandedMessage) {
        QObject* sectionToggle = nullptr;
        for (QObject* toggle : window->findChildren<QObject*>(toggleName)) {
            if (toggle->property("visible").toBool()) {
                sectionToggle = toggle;
                break;
            }
        }
        auto* sectionGroup = sectionToggle != nullptr
            ? sectionToggle->parent()->findChild<QObject*>(
                groupName, Qt::FindDirectChildrenOnly)
            : nullptr;
        const QObject* sectionBackground = sectionToggle != nullptr
            ? sectionToggle->findChild<QObject*>(backgroundName)
            : nullptr;
        const QColor sectionColor = sectionBackground != nullptr
            ? sectionBackground->property("color").value<QColor>()
            : QColor{};
        test.expect(sectionToggle != nullptr && sectionGroup != nullptr, existsMessage);
        test.expect(sectionColor.isValid() && sectionColor.blue() > sectionColor.red(),
                    colorMessage);
        if (sectionToggle == nullptr || sectionGroup == nullptr) return;

        test.expect(!sectionGroup->property("visible").toBool(), collapsedMessage);
        sectionToggle->setProperty("checked", true);
        test.expect(waitFor([&] {
                        const qreal gap = sectionGroup->property("y").toReal()
                            - sectionToggle->property("y").toReal()
                            - sectionToggle->property("height").toReal();
                        return sectionGroup->property("visible").toBool()
                            && sectionGroup->property("title").toString().isEmpty()
                            && gap >= 0.0 && gap <= 4.5;
                    }),
                    expandedMessage);
        sectionToggle->setProperty("checked", false);
        test.expect(waitFor([&] { return !sectionGroup->property("visible").toBool(); }),
                    collapsedMessage);
    };

    QObject* artStyleToggle = nullptr;
    for (QObject* toggle : window->findChildren<QObject*>(
             QStringLiteral("artStyleSettingsToggle"))) {
        if (toggle->property("visible").toBool()) {
            artStyleToggle = toggle;
            break;
        }
    }
    auto* artStyleGroup = artStyleToggle != nullptr
        ? artStyleToggle->parent()->findChild<QObject*>(
            QStringLiteral("recommendedSettingsGroup"), Qt::FindDirectChildrenOnly)
        : nullptr;
    const QObject* artStyleBackground = artStyleToggle != nullptr
        ? artStyleToggle->findChild<QObject*>(
              QStringLiteral("artStyleSettingsToggleBackground"))
        : nullptr;
    const QColor artStyleColor = artStyleBackground != nullptr
        ? artStyleBackground->property("color").value<QColor>()
        : QColor{};
    test.expect(artStyleToggle != nullptr && artStyleGroup != nullptr,
                "conversion panel should expose its Art Style section");
    test.expect(artStyleToggle != nullptr
                    && artStyleToggle->property("checked").toBool()
                    && artStyleToggle->property("text").toString().contains(
                        QStringLiteral("Art Style"))
                    && artStyleGroup != nullptr
                    && artStyleGroup->property("visible").toBool()
                    && artStyleGroup->property("title").toString().isEmpty(),
                "Art Style should replace Recommended starting point and start expanded");
    test.expect(artStyleColor.isValid() && artStyleColor.blue() > artStyleColor.red(),
                "Art Style expander should use a light-blue background");
    if (artStyleToggle != nullptr && artStyleGroup != nullptr) {
        artStyleToggle->setProperty("checked", false);
        test.expect(waitFor([&] { return !artStyleGroup->property("visible").toBool(); }),
                    "Art Style should collapse from its header");
        artStyleToggle->setProperty("checked", true);
    }

    verifyCollapsedSection(QStringLiteral("commonSettingsToggle"),
                           QStringLiteral("commonSettingsGroup"),
                           QStringLiteral("commonSettingsToggleBackground"),
                           "conversion panel should expose its common settings section",
                           "Common settings expander should use a light-blue background",
                           "common settings should collapse from their header",
                           "expanded common settings should have no redundant title and a tight gap");
    verifyCollapsedSection(QStringLiteral("framingSettingsToggle"),
                           QStringLiteral("framingSettingsGroup"),
                           QStringLiteral("framingSettingsToggleBackground"),
                           "conversion panel should expose its framing and scale section",
                           "Framing and scale expander should use a light-blue background",
                           "framing and scale settings should collapse from their header",
                           "expanded framing settings should have no redundant title and a tight gap");
    verifyCollapsedSection(QStringLiteral("workingPaletteSettingsToggle"),
                           QStringLiteral("workingPaletteSettingsGroup"),
                           QStringLiteral("workingPaletteSettingsToggleBackground"),
                           "conversion panel should expose its working palette section",
                           "Working palette expander should use a light-blue background",
                           "working palette settings should collapse from their header",
                           "expanded working palette should have no redundant title and a tight gap");
    verifyCollapsedSection(QStringLiteral("exportSettingsToggle"),
                           QStringLiteral("exportSettingsGroup"),
                           QStringLiteral("exportSettingsToggleBackground"),
                           "conversion panel should expose its export section",
                           "Export expander should use a light-blue background",
                           "export settings should collapse from their header",
                           "expanded export settings should have no redundant title and a tight gap");

    QObject* advancedToggle = nullptr;
    for (QObject* toggle : window->findChildren<QObject*>(
             QStringLiteral("advancedSettingsToggle"))) {
        if (toggle->property("visible").toBool()) {
            advancedToggle = toggle;
            break;
        }
    }
    auto* advancedGroup = advancedToggle != nullptr
        ? advancedToggle->parent()->findChild<QObject*>(
            QStringLiteral("advancedSettingsGroup"), Qt::FindDirectChildrenOnly)
        : nullptr;
    const QObject* advancedToggleBackground = advancedToggle != nullptr
        ? advancedToggle->findChild<QObject*>(
              QStringLiteral("advancedSettingsToggleBackground"))
        : nullptr;
    const QColor advancedToggleColor = advancedToggleBackground != nullptr
        ? advancedToggleBackground->property("color").value<QColor>()
        : QColor{};
    test.expect(advancedToggle != nullptr && advancedGroup != nullptr,
                "conversion panel should expose its advanced settings section");
    test.expect(advancedToggleColor.isValid()
                    && advancedToggleColor.blue() > advancedToggleColor.red(),
                "Advanced Settings expander should use a light-blue background");
    if (advancedToggle != nullptr && advancedGroup != nullptr) {
        advancedToggle->setProperty("checked", true);
        const QObject* errorAccumulationCombo = advancedGroup->findChild<QObject*>(
            QStringLiteral("errorAccumulationCombo"));
        const QObject* ditherModeCombo = window->findChild<QObject*>(
            QStringLiteral("ditherModeCombo"));
        const QObject* orderedMapSizeCombo = advancedGroup->findChild<QObject*>(
            QStringLiteral("orderedDitherMapSizeCombo"));
        const QObject* errorWeightGrid = advancedGroup->findChild<QObject*>(
            QStringLiteral("errorWeightGrid"));
        const QObject* errorRightSpinBox = advancedGroup->findChild<QObject*>(
            QStringLiteral("errorRightSpinBox"));
        const QObject* errorWeightTotalLabel = advancedGroup->findChild<QObject*>(
            QStringLiteral("errorWeightTotalLabel"));
        const QObject* orderedBrightnessSlider = advancedGroup->findChild<QObject*>(
            QStringLiteral("orderedBrightnessSlider"));
        const QObject* maximumColorShiftSlider = advancedGroup->findChild<QObject*>(
            QStringLiteral("maximumColorShiftSlider"));
        const QObject* perceptualWeightsGrid = advancedGroup->findChild<QObject*>(
            QStringLiteral("perceptualWeightsGrid"));
        const QObject* restorePerceptualWeightsButton = advancedGroup->findChild<QObject*>(
            QStringLiteral("restorePerceptualWeightsButton"));
        const QObject* paletteSelectionCombo = advancedGroup->findChild<QObject*>(
            QStringLiteral("paletteSelectionCombo"));
        const QObject* scanlineStaticColorCount = advancedGroup->findChild<QObject*>(
            QStringLiteral("scanlineStaticColorCountSpinBox"));
        test.expect(waitFor([&] {
                        const qreal gap = advancedGroup->property("y").toReal()
                            - advancedToggle->property("y").toReal()
                            - advancedToggle->property("height").toReal();
                        return advancedGroup->property("visible").toBool()
                            && advancedGroup->property("title").toString().isEmpty()
                            && gap >= 0.0 && gap <= 4.5;
                    }),
                    "expanded advanced settings should have no redundant title and a tight gap");
        test.expect(errorAccumulationCombo != nullptr
                        && errorAccumulationCombo->property("currentIndex").toInt()
                            == controller.errorAccumulationMode(),
                    "Advanced Settings should expose Average/Accumulate error handling");
        test.expect(ditherModeCombo != nullptr
                        && ditherModeCombo->property("count").toInt() == 8,
                    "Dithering choices should include the editable Custom mode");
        test.expect(orderedMapSizeCombo != nullptr
                        && orderedMapSizeCombo->property("currentIndex").toInt()
                            == (controller.orderedDitherMapSize() == 4 ? 1 : 0)
                        && orderedMapSizeCombo->property("enabled").toBool()
                            == (controller.ditherMode() == 5 || controller.ditherMode() == 6),
                    "Advanced Settings should expose the 2x2/4x4 ordered pattern");
        test.expect(errorWeightGrid != nullptr && errorRightSpinBox != nullptr
                        && errorWeightTotalLabel != nullptr
                        && errorRightSpinBox->property("value").toInt()
                            == controller.errorRight()
                        && errorWeightGrid->property("enabled").toBool()
                            == (controller.ditherMode() != 0
                                && controller.ditherMode() != 5),
                    "Advanced Settings should expose the editable six-cell error kernel");
        test.expect(orderedBrightnessSlider != nullptr
                        && orderedBrightnessSlider->property("from").toInt() == 0
                        && orderedBrightnessSlider->property("to").toInt() == 16
                        && maximumColorShiftSlider != nullptr
                        && maximumColorShiftSlider->property("to").toInt() == 100,
                    "legacy brightness and color-shift controls should expose their full ranges");
        test.expect(perceptualWeightsGrid != nullptr
                        && restorePerceptualWeightsButton != nullptr
                        && paletteSelectionCombo != nullptr
                        && scanlineStaticColorCount != nullptr,
                    "Advanced Settings should expose perceptual and F18A parity controls");
        advancedToggle->setProperty("checked", false);
    }

    window->setProperty("previewLayout", 0);
    test.expect(waitFor([&] {
                    const QObject* tabbed =
                        window->findChild<QObject*>(QStringLiteral("tabbedPreviewLayout"));
                    const QObject* sourceTab =
                        window->findChild<QObject*>(QStringLiteral("sourcePreviewTab"));
                    const QObject* convertedTab =
                        window->findChild<QObject*>(QStringLiteral("convertedPreviewTab"));
                    const QObject* sourceTitle =
                        window->findChild<QObject*>(QStringLiteral("sourcePreviewTitle"));
                    const QObject* convertedTitle =
                        window->findChild<QObject*>(QStringLiteral("convertedPreviewTitle"));
                    const QObject* sourceViewport =
                        window->findChild<QObject*>(QStringLiteral("sourcePreviewViewport"));
                    const QObject* sourceControls =
                        window->findChild<QObject*>(QStringLiteral("sourcePreviewControls"));
                    return tabbed != nullptr && sourceTab != nullptr && convertedTab != nullptr
                        && sourceTitle != nullptr && convertedTitle != nullptr
                        && !sourceTitle->property("visible").toBool()
                        && !convertedTitle->property("visible").toBool()
                        && sourceViewport != nullptr && sourceControls != nullptr
                        && sourceControls->property("y").toReal()
                            > sourceViewport->property("y").toReal()
                        && sourceTab->property("text").toString() == QStringLiteral("Source")
                        && convertedTab->property("text").toString()
                            == QStringLiteral("Screen Image");
                }),
                "tabbed layout should expose Source and Screen Image tabs");

    window->setProperty("previewLayout", 2);
    test.expect(waitFor([&] {
                    const QObject* vertical = window->findChild<QObject*>(
                        QStringLiteral("verticalPreviewLayout"));
                    const QObject* source = visiblePane(QStringLiteral("sourcePreview"));
                    const QObject* converted =
                        visiblePane(QStringLiteral("convertedPreview"));
                    return vertical != nullptr && source != nullptr && converted != nullptr
                        && qAbs(source->property("width").toReal()
                                - converted->property("width").toReal())
                            <= 1.0
                        && qAbs(source->property("height").toReal()
                                - converted->property("height").toReal())
                            <= 1.0;
                }),
                "vertical layout should start with two equal-size stacked panes");

    window->setProperty("previewLayout", 1);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    test.expect(waitFor([&] {
                    const auto horizontalLayouts = window->findChildren<QObject*>(
                        QStringLiteral("horizontalPreviewLayout"));
                    if (horizontalLayouts.size() != 1) return false;
                    const QObject* horizontal = horizontalLayouts.constFirst();
                    const QObject* source = horizontal != nullptr
                        ? horizontal->findChild<QObject*>(
                              QStringLiteral("sourcePreview"))
                        : nullptr;
                    const QObject* converted = horizontal != nullptr
                        ? horizontal->findChild<QObject*>(
                              QStringLiteral("convertedPreview"))
                        : nullptr;
                    bool hasVisibleSourceTitle = false;
                    bool hasNamedConvertedTitle = false;
                    for (const QObject* sourceTitle : window->findChildren<QObject*>(
                             QStringLiteral("sourcePreviewTitle"))) {
                        hasVisibleSourceTitle |= sourceTitle->property("visible").toBool();
                    }
                    for (const QObject* convertedTitle : window->findChildren<QObject*>(
                             QStringLiteral("convertedPreviewTitle"))) {
                        hasNamedConvertedTitle |= convertedTitle->property("visible").toBool()
                            && convertedTitle->property("text").toString()
                                == QStringLiteral("Screen Image");
                    }
                    return horizontal != nullptr && source != nullptr && converted != nullptr
                        && source->property("visible").toBool()
                        && converted->property("visible").toBool()
                        && qAbs(source->property("width").toReal()
                                - converted->property("width").toReal())
                            <= 1.0
                        && qAbs(source->property("height").toReal()
                                - converted->property("height").toReal())
                            <= 1.0
                        && hasVisibleSourceTitle && hasNamedConvertedTitle;
                }),
                "horizontal layout should start with equal-size Source and Screen Image panes");

    QObject* horizontalPreviewLayout = window->findChild<QObject*>(
        QStringLiteral("horizontalPreviewLayout"));
    QObject* sourcePane = horizontalPreviewLayout != nullptr
        ? horizontalPreviewLayout->findChild<QObject*>(
              QStringLiteral("sourcePreview"))
        : nullptr;
    QObject* convertedPane = horizontalPreviewLayout != nullptr
        ? horizontalPreviewLayout->findChild<QObject*>(
              QStringLiteral("convertedPreview"))
        : nullptr;
    const QObject* updateConversionButton =
        window->findChild<QObject*>(QStringLiteral("updateConversionButton"));
    const QObject* livePreviewSwitch =
        window->findChild<QObject*>(QStringLiteral("livePreviewSwitch"));
    const QObject* sourceDetails = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewDetails"))
        : nullptr;
    const QObject* statusSourceName =
        window->findChild<QObject*>(QStringLiteral("statusSourceName"));
    test.expect(window->title() == QStringLiteral("RetroVDP Studio")
                    && sourceDetails != nullptr
                    && !sourceDetails->property("text").toString().contains(
                        controller.sourceName())
                    && statusSourceName != nullptr
                    && statusSourceName->property("text").toString()
                        == controller.sourceName(),
                "the source filename should appear only in the status bar");
    const QObject* sourceTools = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewSourceTools"))
        : nullptr;
    const QObject* drawingTools = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewDrawingTools"))
        : nullptr;
    const QObject* sourceDrawingToolbarSlot = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewDrawingToolbarSlot"))
        : nullptr;
    const QObject* convertedDrawingToolbarSlot = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewDrawingToolbarSlot"))
        : nullptr;
    const QObject* pencilButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewPencilButton"))
        : nullptr;
    const QObject* eraserButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewEraserButton"))
        : nullptr;
    const QObject* lineButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewLineButton"))
        : nullptr;
    const QObject* kLineButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewKLineButton"))
        : nullptr;
    const QObject* raysButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewRaysButton"))
        : nullptr;
    const QObject* colorSwapButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewColorSwapButton"))
        : nullptr;
    const QObject* ellipseButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewEllipseButton"))
        : nullptr;
    const QObject* rectangleButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewRectangleButton"))
        : nullptr;
    const QObject* hardEdgeButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewHardEdgeButton"))
        : nullptr;
    const QObject* brushShapeButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewBrushShapeButton"))
        : nullptr;
    const QObject* shapeFillButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewShapeFillButton"))
        : nullptr;
    const QObject* drawingUndoButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewDrawingUndoButton"))
        : nullptr;
    const QObject* drawingRedoButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewDrawingRedoButton"))
        : nullptr;
    const QObject* drawingDiameter = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewDrawingDiameterField"))
        : nullptr;
    const QObject* drawingDiameterUp = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewDrawingDiameterUpIndicator"))
        : nullptr;
    const QObject* drawingDiameterDown = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewDrawingDiameterDownIndicator"))
        : nullptr;
    QObject* strokeOverlay = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewStrokeOverlay"))
        : nullptr;
    const QObject* brushCursorRing = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewBrushCursorRing"))
        : nullptr;
    const QObject* sourceColorSelector = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewBackgroundColorButton"))
        : nullptr;
    const QObject* sourcePreviewImage = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewImage"))
        : nullptr;
    const QObject* sourceMirrorButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewMirrorButton"))
        : nullptr;
    const QObject* sourceFlipButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewFlipButton"))
        : nullptr;
    const QObject* sourceInvertButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewInvertButton"))
        : nullptr;
    const QObject* sourceRemoveColorButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewRemoveColorButton"))
        : nullptr;
    const QObject* sourceTypeToolButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewTypeToolButton"))
        : nullptr;
    const QObject* sourceClipArtToolButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewClipArtToolButton"))
        : nullptr;
    const QObject* sourceSelectionButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewSelectionButton"))
        : nullptr;
    const QObject* sourceMoveSelectionButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewMoveSelectionButton"))
        : nullptr;
    const QObject* sourceCopyButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewCopyButton"))
        : nullptr;
    const QObject* sourcePasteButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewPasteButton"))
        : nullptr;
    const QObject* sourceClearButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewClearButton"))
        : nullptr;
    const QObject* sourceCharacterSnapButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(
              QStringLiteral("sourcePreviewCharacterSnapButton"))
        : nullptr;
    const QObject* sourceGridButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewGridButton"))
        : nullptr;
    const QObject* sourceZoom = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewZoomControls"))
        : nullptr;
    const QObject* convertedTools = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(QStringLiteral("convertedPreviewConvertedTools"))
        : nullptr;
    const QObject* convertedAutoUpdate = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewAutoUpdateSwitch"))
        : nullptr;
    const QObject* applyScreenImageEditsButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewApplyScreenImageEditsButton"))
        : nullptr;
    const QObject* convertedZoom = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(QStringLiteral("convertedPreviewZoomControls"))
        : nullptr;
    const QObject* convertedDrawingTools = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewDrawingTools"))
        : nullptr;
    const QObject* convertedColorSelector = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewBackgroundColorButton"))
        : nullptr;
    const QObject* convertedLineButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewLineButton"))
        : nullptr;
    const QObject* convertedKLineButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewKLineButton"))
        : nullptr;
    const QObject* convertedRaysButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewRaysButton"))
        : nullptr;
    const QObject* convertedColorSwapButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewColorSwapButton"))
        : nullptr;
    const QObject* convertedInvertButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewInvertButton"))
        : nullptr;
    const QObject* convertedRemoveColorButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewRemoveColorButton"))
        : nullptr;
    const QObject* convertedCharacterSnapButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewCharacterSnapButton"))
        : nullptr;
    const QObject* convertedBrushShapeButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewBrushShapeButton"))
        : nullptr;
    const QObject* convertedMirror = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(QStringLiteral("convertedPreviewMirrorButton"))
        : nullptr;
    const QObject* convertedFlip = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(QStringLiteral("convertedPreviewFlipButton"))
        : nullptr;
    const QObject* convertedTypeTool = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewTypeToolButton"))
        : nullptr;
    const QObject* convertedTypeDialog = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewTypeToolDialog"))
        : nullptr;
    const QObject* convertedTypeFontCombo = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewTypeFontComboBox"))
        : nullptr;
    const QObject* convertedClipArtTool = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewClipArtToolButton"))
        : nullptr;
    const QObject* convertedClipArtDialog = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewClipArtDialog"))
        : nullptr;
    const QObject* convertedClipArtColorMode = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewClipArtColorModeComboBox"))
        : nullptr;
    const QObject* convertedSelection = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewSelectionButton"))
        : nullptr;
    const QObject* convertedMoveSelection = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewMoveSelectionButton"))
        : nullptr;
    const QObject* convertedSelectionOverlay = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewSelectionOverlay"))
        : nullptr;
    const QObject* convertedFloatingOverlay = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(
              QStringLiteral("convertedPreviewFloatingPlacementOverlay"))
        : nullptr;
    const QObject* convertedCopy = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(QStringLiteral("convertedPreviewCopyButton"))
        : nullptr;
    const QObject* convertedPaste = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(QStringLiteral("convertedPreviewPasteButton"))
        : nullptr;
    const QObject* convertedGridButton = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(QStringLiteral("convertedPreviewGridButton"))
        : nullptr;
    QObject* convertedGridOverlay = convertedPane != nullptr
        ? convertedPane->findChild<QObject*>(QStringLiteral("convertedPreviewGridOverlay"))
        : nullptr;
    const QObject* sourceZoomMenuButton = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewZoomMenuButton"))
        : nullptr;
    const QObject* sourceZoomIn = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewZoomInButton"))
        : nullptr;
    const QObject* sourceZoomOut = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewZoomOutButton"))
        : nullptr;
    const QObject* sourceFitMenuItem = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewFitMenuItem"))
        : nullptr;
    const QObject* sourceActualSizeMenuItem = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewActualSizeMenuItem"))
        : nullptr;
    const qreal zoomStepContentWidth = sourceZoomIn != nullptr && sourceZoomOut != nullptr
        ? qMax(sourceZoomIn->property("renderedContentWidth").toReal(),
               sourceZoomOut->property("renderedContentWidth").toReal())
        : 0.0;
    test.expect(lineButton != nullptr && lineButton->property("enabled").toBool()
                    && kLineButton != nullptr
                    && kLineButton->property("enabled").toBool()
                    && raysButton != nullptr
                    && raysButton->property("enabled").toBool()
                    && colorSwapButton != nullptr
                    && colorSwapButton->property("enabled").toBool()
                    && ellipseButton != nullptr && ellipseButton->property("enabled").toBool()
                    && rectangleButton != nullptr
                    && rectangleButton->property("enabled").toBool()
                    && hardEdgeButton != nullptr
                    && hardEdgeButton->property("enabled").toBool()
                    && brushShapeButton != nullptr
                    && brushShapeButton->property("enabled").toBool()
                    && shapeFillButton != nullptr
                    && shapeFillButton->property("enabled").toBool(),
                "source drawing toolbar should expose line, K-Line, shape, brush, edge, and fill controls");
    test.expect(sourceMirrorButton != nullptr
                    && sourceMirrorButton->property("visible").toBool()
                    && sourceFlipButton != nullptr
                    && sourceFlipButton->property("visible").toBool()
                    && sourceInvertButton != nullptr
                    && sourceInvertButton->property("visible").toBool()
                    && sourceRemoveColorButton != nullptr
                    && sourceRemoveColorButton->property("visible").toBool()
                    && sourceTypeToolButton != nullptr
                    && sourceTypeToolButton->property("visible").toBool()
                    && sourceClipArtToolButton != nullptr
                    && sourceClipArtToolButton->property("visible").toBool()
                    && sourceSelectionButton != nullptr
                    && sourceSelectionButton->property("visible").toBool()
                    && sourceMoveSelectionButton != nullptr
                    && sourceMoveSelectionButton->property("visible").toBool()
                    && sourceCopyButton != nullptr
                    && sourceCopyButton->property("visible").toBool()
                    && sourcePasteButton != nullptr
                    && sourcePasteButton->property("visible").toBool()
                    && sourceClearButton != nullptr
                    && sourceClearButton->property("visible").toBool()
                    && sourceCharacterSnapButton != nullptr
                    && !sourceCharacterSnapButton->property("visible").toBool()
                    && sourceGridButton != nullptr
                    && !sourceGridButton->property("visible").toBool(),
                "source drawing toolbar should expose generic image-editing tools while hiding target-only snap and grid controls");
    test.expect(convertedTools != nullptr && convertedAutoUpdate != nullptr
                    && applyScreenImageEditsButton != nullptr,
                "Converted lower toolbar should expose automatic source conversion and explicit chipset-rule application");
    test.expect(convertedDrawingTools != nullptr
                    && convertedDrawingTools->property("visible").toBool()
                    && convertedColorSelector != nullptr
                    && convertedColorSelector->property("enabled").toBool()
                    && convertedLineButton != nullptr
                    && convertedLineButton->property("enabled").toBool()
                    && convertedKLineButton != nullptr
                    && convertedKLineButton->property("enabled").toBool()
                    && convertedRaysButton != nullptr
                    && convertedRaysButton->property("enabled").toBool()
                    && convertedColorSwapButton != nullptr
                    && convertedColorSwapButton->property("enabled").toBool()
                    && convertedInvertButton != nullptr
                    && convertedInvertButton->property("enabled").toBool()
                    && convertedRemoveColorButton != nullptr
                    && convertedRemoveColorButton->property("enabled").toBool()
                    && convertedCharacterSnapButton != nullptr
                    && convertedCharacterSnapButton->property("enabled").toBool()
                    && convertedBrushShapeButton != nullptr
                    && convertedBrushShapeButton->property("enabled").toBool()
                    && convertedMirror != nullptr
                    && convertedMirror->property("enabled").toBool()
                    && convertedFlip != nullptr
                    && convertedSelection != nullptr
                    && convertedSelection->property("enabled").toBool()
                    && convertedMoveSelection != nullptr
                    && !convertedMoveSelection->property("enabled").toBool()
                    && convertedSelectionOverlay != nullptr
                    && convertedFloatingOverlay != nullptr
                    && convertedCopy != nullptr && convertedPaste != nullptr
                    && convertedGridButton != nullptr,
                "Screen Image should expose drawing, color, selection, move, transform, clipboard, and grid tools");
    test.expect(convertedTypeTool != nullptr
                    && convertedTypeTool->property("enabled").toBool(),
                "Screen Image should expose an enabled Type tool");
    test.expect(convertedTypeDialog != nullptr && convertedTypeFontCombo != nullptr
                    && convertedTypeFontCombo->property("count").toInt() > 0,
                "Type dialog should expose the populated font selector");
    test.expect(convertedClipArtTool != nullptr
                    && convertedClipArtTool->property("enabled").toBool(),
                "Screen Image should expose an enabled Slide/ClipArt tool");
    test.expect(convertedClipArtDialog != nullptr
                    && convertedClipArtColorMode != nullptr
                    && convertedClipArtColorMode->property("count").toInt() == 3,
                "Slide/ClipArt dialog should expose all three color modes");
    bool screenGridChangesWithZoom = false;
    if (convertedPane != nullptr && convertedGridOverlay != nullptr) {
        convertedPane->setProperty("gridVisible", true);
        convertedPane->setProperty("fitToView", false);
        convertedPane->setProperty("manualZoom", 4.0);
        QCoreApplication::processEvents();
        const bool characterGrid = convertedGridOverlay->property("visible").toBool()
            && !convertedGridOverlay->property("pixelGrid").toBool()
            && convertedGridOverlay->property("gridStep").toInt() == 8
            && convertedGridOverlay->property("patternGridStep").toInt() == 8
            && convertedGridOverlay->property("patternGridVisible").toBool()
            && convertedGridOverlay->property("drawsOuterBorder").toBool();
        convertedPane->setProperty("manualZoom", 5.0);
        QCoreApplication::processEvents();
        screenGridChangesWithZoom = characterGrid
            && convertedGridOverlay->property("pixelGrid").toBool()
            && convertedGridOverlay->property("gridStep").toInt() == 1
            && convertedGridOverlay->property("patternGridVisible").toBool()
            && convertedGridOverlay->property("patternGridStep").toInt() == 8
            && convertedGridOverlay->property("drawsOuterBorder").toBool();
        convertedPane->setProperty("fitToView", true);
    }
    test.expect(screenGridChangesWithZoom,
                "Screen Image grid should keep bordered 8x8 pattern lines beneath the pixel grid at 500 percent");
    bool characterBoundSnapping = false;
    if (convertedPane != nullptr) {
        convertedPane->setProperty("fitToView", false);
        convertedPane->setProperty("manualZoom", 1.0);
        convertedPane->setProperty("snapDrawingToCharacterBounds", true);
        convertedPane->setProperty("drawingDiameter", 3);
        convertedPane->setProperty("hardDrawingEdges", true);
        QCoreApplication::processEvents();
        QVariant hardStartPoint;
        QVariant hardEndPoint;
        const bool foundStart = QMetaObject::invokeMethod(
            convertedPane, "drawingStartPoint", Q_RETURN_ARG(QVariant, hardStartPoint),
            Q_ARG(QVariant, QVariant(19.0)), Q_ARG(QVariant, QVariant(21.0)),
            Q_ARG(QVariant, QVariant(4)));
        const bool foundEnd = QMetaObject::invokeMethod(
            convertedPane, "drawingEndPoint", Q_RETURN_ARG(QVariant, hardEndPoint),
            Q_ARG(QVariant, QVariant(19.0)), Q_ARG(QVariant, QVariant(21.0)),
            Q_ARG(QVariant, QVariant(4)));
        const QPointF hardStart = hardStartPoint.toPointF();
        const QPointF hardEnd = hardEndPoint.toPointF();

        convertedPane->setProperty("hardDrawingEdges", false);
        QVariant softStartPoint;
        QVariant softEndPoint;
        const bool foundSoftStart = QMetaObject::invokeMethod(
            convertedPane, "drawingStartPoint", Q_RETURN_ARG(QVariant, softStartPoint),
            Q_ARG(QVariant, QVariant(19.0)), Q_ARG(QVariant, QVariant(21.0)),
            Q_ARG(QVariant, QVariant(4)));
        const bool foundSoftEnd = QMetaObject::invokeMethod(
            convertedPane, "drawingEndPoint", Q_RETURN_ARG(QVariant, softEndPoint),
            Q_ARG(QVariant, QVariant(19.0)), Q_ARG(QVariant, QVariant(21.0)),
            Q_ARG(QVariant, QVariant(4)));
        const QPointF softStart = softStartPoint.toPointF();
        const QPointF softEnd = softEndPoint.toPointF();

        convertedPane->setProperty("drawingDiameter", 12);
        convertedPane->setProperty("hardDrawingEdges", true);
        QVariant cappedDiameter;
        const bool foundCappedDiameter = QMetaObject::invokeMethod(
            convertedPane, "drawingDiameterForTool",
            Q_RETURN_ARG(QVariant, cappedDiameter),
            Q_ARG(QVariant, QVariant(4)));
        characterBoundSnapping = foundStart && foundEnd
            && qAbs(hardStart.x() - 17.5) < 0.01
            && qAbs(hardStart.y() - 17.5) < 0.01
            && qAbs(hardEnd.x() - 22.5) < 0.01
            && qAbs(hardEnd.y() - 22.5) < 0.01
            && foundSoftStart && foundSoftEnd
            && qAbs(softStart.x() - 18.0) < 0.01
            && qAbs(softStart.y() - 18.0) < 0.01
            && qAbs(softEnd.x() - 22.0) < 0.01
            && qAbs(softEnd.y() - 22.0) < 0.01
            && foundCappedDiameter && cappedDiameter.toInt() == 8;
        convertedPane->setProperty("drawingDiameter", 1);
        convertedPane->setProperty("hardDrawingEdges", false);
        convertedPane->setProperty("snapDrawingToCharacterBounds", false);
        convertedPane->setProperty("fitToView", true);
    }
    test.expect(characterBoundSnapping,
                "character snap should inset hard and soft brush footprints inside the selected cell");
    test.expect(convertedTools != nullptr && convertedZoom != nullptr
                    && convertedTools->parent() == convertedZoom->parent(),
                "Converted update and zoom controls should share a toolbar row");
    test.expect(convertedTools != nullptr && convertedZoom != nullptr
                    && qAbs(convertedTools->property("y").toReal()
                            - convertedZoom->property("y").toReal())
                        <= 1.0,
                "Converted update and zoom controls should align vertically");
    test.expect(sourcePane == nullptr
                    || sourcePane->findChild<QObject*>(
                           QStringLiteral("sourcePreviewAutoUpdateSwitch"))
                        == nullptr,
                "automatic conversion update should be removed from Source controls");
    bool constrainedShapePreview = false;
    if (strokeOverlay != nullptr) {
        const QVariant startX{10.0};
        const QVariant startY{12.0};
        const QVariant ellipse{true};
        const QVariant endX{36.0};
        const QVariant endY{20.0};
        const QVariant locked{true};
        const bool began = QMetaObject::invokeMethod(
            strokeOverlay, "beginShape", Q_ARG(QVariant, startX),
            Q_ARG(QVariant, startY), Q_ARG(QVariant, ellipse));
        const bool updated = QMetaObject::invokeMethod(
            strokeOverlay, "updateShape", Q_ARG(QVariant, endX),
            Q_ARG(QVariant, endY), Q_ARG(QVariant, locked));
        constrainedShapePreview = began && updated
            && qAbs(qAbs(strokeOverlay->property("shapeEndX").toReal()
                         - strokeOverlay->property("shapeStartX").toReal())
                    - qAbs(strokeOverlay->property("shapeEndY").toReal()
                           - strokeOverlay->property("shapeStartY").toReal()))
                < 0.01;
        QMetaObject::invokeMethod(strokeOverlay, "cancelShape");
    }
    test.expect(constrainedShapePreview,
                "Shift-drag shape preview should lock displayed width and height");
    bool constrainedLinePreview = false;
    if (strokeOverlay != nullptr) {
        const QVariant startX{10.0};
        const QVariant startY{12.0};
        const QVariant endX{36.0};
        const QVariant endY{20.0};
        const QVariant locked{true};
        const bool began = QMetaObject::invokeMethod(
            strokeOverlay, "beginLine", Q_ARG(QVariant, startX),
            Q_ARG(QVariant, startY));
        const bool updated = QMetaObject::invokeMethod(
            strokeOverlay, "updateLine", Q_ARG(QVariant, endX),
            Q_ARG(QVariant, endY), Q_ARG(QVariant, locked));
        constrainedLinePreview = began && updated
            && qAbs(strokeOverlay->property("shapeEndY").toReal()
                    - strokeOverlay->property("shapeStartY").toReal()) < 0.01
            && qAbs(strokeOverlay->property("shapeEndX").toReal()
                    - endX.toReal()) < 0.01;
        QMetaObject::invokeMethod(strokeOverlay, "cancelShape");
    }
    test.expect(constrainedLinePreview,
                "Shift-drag line preview should lock to its dominant axis");
    test.expect(sourcePane != nullptr && convertedPane != nullptr
                    && sourceTools != nullptr && drawingTools != nullptr
                    && sourceDrawingToolbarSlot != nullptr
                    && convertedDrawingToolbarSlot != nullptr
                    && qAbs(sourceDrawingToolbarSlot->property("height").toReal()
                            - convertedDrawingToolbarSlot->property("height").toReal())
                        <= 1.0
                    && pencilButton != nullptr && pencilButton->property("enabled").toBool()
                    && eraserButton != nullptr && eraserButton->property("enabled").toBool()
                    && drawingUndoButton != nullptr
                    && drawingUndoButton->property("enabled").toBool()
                        == controller.canUndoDrawing()
                    && drawingRedoButton != nullptr
                    && drawingRedoButton->property("enabled").toBool()
                        == controller.canRedoDrawing()
                    && drawingDiameter != nullptr
                    && drawingDiameter->property("enabled").toBool()
                    && drawingDiameter->property("width").toReal() <= 48.0
                    && brushShapeButton != nullptr
                    && brushShapeButton->parent() == drawingDiameter->parent()
                    && qAbs(drawingDiameter->property("x").toReal()
                            - brushShapeButton->property("x").toReal()
                            - brushShapeButton->property("width").toReal())
                        <= 2.0
                    && drawingDiameterUp != nullptr
                    && drawingDiameterUp->property("visible").toBool()
                    && drawingDiameterDown != nullptr
                    && drawingDiameterDown->property("visible").toBool()
                    && strokeOverlay != nullptr
                    && brushCursorRing != nullptr
                    && qAbs(brushCursorRing->property("cursorDiameter").toReal()
                            - qMax(1.0,
                                   sourcePane->property("drawingDiameter").toReal()
                                       * sourcePane->property("effectiveZoom").toReal()))
                        < 0.01
                    && sourcePreviewImage != nullptr
                    && !sourcePreviewImage->property("asynchronous").toBool()
                    && sourcePreviewImage->property("retainWhileLoading").toBool()
                    && sourceZoom != nullptr
                    && convertedZoom != nullptr
                    && sourceTools->parent() == sourceZoom->parent()
                    && qAbs(sourceTools->property("y").toReal()
                            - sourceZoom->property("y").toReal())
                        <= 1.0
                    && sourceColorSelector != nullptr
                    && sourceColorSelector->property("enabled").toBool()
                    && sourcePane->findChild<QObject*>(
                        QStringLiteral("sourcePreviewEyedropperButton"))
                        != nullptr
                    && sourcePane->findChild<QObject*>(
                        QStringLiteral("sourcePreviewCenterButton"))
                        != nullptr
                    && sourcePane->findChild<QObject*>(
                        QStringLiteral("sourcePreviewUpdateButton"))
                        == nullptr
                    && updateConversionButton != nullptr
                    && livePreviewSwitch != nullptr
                    && sourceZoomMenuButton != nullptr
                    && sourceZoomMenuButton->property("text").toString().contains(
                        QLatin1Char('%'))
                    && sourceZoomMenuButton->property("width").toReal()
                        <= sourceZoomMenuButton->property("renderedContentWidth").toReal()
                            + 5.0
                    && sourceZoomMenuButton->property("leftPadding").toReal() <= 2.0
                    && sourceZoomMenuButton->property("rightPadding").toReal() <= 2.0
                    && sourceZoomIn != nullptr && sourceZoomOut != nullptr
                    && sourceZoomIn->property("width").toReal()
                        <= zoomStepContentWidth + 5.0
                    && sourceZoomOut->property("width").toReal()
                        <= zoomStepContentWidth + 5.0
                    && sourceZoomIn->property("leftPadding").toReal() <= 2.0
                    && sourceZoomIn->property("rightPadding").toReal() <= 2.0
                    && sourceZoomOut->property("leftPadding").toReal() <= 2.0
                    && sourceZoomOut->property("rightPadding").toReal() <= 2.0
                    && sourceZoomIn->property("height").toReal()
                        <= sourceZoomMenuButton->property("height").toReal() / 2.0 + 1.0
                    && sourceZoomOut->property("height").toReal()
                        <= sourceZoomMenuButton->property("height").toReal() / 2.0 + 1.0
                    && sourceFitMenuItem != nullptr
                    && !sourceFitMenuItem->property("checkable").toBool()
                    && sourceActualSizeMenuItem != nullptr
                    && !sourceActualSizeMenuItem->property("checkable").toBool()
                    && convertedPane->findChild<QObject*>(
                        QStringLiteral("convertedPreviewZoomMenuButton"))
                        != nullptr
                    && convertedPane->findChild<QObject*>(
                        QStringLiteral("convertedPreviewFitMenuItem"))
                        != nullptr,
                "both panes should expose compact popup-based zoom controls");

    QObject* colorPicker = sourcePane != nullptr
        ? sourcePane->findChild<QObject*>(QStringLiteral("sourcePreviewColorPickerPopup"))
        : nullptr;
    QObject* colorPickerTabs = colorPicker != nullptr
        ? colorPicker->findChild<QObject*>(QStringLiteral("sourcePreviewColorPickerTabs"))
        : nullptr;
    QObject* usedColorGrid = colorPicker != nullptr
        ? colorPicker->findChild<QObject*>(QStringLiteral("sourcePreviewUsedColorGrid"))
        : nullptr;
    const bool pickerOpened = colorPicker != nullptr
        && QMetaObject::invokeMethod(colorPicker, "open");
    if (colorPickerTabs != nullptr) colorPickerTabs->setProperty("currentIndex", 2);
    test.expect(pickerOpened && colorPickerTabs != nullptr && usedColorGrid != nullptr
                    && waitFor([&] {
                           return usedColorGrid->property("visible").toBool()
                               && usedColorGrid->property("count").toInt()
                                   == controller.sourceUsedColors().size();
                       }),
                "used-color tab should display every unique source-image color");
    if (colorPicker != nullptr) QMetaObject::invokeMethod(colorPicker, "close");

    if (sourcePane != nullptr) {
        sourcePane->setProperty("manualZoom", 1.0);
        sourcePane->setProperty("fitToView", true);
        QCoreApplication::processEvents();
        const qreal fittedZoom = sourcePane->property("effectiveZoom").toReal();
        const QVariant zoomFactor{1.25};
        const bool zoomInvoked = QMetaObject::invokeMethod(
            sourcePane, "zoomBy", Q_ARG(QVariant, zoomFactor));
        test.expect(zoomInvoked && !sourcePane->property("fitToView").toBool()
                        && qAbs(sourcePane->property("manualZoom").toReal()
                                - qBound(0.125, fittedZoom * 1.25, 16.0))
                            < 0.001,
                    "first manual zoom step should start from the fitted zoom");
        sourcePane->setProperty("fitToView", true);
    }

    test.expect(window->findChild<QObject*>(QStringLiteral("powerPaintFramingCheckBox"))
                        != nullptr
                    && window->findChild<QObject*>(QStringLiteral("workingPaletteSettingsGroup"))
                        != nullptr
                    && window->findChild<QObject*>(QStringLiteral("resetWorkingPaletteButton"))
                        != nullptr,
                "the interface should expose PowerPaint framing and editable working palettes");

    auto* adjacentPanel =
        window->findChild<QObject*>(QStringLiteral("adjacentConversionPanel"));
    auto* overlayPanel =
        window->findChild<QObject*>(QStringLiteral("overlayConversionPanel"));
    auto* overlayRail =
        window->findChild<QObject*>(QStringLiteral("overlayExpandRail"));
    auto* overlayExpandButton =
        window->findChild<QObject*>(QStringLiteral("overlayExpandButton"));
    const QObject* overlayExpandButtonBackground = overlayExpandButton != nullptr
        ? overlayExpandButton->findChild<QObject*>(
              QStringLiteral("overlayExpandButtonBackground"))
        : nullptr;
    test.expect(adjacentPanel != nullptr && overlayPanel != nullptr
                    && overlayRail != nullptr && overlayExpandButton != nullptr,
                "both conversion-panel placements and the overlay reopen rail should exist");
    test.expect(overlayRail != nullptr
                    && !overlayRail->property("outlineColor").isValid(),
                "hidden overlay rail should not paint a full-height bounding frame");
    test.expect(overlayExpandButtonBackground != nullptr
                    && overlayExpandButtonBackground->property("color")
                           .value<QColor>().alpha() > 0,
                "hidden overlay expand button should keep a visible themed background");
    const QColor outlineColor = sourcePane->property("outlineColor").value<QColor>();
    bool themedOutlines = outlineColor.isValid() && outlineColor.alpha() > 0;
    for (const auto& name : {QStringLiteral("recommendedSettingsGroup"),
                             QStringLiteral("commonSettingsGroup"),
                             QStringLiteral("framingSettingsGroup"),
                             QStringLiteral("advancedSettingsGroup"),
                             QStringLiteral("exportSettingsGroup")}) {
        const QObject* box = window->findChild<QObject*>(name);
        themedOutlines &= box != nullptr
            && box->property("outlineColor").value<QColor>() == outlineColor;
    }
    themedOutlines &= convertedPane->property("outlineColor").value<QColor>() == outlineColor
        && adjacentPanel->property("outlineColor").value<QColor>() == outlineColor;
    test.expect(themedOutlines,
                "pane and settings outlines should share the active palette text color");
    window->setProperty("conversionPanelVisible", false);
    QCoreApplication::processEvents();
    test.expect(adjacentPanel != nullptr && !adjacentPanel->property("visible").toBool(),
                "conversion panel should be hideable");

    window->setProperty("conversionPanelMode", 1);
    test.expect(waitFor([&] {
                    return overlayRail->property("visible").toBool()
                        && !overlayPanel->property("opened").toBool();
                }),
                "hidden overlay should expose its right-edge expand control");
    window->setProperty("conversionPanelVisible", true);
    test.expect(waitFor([&] {
                    const QObject* hideButton =
                        visiblePane(QStringLiteral("overlayHideButton"));
                    return overlayPanel->property("opened").toBool()
                        && !overlayRail->property("visible").toBool()
                        && hideButton != nullptr && hideButton->property("visible").toBool()
                        && hideButton->property("text").toString() == QStringLiteral("›")
                        && overlayExpandButton->property("text").toString()
                            == QStringLiteral("‹")
                        && qAbs(overlayExpandButton->property("x").toReal()
                                + overlayExpandButton->property("width").toReal() / 2.0
                                - overlayRail->property("width").toReal() / 2.0 - 6.0)
                            <= 0.5;
                }),
                "overlay arrows and the expand control should align outward");
    test.expect(QMetaObject::invokeMethod(
                    overlayPanel, "handlePointerPresence", Q_ARG(QVariant, true))
                    && QMetaObject::invokeMethod(
                        overlayPanel, "handlePointerPresence", Q_ARG(QVariant, false)),
                "overlay pointer-presence handling should be callable");
    test.expect(waitFor([&] {
                    return !window->property("conversionPanelVisible").toBool()
                        && overlayRail->property("visible").toBool();
                }),
                "overlay should auto-hide after the pointer enters and leaves it");
    window->setProperty("conversionPanelVisible", true);
    test.expect(waitFor([&] { return overlayPanel->property("opened").toBool(); }),
                "overlay should reopen after automatic hiding");
    window->setProperty("conversionPanelVisible", false);
    test.expect(waitFor([&] {
                    return !overlayPanel->property("visible").toBool()
                        && overlayRail->property("visible").toBool();
                }),
                "hiding the overlay should restore the right-edge expand control");
    window->setProperty("conversionPanelVisible", true);
    window->setProperty("conversionPanelMode", 0);
    test.expect(waitFor([&] {
                    return !overlayPanel->property("opened").toBool()
                        && !overlayPanel->property("visible").toBool()
                        && adjacentPanel->property("visible").toBool();
                }),
                "adjacent placement should restore the docked conversion panel");

    window->setWidth(900);
    window->setHeight(640);
    window->setProperty("layoutWidth", 900);
    QCoreApplication::processEvents();
    test.expect(window->property("compactLayout").toBool(),
                "narrow window should report its compact breakpoint");

    window->setWidth(1280);
    window->setHeight(800);
    window->setProperty("layoutWidth", 1280);
    test.expect(waitFor([&] {
                    const QObject* sourcePreview =
                        window->findChild<QObject*>(QStringLiteral("sourcePreviewImage"));
                    const QObject* convertedPreview =
                        window->findChild<QObject*>(QStringLiteral("convertedPreviewImage"));
                    return sourcePreview != nullptr && convertedPreview != nullptr
                        && sourcePreview->property("status").toInt() == 1
                        && convertedPreview->property("status").toInt() == 1;
                }),
                "source and converted preview images should finish loading in QML");
    test.expect(!window->property("compactLayout").toBool(),
                "wide window should report its full-size breakpoint");
    const QImage screenshot = window->grabWindow();
    test.expect(QGuiApplication::primaryScreen() != nullptr
                    && QGuiApplication::primaryScreen()->devicePixelRatio() > 1.0,
                "fractional-scale test should run on a high-DPI virtual display");
    test.expect(!screenshot.isNull() && screenshot.width() >= 700
                    && screenshot.height() >= 500,
                "fractional-scale offscreen rendering should produce a complete frame");
    screenshot.save(QDir::current().filePath(QStringLiteral("phase6-interface.png")));

    editorProject.configureProjectWithTargets(
        QStringLiteral("Genesis Sprite UI"), false, false,
        QStringList{QStringLiteral("sega-genesis-vdp")});
    controller.setConversionMode(static_cast<int>(
        retrovdp::core::ConversionMode::Mode5GenesisH40));
    editorProject.setWorkspaceMode(2);
    editorProject.setActiveSprite(7);
    editorProject.setActiveSpriteSize(2432);
    editorProject.setActiveSprite(0);
    QObject* genesisSpriteList = nullptr;
    QObject* genesisSpriteGrid = nullptr;
    QObject* genesisSpriteTitle = nullptr;
    const bool genesisUiReady = waitFor([&] {
        spriteSetView = visiblePane(QStringLiteral("spriteSetView"));
        genesisSpriteList = visiblePane(QStringLiteral("genesisSpriteList"));
        genesisSpriteGrid = visiblePane(QStringLiteral("genesisSpriteEntryGrid"));
        genesisSpriteTitle = visiblePane(QStringLiteral("genesisSpriteListTitle"));
        return genesisSpriteList != nullptr && genesisSpriteGrid != nullptr
            && genesisSpriteGrid->property("count").toInt() == 80
            && genesisSpriteTitle != nullptr
            && genesisSpriteTitle->property("text").toString().contains(
                QStringLiteral("80"));
    });
    const bool selectedRectangularGenesisSprite = spriteSetView != nullptr
        && QMetaObject::invokeMethod(spriteSetView, "selectGenesisSprite",
                                     Q_ARG(QVariant, QVariant(7)));
    const QVariantList genesisPlacements = editorProject.activeSpritePlacements();
    test.expect(genesisUiReady
                    && visiblePane(QStringLiteral("sprite8PatternBank")) == nullptr
                    && visiblePane(QStringLiteral("sprite16PatternBank")) == nullptr
                    && selectedRectangularGenesisSprite
                    && editorProject.activeSprite() == 7
                    && editorProject.activeSpriteSize() == 2432
                    && genesisPlacements.at(7).toMap()
                           .value(QStringLiteral("size")).toInt() == 2432,
                "Genesis should use one size-aware sprite list and preserve a selected entry's rectangular geometry");

    editorProject.setActiveSpritePriority(true);
    window->setProperty("workspaceMode", 1);
    editorProject.setCharacterTilingMode(true);
    editorProject.setActiveCharacterPlane(1);
    editorProject.setGenesisCompositePreview(true);
    test.expect(waitFor([&] {
                    return exportAction->property("enabled").toBool()
                        && window->findChild<QObject*>(
                               QStringLiteral("genesisCharacterExportDialog")) != nullptr
                        && window->findChild<QObject*>(
                               QStringLiteral("characterEditorSidePanelGenesisPlaneComboBox")) != nullptr
                        && window->findChild<QObject*>(
                               QStringLiteral("characterEditorSidePanelGenesisTilePriorityCheckBox")) != nullptr
                        && window->findChild<QObject*>(
                               QStringLiteral("characterEditorSidePanelGenesisCompositePreviewCheckBox")) != nullptr;
                })
                    && editorProject.activeCharacterPlane() == 1
                    && editorProject.genesisCompositePreview()
                    && editorProject.activeSpritePriority(),
                "Genesis Character mode should expose layer authoring, priority composition, and native export controls");

    appPreferences.setPreviewLayout(0);
    test.expect(waitFor([&] {
                    return !modeMenu->property("enabled").toBool()
                        && window->findChild<QObject*>(QStringLiteral("sourcePreviewTab")) != nullptr
                        && window->findChild<QObject*>(QStringLiteral("convertedPreviewTab")) != nullptr
                        && window->findChild<QObject*>(QStringLiteral("characterEditorTab")) != nullptr
                        && window->findChild<QObject*>(QStringLiteral("spriteEditorTab")) != nullptr;
                }),
                "Tabbed view should populate Source and every available mode while disabling the Mode menu");

    editorProject.configureProjectWithTargets(
        QStringLiteral("Genesis Sprite UI"), true, false,
        QStringList{QStringLiteral("sega-genesis-vdp")});

    // Instantiate the image-heavy catalog after the workspace lifecycle checks so
    // its delegates cannot contend with workspace delegate incubation on slower CI
    // renderers. The production dialog remains immediate when the user opens it.
    const bool supportedTargetsOpened = supportedTargetsAction != nullptr
        && QMetaObject::invokeMethod(supportedTargetsAction, "trigger");
    const bool targetSupportVisible = targetSupportDialog != nullptr
        && waitFor([&] {
            return targetSupportDialog->property("visible").toBool()
                && window->findChild<QObject*>(
                       QStringLiteral("targetSupportCatalogGrid")) != nullptr;
        });
    test.expect(supportedTargetsOpened && targetSupportVisible,
                "Help should open the categorized Supported Targets catalog");
    const QColor selectedTargetPanelColor = targetSupportDialog != nullptr
        ? targetSupportDialog->property("selectedTargetPanelColor").value<QColor>()
        : QColor{};
    const QColor activeTargetBorderColor = targetSupportDialog != nullptr
        ? targetSupportDialog->property("activeTargetBorderColor").value<QColor>()
        : QColor{};
    test.expect(targetSupportDialog != nullptr
                    && targetSupportDialog->property("activeTargetId").toString()
                        == editorProject.activeTargetInfo()
                               .value(QStringLiteral("id")).toString()
                    && targetSupportDialog->property("activeTargetInCatalog").toBool()
                    && targetSupportDialog->property(
                           "selectedCatalogTargetCount").toInt() == 2
                    && selectedTargetPanelColor.isValid()
                    && qAbs(selectedTargetPanelColor.alphaF() - 0.32) <= 0.01
                    && activeTargetBorderColor == QColor(Qt::white),
                "Supported Targets should tint every selected project target and outline the active target in white");
    if (targetSupportDialog != nullptr)
        QMetaObject::invokeMethod(targetSupportDialog, "close");
}

void testTargetSupportDeliverables(TestContext& test)
{
    const QDir projectRoot(QDir(QStringLiteral(RETROVDP_QML_DIR))
                               .absoluteFilePath(QStringLiteral("../..")));
    const QString catalogPath = projectRoot.filePath(
        QStringLiteral("data/target-support/targets.json"));
    const TargetSupportCatalog bundledCatalog(catalogPath);
    test.expect(bundledCatalog.ready()
                    && bundledCatalog.errorMessage().isEmpty()
                    && bundledCatalog.categories().size() == 5
                    && bundledCatalog.targets().size() == 20,
                "the application catalog loader should synchronously expose all bundled targets");
    QFile catalogFile(catalogPath);
    test.expect(catalogFile.open(QIODevice::ReadOnly),
                "target support catalog should be shipped with the project");
    if (!catalogFile.isOpen()) return;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        catalogFile.readAll(), &parseError);
    test.expect(parseError.error == QJsonParseError::NoError
                    && document.isObject(),
                "target support catalog should be valid JSON");
    if (!document.isObject()) return;

    const QJsonObject root = document.object();
    const QJsonArray categories = root.value(QStringLiteral("categories")).toArray();
    const QJsonArray targets = root.value(QStringLiteral("targets")).toArray();
    int imageCount = 0;
    int plannedCount = 0;
    QSet<QString> categoryIds;
    for (const QJsonValue& categoryValue : categories) {
        const QJsonObject category = categoryValue.toObject();
        categoryIds.insert(category.value(QStringLiteral("id")).toString());
    }
    bool complete = root.value(QStringLiteral("schema")).toString()
                        == QStringLiteral("retrovdp.target-support/v1")
        && categories.size() == 5 && categoryIds.size() == 5
        && targets.size() == 20;
    for (const QJsonValue& targetValue : targets) {
        const QJsonObject target = targetValue.toObject();
        const QString status = target.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("planned")) ++plannedCount;
        const QJsonObject workspaces = target.value(
            QStringLiteral("workspaces")).toObject();
        complete = complete
            && !target.value(QStringLiteral("id")).toString().isEmpty()
            && !target.value(QStringLiteral("name")).toString().isEmpty()
            && !target.value(QStringLiteral("hardware")).toString().isEmpty()
            && categoryIds.contains(
                target.value(QStringLiteral("category")).toString())
            && (status == QStringLiteral("implemented")
                || status == QStringLiteral("planned"))
            && !target.value(QStringLiteral("modes")).toArray().isEmpty()
            && !workspaces.value(QStringLiteral("screenImage")).toString().isEmpty()
            && !workspaces.value(QStringLiteral("character")).toString().isEmpty()
            && !workspaces.value(QStringLiteral("sprite")).toString().isEmpty();

        const QJsonArray images = target.value(QStringLiteral("images")).toArray();
        complete = complete && !images.isEmpty();
        for (const QJsonValue& imageValue : images) {
            ++imageCount;
            const QJsonObject imageEntry = imageValue.toObject();
            const QString local = imageEntry.value(QStringLiteral("local")).toString();
            const QString imagePath = projectRoot.filePath(
                QStringLiteral("app/") + local);
            const QImage image(imagePath);
            complete = complete && !local.isEmpty() && !image.isNull()
                && image.width() > 0 && image.height() > 0
                && QUrl(imageEntry.value(QStringLiteral("url")).toString()).isValid()
                && QUrl(imageEntry.value(QStringLiteral("sourcePage")).toString()).isValid()
                && !imageEntry.value(QStringLiteral("license")).toString().isEmpty()
                && !imageEntry.value(QStringLiteral("credit")).toString().isEmpty();
        }
    }
    test.expect(complete && plannedCount == 8 && imageCount == 29,
                "all 20 target summaries and 29 bundled offline photographs should be complete and decodable");
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("CiscoGarciaFL-Test"));
    application.setApplicationName(QStringLiteral("RetroVDPStudio-InterfaceTest"));
    application.setApplicationVersion(QStringLiteral(RETROVDP_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid()) {
        std::cerr << "Unable to create temporary settings directory\n";
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());
    QSettings().clear();

    TestContext test;
    previousMessageHandler = qInstallMessageHandler(captureQmlMessages);
    ImageInputController controller;
    testApplicationPreferences(test, controller);
    testLiveWorkflow(test, controller);
    testExportWorkflow(test, controller);
    testEditorProjectRecipe(test);
    testTargetSupportDeliverables(test);
    testResponsiveQml(test, controller);
    qInstallMessageHandler(previousMessageHandler);
    test.expect(qmlBindingErrors.load() == 0,
                "QML bindings should remain valid through engine shutdown");

    QSettings().clear();
    if (test.failures != 0) {
        std::cerr << test.failures << " interface workflow test(s) failed\n";
        return 1;
    }
    std::cout << "All interface workflow tests passed\n";
    return 0;
}
