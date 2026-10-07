#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>

#include <cstdint>
#include <vector>

class MediaClipController final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool clipLoaded READ clipLoaded NOTIFY clipChanged)
    Q_PROPERTY(QString clipName READ clipName NOTIFY clipChanged)
    Q_PROPERTY(QString manifestPath READ manifestPath NOTIFY clipChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY clipChanged)
    Q_PROPERTY(QString audioPath READ audioPath NOTIFY clipChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY clipChanged)
    Q_PROPERTY(QString audioFormat READ audioFormat NOTIFY clipChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY clipChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(QUrl currentFrameUrl READ currentFrameUrl NOTIFY currentFrameChanged)
    Q_PROPERTY(qint64 positionMicroseconds READ positionMicroseconds
                   NOTIFY currentFrameChanged)
    Q_PROPERTY(qint64 startMicroseconds READ startMicroseconds NOTIFY clipChanged)
    Q_PROPERTY(qint64 durationMicroseconds READ durationMicroseconds NOTIFY clipChanged)
    Q_PROPERTY(QString framesPerSecond READ framesPerSecond NOTIFY clipChanged)
    Q_PROPERTY(QString sizingMode READ sizingMode NOTIFY clipChanged)
    Q_PROPERTY(bool outputMonitor READ outputMonitor NOTIFY clipChanged)
    Q_PROPERTY(QString outputTarget READ outputTarget NOTIFY clipChanged)
    Q_PROPERTY(QString outputMode READ outputMode NOTIFY clipChanged)
    Q_PROPERTY(QString outputFormat READ outputFormat NOTIFY clipChanged)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY clipChanged)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY clipChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY messagesChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY messagesChanged)

public:
    enum FrameRole {
        OrdinalRole = Qt::UserRole + 1,
        NumberRole,
        FileRole,
        FrameUrlRole,
        PtsMicrosecondsRole,
        DurationMicrosecondsRole,
        TimeLabelRole,
    };

    explicit MediaClipController(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(
        const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool clipLoaded() const { return !frames_.empty(); }
    [[nodiscard]] QString clipName() const { return clipName_; }
    [[nodiscard]] QString manifestPath() const { return manifestPath_; }
    [[nodiscard]] QString sourcePath() const { return sourcePath_; }
    [[nodiscard]] QString audioPath() const { return audioPath_; }
    [[nodiscard]] bool hasAudio() const { return !audioPath_.isEmpty(); }
    [[nodiscard]] QString audioFormat() const { return audioFormat_; }
    [[nodiscard]] int frameCount() const { return static_cast<int>(frames_.size()); }
    [[nodiscard]] int currentFrame() const { return currentFrame_; }
    [[nodiscard]] QUrl currentFrameUrl() const;
    [[nodiscard]] qint64 positionMicroseconds() const;
    [[nodiscard]] qint64 startMicroseconds() const { return startMicroseconds_; }
    [[nodiscard]] qint64 durationMicroseconds() const { return durationMicroseconds_; }
    [[nodiscard]] QString framesPerSecond() const { return framesPerSecond_; }
    [[nodiscard]] QString sizingMode() const { return sizingMode_; }
    [[nodiscard]] bool outputMonitor() const { return outputMonitor_; }
    [[nodiscard]] QString outputTarget() const { return outputTarget_; }
    [[nodiscard]] QString outputMode() const { return outputMode_; }
    [[nodiscard]] QString outputFormat() const { return outputFormat_; }
    [[nodiscard]] int frameWidth() const { return frameWidth_; }
    [[nodiscard]] int frameHeight() const { return frameHeight_; }
    [[nodiscard]] bool playing() const { return playbackTimer_.isActive(); }
    [[nodiscard]] QString statusMessage() const { return statusMessage_; }
    [[nodiscard]] QString errorMessage() const { return errorMessage_; }

    Q_INVOKABLE bool openClipUrl(const QUrl& url);
    Q_INVOKABLE void closeClip();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void seekToFrame(int index);
    Q_INVOKABLE void stepForward();
    Q_INVOKABLE void stepBackward();

signals:
    void clipChanged();
    void currentFrameChanged();
    void playingChanged();
    void messagesChanged();

private:
    struct Frame {
        int ordinal{};
        qint64 number{};
        QString relativeFile;
        QString absoluteFile;
        qint64 ptsMicroseconds{};
        qint64 durationMicroseconds{};
    };

    void clearClipData();
    void setCurrentFrame(int index, bool resetPlaybackClock);
    void setError(QString message);
    void updatePlayback();
    [[nodiscard]] QString timeLabel(qint64 absoluteMicroseconds) const;

    std::vector<Frame> frames_;
    QString clipDirectory_;
    QString manifestPath_;
    QString clipName_;
    QString sourcePath_;
    QString audioPath_;
    QString audioFormat_;
    QString framesPerSecond_;
    QString sizingMode_;
    bool outputMonitor_{};
    QString outputTarget_;
    QString outputMode_;
    QString outputFormat_;
    qint64 startMicroseconds_{};
    qint64 durationMicroseconds_{};
    int frameWidth_{};
    int frameHeight_{};
    int currentFrame_{-1};
    QTimer playbackTimer_;
    QElapsedTimer playbackClock_;
    qint64 playbackOriginMicroseconds_{};
    QString statusMessage_;
    QString errorMessage_;
};
