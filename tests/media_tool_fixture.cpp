#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#ifndef MEDIA_TOOL_NAME
#error MEDIA_TOOL_NAME must identify the fixture executable
#endif

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (QStringLiteral(MEDIA_TOOL_NAME) == QStringLiteral("ffmpeg")
        && arguments.contains(QStringLiteral("-start_number"))) {
        QString pattern = arguments.back();
        for (int frame = 1; frame <= 2; ++frame) {
            QString path = pattern;
            path.replace(QStringLiteral("%06d"),
                         QStringLiteral("%1").arg(frame, 6, 10, QLatin1Char('0')));
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write("fixture-png") < 0) return 3;
        }
        return 0;
    }
    if (QStringLiteral(MEDIA_TOOL_NAME) == QStringLiteral("ffmpeg")
        && arguments.contains(QStringLiteral("-vn"))) {
        const QString path = arguments.back();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write("fixture-audio") < 0) return 3;
        return 0;
    }
    if (arguments.contains(QStringLiteral("-show_streams"))) {
        QTextStream(stdout) << R"json({
  "streams": [
    {
      "index": 0,
      "codec_name": "h264",
      "codec_type": "video",
      "width": 960,
      "height": 540,
      "pix_fmt": "yuv420p",
      "sample_aspect_ratio": "1:1",
      "r_frame_rate": "30000/1001",
      "avg_frame_rate": "30000/1001",
      "time_base": "1/30000",
      "start_time": "0.000000",
      "duration": "5.005000",
      "nb_frames": "150",
      "disposition": {"default": 1}
    },
    {
      "index": 1,
      "codec_name": "aac",
      "codec_type": "audio",
      "sample_fmt": "fltp",
      "sample_rate": "48000",
      "channels": 2,
      "channel_layout": "stereo",
      "time_base": "1/48000",
      "start_time": "0.000000",
      "duration": "5.055000",
      "disposition": {"default": 1},
      "tags": {"language": "und"}
    }
  ],
  "chapters": [],
  "format": {
    "format_name": "mov,mp4,m4a,3gp,3g2,mj2",
    "duration": "5.055000"
  }
})json";
        return 0;
    }
    if (arguments.contains(QStringLiteral("-version"))) {
        QTextStream(stdout) << MEDIA_TOOL_NAME << " version 99.0-test\n";
        return 0;
    }
    QTextStream(stderr) << MEDIA_TOOL_NAME << ": expected -version\n";
    return 2;
}
