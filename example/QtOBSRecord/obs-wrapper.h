#pragma once

#if _MSC_VER >= 1600
#pragma execution_character_set("utf-8")
#endif

#include "obs.h"
#include "obs.hpp"

#define OUTPUT_FLV 0

#include <string>
#include <QSize>

#include <QObject>

class QtOBSContext : public QObject
{
    Q_OBJECT

private:
    char *filePath;
    char *liveServer;
    char *liveKey;

    // 参考 struct obs_output
    OBSService rtmpService;

    OBSOutput recordOutput;
    OBSOutput streamOutput;

    OBSEncoder h264Streaming;

    OBSEncoder aacTrack[MAX_AUDIO_MIXES];
    std::string aacEncoderID[MAX_AUDIO_MIXES];

    obs_scene_t *scene;
    obs_source_t *fadeTransition;
    obs_source_t *captureSource;
    obs_properties_t *properties;

    OBSSignal recordingStarted;
    OBSSignal recordingStopping;
    OBSSignal recordingStopped;
    OBSSignal streamingStarted;
    OBSSignal streamingStopping;
    OBSSignal streamingStopped;

    bool recordWhenStreaming;

    int baseWidth;    // 场景画布分辨率
    int baseHeight;
    int outputWidth;  // 输出文件分辨率
    int outputHeight;
    int orgWidth;     // 窗口原始分辨率
    int orgHeight;

    uint64_t lastBytesSent;
    uint64_t lastBytesSentTime;
    int      firstTotal;
    int      firstDropped;

public:
    explicit QtOBSContext(QObject *parent = nullptr);
    ~QtOBSContext();

    enum ErrorType { Init, Record, Stream };

    const QSize getBaseSize() { return QSize(baseWidth, baseHeight); }
    const QSize getOriginalSize() { return QSize(orgWidth, orgHeight); }

signals:
    void initialized();
    void recordStarted();
    void recordStopped();
    void streamStarted();
    void streamStopped();
    void errorOccurred(const int, const QString &);

public slots:
    void initialize(const QString &configPath, const QString &windowTitle,
                    const QSize &screenSize, const QRect &sourceRect);
    void release();

    void resetRecordFilePath(const QString &path);
    void resetStreamLiveUrl(const QString &server, const QString &key);

    void scaleScene(int w, int h);
    void updateVideoSettings(bool cursor = true, bool compatibility = false,
                             bool useWildcards = false);
    void videoCrop(const QRect &);

    void resetAudioInput(const QString &deviceId, const QString &deviceDesc);
    void resetAudioOutput(const QString &deviceId, const QString &deviceDesc);

    /* 处理单声道问题 */
    void downmixMonoInput(bool enable);
    void downmixMonoOutput(bool enable);

    void muteAudioInput(bool);
    void muteAudioOutput(bool);

    void startRecord(const QString &output);
    void stopRecord(bool force);

    void startStream(const QString &server, const QString &key);
    void stopStream(bool force);

    void logStreamStats();

private:
    bool resetAudio();
    int  resetVideo();

    OBSData getStreamEncSettings();
    bool initService();
    bool resetOutputs();

    bool setupRecord();
    bool setupStream();

    void addFilterToSource(obs_source_t *, const char *);
};
