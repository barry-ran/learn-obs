#include "obs-wrapper.h"

#ifdef _WIN32
#define IS_WIN32 1
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Dwmapi.h>
#include <psapi.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
// obs headers
#include <util/windows/WinHandle.hpp>
#include <util/windows/HRError.hpp>
#include <util/windows/ComPtr.hpp>
#include <util/windows/win-version.h>

#pragma comment(lib, "ole32.lib")

#else
#define IS_WIN32 0
#endif

// obs headers
#include <util/util.hpp>
#include <util/platform.h>
#include <libavcodec/avcodec.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QSysInfo>
#include <QtWin>
#include <QSize>

#include <QDebug>

#define DL_OPENGL "libobs-opengl.dll"
#define DL_D3D11  "libobs-d3d11.dll"

#define TAG "QtOBS"

#define INPUT_AUDIO_SOURCE  "wasapi_input_capture"
#define OUTPUT_AUDIO_SOURCE "wasapi_output_capture"

enum SourceChannels {
    SOURCE_CHANNEL_TRANSITION     = 0, // 淡入淡出
    SOURCE_CHANNEL_AUDIO_OUTPUT      , // 桌面音频设备
    SOURCE_CHANNEL_AUDIO_OUTPUT_2    , // 桌面音频设备 2
    SOURCE_CHANNEL_AUDIO_INPUT       , // 麦克风/辅助音频设备
    SOURCE_CHANNEL_AUDIO_INPUT_2     , // 麦克风/辅助音频设备 2
    SOURCE_CHANNEL_AUDIO_INPUT_3     , // 麦克风/辅助音频设备 3
};

#define AUDIO_BITRATE 128 // kb/s
#define VIDEO_BITRATE 150 // kb/s 用于输出 FLV 格式视频，可自行调整

#define VIDEO_CROP_FILTER_ID "crop_filter"
#define VIDEO_FPS            15

#if OUTPUT_FLV
#define VIDEO_ENCODER_ID           AV_CODEC_ID_FLV1
#define VIDEO_ENCODER_NAME         "flv"
#define RECORD_OUTPUT_FORMAT       "flv"
#define RECORD_OUTPUT_FORMAT_MIME  "video/x-flv"
#else
#define VIDEO_ENCODER_ID           AV_CODEC_ID_H264
#define VIDEO_ENCODER_NAME         "libx264"
#define RECORD_OUTPUT_FORMAT       "mp4"
#define RECORD_OUTPUT_FORMAT_MIME  "video/mp4"
#endif

static const double scaled_vals[] =
{
    1.0,
    1.25,
    (1.0 / 0.75),
    1.5,
    (1.0 / 0.6),
    1.75,
    2.0,
    2.25,
    2.5,
    2.75,
    3.0,
    0.0
};

static log_handler_t DefLogHandler;

#ifdef _WIN32
static bool DisableAudioDucking(bool disable)
{
    ComPtr<IMMDeviceEnumerator>   devEnum;
    ComPtr<IMMDevice>             device;
    ComPtr<IAudioSessionManager2> sessionManager2;
    ComPtr<IAudioSessionControl>  sessionControl;
    ComPtr<IAudioSessionControl2> sessionControl2;

    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                      nullptr, CLSCTX_INPROC_SERVER,
                                      __uuidof(IMMDeviceEnumerator),
                                      (void **)&devEnum);
    if (FAILED(result))
        return false;

    result = devEnum->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result))
        return false;

    result = device->Activate(__uuidof(IAudioSessionManager2),
                              CLSCTX_INPROC_SERVER, nullptr,
                              (void **)&sessionManager2);
    if (FAILED(result))
        return false;

    result = sessionManager2->GetAudioSessionControl(nullptr, 0,
                                                     &sessionControl);
    if (FAILED(result))
        return false;

    result = sessionControl->QueryInterface(&sessionControl2);
    if (FAILED(result))
        return false;

    result = sessionControl2->SetDuckingPreference(disable);
    return SUCCEEDED(result);
}

static void SetAeroEnabled(bool enable)
{
    static HRESULT(WINAPI *func)(UINT) = nullptr;
    static bool failed = false;

    if (!func) {
        if (failed)
            return;

        HMODULE dwm = LoadLibraryW(L"dwmapi");
        if (!dwm) {
            failed = true;
            return;
        }

        func = reinterpret_cast<decltype(func)>(GetProcAddress(dwm,
                                                               "DwmEnableComposition"));
        if (!func) {
            failed = true;
            return;
        }
    }

    func(enable ? DWM_EC_ENABLECOMPOSITION : DWM_EC_DISABLECOMPOSITION);
}

static uint32_t GetWindowsVersion()
{
    static uint32_t ver = 0;

    if (ver == 0) {
        struct win_version_info ver_info;

        get_win_ver(&ver_info);
        ver = (ver_info.major << 8) | ver_info.minor;
    }

    return ver;
}
#endif

static void LogHandler(int level, const char *format, va_list args, void *param)
{
    Q_UNUSED(level);
    Q_UNUSED(param);

    char str[4096];
    vsnprintf(str, sizeof(str), format, args);

    qInfo().noquote() << TAG << str;
}

static void AddFilterToAudioInput(const char *id)
{
    if (id == nullptr || *id == '\0')
        return;

    obs_source_t *source = obs_get_output_source(SOURCE_CHANNEL_AUDIO_INPUT);
    if (!source)
        return;

    obs_source_t *existing_filter;
    std::string name = obs_source_get_display_name(id);
    if (name.empty())
        name = id;
    existing_filter = obs_source_get_filter_by_name(source, name.c_str());
    if (existing_filter) {
        obs_source_release(existing_filter);
        obs_source_release(source);
        return;
    }

    obs_source_t *filter = obs_source_create(id, name.c_str(), nullptr, nullptr);
    if (filter) {
        const char *sourceName = obs_source_get_name(source);

        blog(LOG_INFO, "added filter '%s' (%s) to source '%s'", name.c_str(),
             id, sourceName);

        obs_source_filter_add(source, filter);
        obs_source_release(filter);
    }

    obs_source_release(source);
}

static inline bool HasAudioDevices(const char *source_id)
{
    const char *output_id = source_id;
    obs_properties_t *props = obs_get_source_properties(output_id);
    size_t count = 0;

    if (!props)
        return false;

    obs_property_t *devices = obs_properties_get(props, "device_id");
    if (devices)
        count = obs_property_list_item_count(devices);

    obs_properties_destroy(props);

    return count != 0;
}

static void ResetAudioDevice(const char *sourceId, const char *deviceId,
                             const char *deviceDesc, int channel)
{
    bool disable = deviceId && strcmp(deviceId, "disabled") == 0;
    obs_source_t *source;
    obs_data_t *settings;

    source = obs_get_output_source(channel);
    if (source) {
        if (disable) {
            obs_set_output_source(channel, nullptr);
        } else {
            settings = obs_source_get_settings(source);
            const char *oldId = obs_data_get_string(settings,
                                                    "device_id");
            if (strcmp(oldId, deviceId) != 0) {
                obs_data_set_string(settings, "device_id",
                                    deviceId);
            }
            obs_data_set_bool(settings, "use_device_timing", false);
            obs_source_update(source, settings);
            obs_data_release(settings);
        }

    } else if (!disable) {
        settings = obs_data_create();
        obs_data_set_string(settings, "device_id", deviceId);
        obs_data_set_bool(settings, "use_device_timing", false);
        source = obs_source_create(sourceId, deviceDesc, settings, nullptr);
        obs_data_release(settings);

        obs_set_output_source(channel, source);
    }

    obs_source_release(source);
}

static bool CreateAACEncoder(OBSEncoder &res, std::string &id, const char *name,
                             size_t idx, obs_data_t *setting)
{
    /*
    static const string encoders[] = {
        "ffmpeg_aac",
        "mf_aac",
        "libfdk_aac",
        "CoreAudio_AAC",
    };
    */
    const char *id_ = "ffmpeg_aac";
    id = id_;
    res = obs_audio_encoder_create(id_, name, setting, idx, nullptr);

    if (res) {
        obs_encoder_release(res);
        return true;
    }

    return false;
}

static void AddSource(void *_data, obs_scene_t *scene)
{
    // 为这个 source 创建一个 scene item
    obs_source_t *source = (obs_source_t *)_data;
    obs_scene_add(scene, source);
    obs_source_release(source);
}

static bool FindSceneItemAndScale(obs_scene_t *scene, obs_sceneitem_t *item,
                                  void *param)
{
    Q_UNUSED(scene);

    // scene item 只有一个，它对应的 source 为 captureSource

    QtOBSContext *obs = static_cast<QtOBSContext *>(param);
    QSize baseSize = obs->getBaseSize(), orgSize = obs->getOriginalSize();

    float width = baseSize.width();
    float height = baseSize.height();
    float sratio = height / orgSize.height();
    bool align_width = false;
    float base_ratio = baseSize.width() * 1.f / baseSize.height();
    float orginal_ratio = orgSize.width() * 1.f / orgSize.height();
    if (base_ratio >= orginal_ratio)
        width = height * orginal_ratio;
    else {
        height = width / orginal_ratio;
        sratio = width / orgSize.width();
        align_width = true;
    }

    vec2 scale;
    scale.x = scale.y = sratio;
    obs_sceneitem_set_scale(item, &scale);

    vec2 pos;
    if (align_width)
        vec2_set(&pos, 0, (baseSize.height() - height) / 2);
    else
        vec2_set(&pos, (baseSize.width() - width) / 2, 0);
    obs_sceneitem_set_pos(item, &pos);

    blog(LOG_INFO, "scene scale %f %f.", scale.x, scale.y);
    return true;
}

#define RECORDING_STARTED \
    "==== Recording Started ==============================================="
#define RECORDING_STOPPING \
    "==== Recording Stopping ================================================"
#define RECORDING_STOPPED \
    "==== Recording Stopped ================================================"
#define STREAMING_STARTED \
    "==== Streaming Started ==============================================="
#define STREAMING_STOPPING \
    "==== Streaming Stopping ================================================"
#define STREAMING_STOPPED \
    "==== Streaming Stopped ================================================"
static void RecordingStarted(void *data, calldata_t *params)
{
    Q_UNUSED(params);
    blog(LOG_INFO, RECORDING_STARTED);
    QtOBSContext *handler = static_cast<QtOBSContext *>(data);
    QMetaObject::invokeMethod(handler, "recordStarted");
}

static void RecordingStopping(void *data, calldata_t *params)
{
    Q_UNUSED(data);
    Q_UNUSED(params);
    blog(LOG_INFO, RECORDING_STOPPING);
}

static void RecordingStopped(void *data, calldata_t *params)
{
    blog(LOG_INFO, RECORDING_STOPPED);

    QString msg;
    int code = (int)calldata_int(params, "code");
    switch (code)
    {
    case OBS_OUTPUT_SUCCESS:
        blog(LOG_INFO, "recording finished!");
        Q_UNUSED(data);
        break;
    case OBS_OUTPUT_NO_SPACE:
        msg = "磁盘存储空间不足！";
        break;
    case OBS_OUTPUT_UNSUPPORTED:
        msg = "格式不支持！";
        break;
    default:
        {
            const char *last_error = calldata_string(params, "last_error");
            blog(LOG_ERROR, "record error, code=%d,error=%s", code, last_error);
        }
        msg = QString("发生未指定错误 (Code:%1)！").arg(code);
        break;
    }

    if (!msg.isEmpty()) {
        QtOBSContext *handler = static_cast<QtOBSContext *>(data);
        QMetaObject::invokeMethod(handler, "errorOccurred",
                                  Q_ARG(int, QtOBSContext::Record),
                                  Q_ARG(QString, msg));
    } else {
        QtOBSContext *handler = static_cast<QtOBSContext *>(data);
        QMetaObject::invokeMethod(handler, "recordStopped");
    }
}

static void StreamingStarted(void *data, calldata_t *params)
{
    Q_UNUSED(params);
    blog(LOG_INFO, STREAMING_STARTED);
    QtOBSContext *handler = static_cast<QtOBSContext *>(data);
    QMetaObject::invokeMethod(handler, "streamStarted");
}

static void StreamingStopping(void *data, calldata_t *params)
{
    Q_UNUSED(data);
    Q_UNUSED(params);
    blog(LOG_INFO, STREAMING_STOPPING);
}

static void StreamingStopped(void *data, calldata_t *params)
{
    blog(LOG_INFO, STREAMING_STOPPED);

    QString msg;
    int code = (int)calldata_int(params, "code");
    switch (code)
    {
    case OBS_OUTPUT_SUCCESS:
        blog(LOG_INFO, "streaming finished!");
        Q_UNUSED(data);
        break;
    case OBS_OUTPUT_BAD_PATH:
        msg = "无效的地址！";
        break;
    case OBS_OUTPUT_CONNECT_FAILED:
        msg = "无法连接到服务器！";
        break;
    case OBS_OUTPUT_INVALID_STREAM:
        msg = "无法访问流密钥或无法连接服务器！";
        break;
    case OBS_OUTPUT_ERROR:
        msg = "连接服务器时发生意外错误！";
        break;
    case OBS_OUTPUT_DISCONNECTED:
        msg = "已与服务器断开连接！";
        break;
    default:
        {
            const char *last_error = calldata_string(params, "last_error");
            blog(LOG_ERROR, "stream error, code=%d,error=%s", code, last_error);
        }
        msg = QString("发生未指定错误 (Code:%1)！").arg(code);
        break;
    }

    if (!msg.isEmpty()) {
        QtOBSContext *handler = static_cast<QtOBSContext *>(data);
        QMetaObject::invokeMethod(handler, "errorOccurred",
                                  Q_ARG(int, QtOBSContext::Stream),
                                  Q_ARG(QString, msg));
    } else {
        QtOBSContext *handler = static_cast<QtOBSContext *>(data);
        QMetaObject::invokeMethod(handler, "streamStopped");
    }
}

#define OBS_INIT_BEGIN \
    "==== OBS Init Begin ==============================================="
#define OBS_INIT_END \
    "==== OBS Init End ==============================================="
#define OBS_STARTUP_SEPARATOR \
    "==== OBS Start Complete ==============================================="
#define OBS_RELEASE_BEGIN_SEPARATOR \
    "==== OBS Release Begin ==============================================="
#define OBS_RELEASE_END_SEPARATOR \
    "==== OBS Release End ==============================================="
#define OBS_SEPARATOR "---------------------------------------------"

QtOBSContext::QtOBSContext(QObject *parent) : QObject(parent),
    filePath(nullptr),
    liveServer(nullptr),
    liveKey(nullptr),
    rtmpService(nullptr),
    recordOutput(nullptr),
    streamOutput(nullptr),
    h264Streaming(nullptr),
    scene(nullptr),
    fadeTransition(nullptr),
    captureSource(nullptr),
    properties(nullptr),
    recordWhenStreaming(false)
{
#ifdef _WIN32
    DisableAudioDucking(true);
#endif
    for (size_t i = 0; i < MAX_AUDIO_MIXES; i++)
        aacTrack[i] = nullptr;
    base_get_log_handler(&DefLogHandler, nullptr);
    base_set_log_handler(LogHandler, nullptr);
}

QtOBSContext::~QtOBSContext()
{
#ifdef _WIN32
    DisableAudioDucking(false);
#endif

    if (obs_initialized())
        release();

    obs_shutdown();

    blog(LOG_INFO, "memory leaks: %ld", bnum_allocs());
    base_set_log_handler(nullptr, nullptr);
}

void QtOBSContext::release()
{
    blog(LOG_INFO, OBS_RELEASE_BEGIN_SEPARATOR);

    recordingStarted.Disconnect();
    recordingStopping.Disconnect();
    recordingStopped.Disconnect();
    streamingStarted.Disconnect();
    streamingStopping.Disconnect();
    streamingStopped.Disconnect();

    obs_set_output_source(SOURCE_CHANNEL_TRANSITION, nullptr);
    obs_set_output_source(SOURCE_CHANNEL_AUDIO_OUTPUT, nullptr);
    obs_set_output_source(SOURCE_CHANNEL_AUDIO_INPUT, nullptr);

    auto cb = [] (void *unused, obs_source_t *source)
    {
        obs_source_remove(source);
        Q_UNUSED(unused);
        return true;
    };
    obs_enum_sources(cb, nullptr);

    obs_scene_release(scene);
    obs_properties_destroy(properties);

    scene        = nullptr;
    properties   = nullptr;

    rtmpService    = nullptr;
    h264Streaming  = nullptr;

    for (size_t i = 0; i < MAX_AUDIO_MIXES; ++i)
        aacTrack[i] = nullptr;

    streamOutput = nullptr;
    recordOutput = nullptr;

    free(filePath);
    free(liveServer);
    free(liveKey);

    blog(LOG_INFO, OBS_RELEASE_END_SEPARATOR);
}

// 整个初始化流程，参见 window-basic-main.cpp -> OBSBasic::OBSInit()
void QtOBSContext::initialize(const QString &configPath,
                              const QString &windowTitle,
                              const QSize &screenSize,
                              const QRect &sourceRegion)
{
    blog(LOG_INFO, OBS_INIT_BEGIN);

    if (configPath.isEmpty() || windowTitle.isEmpty() ||
            screenSize.isEmpty() || sourceRegion.isEmpty()) {
        emit errorOccurred(Init, QStringLiteral("参数错误"));
        return;
    }

    // 参见 window-basic-main.cpp -> OBSBasic::InitBasicConfigDefaults

    // 计算最终需要输出的分辨率，本例以屏幕分辨率作为标准
    // 举例（参见 FindSceneItemAndScale 中的处理）：
    // 屏幕是 1920*1080，那么最终计算出的录制输出为 1280*720，同时 base 也设置为 1280*720，
    // 如果软件窗口界面大小为 480*360，则把它（窗口捕获源）同比例放大到 960*720，
    // 并设置它在场景中居中（左右各留黑160）
    int i = 0;
    uint32_t out_cx = screenSize.width();
    uint32_t out_cy = screenSize.height();
    while (((out_cx * out_cy) > (1280 * 720)) && scaled_vals[i] > 0.0) {
        double scale = scaled_vals[i++];
        out_cx = uint32_t(double(screenSize.width()) / scale);
        out_cy = uint32_t(double(screenSize.height()) / scale);
    }
    baseWidth    = out_cx;
    baseHeight   = out_cy;
    outputWidth  = out_cx; // 可自行修改
    outputHeight = out_cy;
    orgWidth     = sourceRegion.width();
    orgHeight    = sourceRegion.height();
    blog(LOG_INFO, "final resolution => org=%dx%d, base=%dx%d, output=%dx%d",
         orgWidth, orgHeight, baseWidth, baseHeight, outputWidth, outputHeight);

    // 初始化 OBS
    if (!obs_initialized()) {

        blog(LOG_INFO, "obs version %u", obs_get_version());

        if (!obs_startup("en-US", configPath.toStdString().c_str(), nullptr)) {
            blog(LOG_ERROR, "startup failed.");
            emit errorOccurred(Init, QStringLiteral("obs startup failed."));
            return;
        }

        // 加载模块，先设置模块加载路径
        QString appPath = QCoreApplication::applicationDirPath();
        QString pluginsPath = appPath + "/obs-plugins/32bit";
        QString modulePath = appPath + "/data/obs-plugins/%module%";
        obs_add_module_path(pluginsPath.toStdString().c_str(),
                            modulePath.toStdString().c_str());
        blog(LOG_INFO, OBS_SEPARATOR);
        obs_load_all_modules();
        blog(LOG_INFO, OBS_SEPARATOR);
        obs_log_loaded_modules();

        blog(LOG_INFO, OBS_STARTUP_SEPARATOR);
    }

    // 音频基本配置
    if (!resetAudio()) {
        blog(LOG_ERROR, "reset audio failed.");
        emit errorOccurred(Init, QStringLiteral("音频设置失败"));
        return;
    }

    // 视频基本配置
    int ret = resetVideo();
    if (ret != OBS_VIDEO_SUCCESS) {
        switch (ret) {
        case OBS_VIDEO_MODULE_NOT_FOUND:
            blog(LOG_ERROR, "failed to initialize video: graphics module not found.");
            break;
        case OBS_VIDEO_NOT_SUPPORTED:
            blog(LOG_ERROR, "unsupported.");
            break;
        case OBS_VIDEO_INVALID_PARAM:
            blog(LOG_ERROR, "failed to initialize video: invalid parameters.");
            break;
        default:
            blog(LOG_ERROR, "unknown.");
            break;
        }
        blog(LOG_ERROR, "reset video failed.");
        emit errorOccurred(Init, QStringLiteral("视频设置失败"));
        return;
    }

    // 设置音频检测设备（obs 软件，设置->高级->音频->音频监视设备）
    //#if defined(_WIN32)
    //    obs_set_audio_monitoring_device(TAG"-audio-monitor-default", "default");
    //    blog(LOG_INFO, "audio monitoring device:\tname: %s\tid: %s",
    //         "obs-audio-monitor-default", "default");
    //#endif

    // 初始化推流服务
    if (!initService()) {
        emit errorOccurred(Init, QStringLiteral("初始化服务失败"));
        return;
    }

    // 初始化输出相关(推流/本地录制/编码器)
    // 参见 window-basic-main.cpp -> OBSBasic::OBSInit()
    if (!resetOutputs()) {
        emit errorOccurred(Init, QStringLiteral("初始化编码器失败"));
        return;
    }

#ifdef _WIN32
    // 开启 Aero（Windows 8 及以上版本不起作用）
    // 方法 1
    if (1) {
        uint32_t winVer = GetWindowsVersion();
        blog(LOG_INFO, "windows version %x", winVer);
        if (winVer > 0 && winVer < 0x602) {
            blog(LOG_INFO, "set aero enable");
            SetAeroEnabled(true);
        }
    }
    // 方法 2
    else {
        // if (QSysInfo::windowsVersion() < QSysInfo::WV_WINDOWS8)
        QtWin::setCompositionEnabled(true);
    }
#endif

    // 以下流程
    // 参见 window-basic-main.cpp -> OBSBasic::Load -> OBSBasic::CreateDefaultScene

    // 参见 obs 软件 设置 -> 音频
    obs_set_output_source(SOURCE_CHANNEL_TRANSITION, nullptr);
    obs_set_output_source(SOURCE_CHANNEL_AUDIO_OUTPUT, nullptr);
    obs_set_output_source(SOURCE_CHANNEL_AUDIO_INPUT, nullptr);

    // 场景过度 - 淡出
    // 参见 window-basic-main-transitions.cpp -> OBSBasic::InitDefaultTransitions
    size_t idx = 0;
    const char *id;
    while (obs_enum_transition_types(idx++, &id)) {
        if (!obs_is_source_configurable(id)) {
            if (strcmp(id, "fade_transition") == 0) {
                const char *name = obs_source_get_display_name(id);
                obs_source_t *tr = obs_source_create_private(id, name, NULL);
                blog(LOG_INFO, "transition saved");
                fadeTransition = tr;
                break;
            }
        }
    }
    if (!fadeTransition) {
        blog(LOG_ERROR, "cannot find fade transition.");
        emit errorOccurred(Init, QStringLiteral("创建转换器失败"));
        return;
    }
    obs_set_output_source(SOURCE_CHANNEL_TRANSITION, fadeTransition);
    obs_source_release(fadeTransition);

    // 创建场景（scene 也是一种 source）
    scene = obs_scene_create(TAG "-Scene");
    if (!scene) {
        blog(LOG_ERROR, "create scene failed.");
        emit errorOccurred(Init, QStringLiteral("创建场景失败"));
        return;
    }
    obs_source_t *s = obs_get_output_source(SOURCE_CHANNEL_TRANSITION);
    obs_transition_set(s, obs_scene_get_source(scene));
    obs_source_release(s);

    if (HasAudioDevices(OUTPUT_AUDIO_SOURCE))
        ResetAudioDevice(OUTPUT_AUDIO_SOURCE, "default",
                         TAG " Default Desktop Audio",
                         SOURCE_CHANNEL_AUDIO_OUTPUT);
    if (HasAudioDevices(INPUT_AUDIO_SOURCE))
        ResetAudioDevice(INPUT_AUDIO_SOURCE, "default",
                         TAG " Default Mic/Aux",
                         SOURCE_CHANNEL_AUDIO_INPUT);
    // 设置降噪
    AddFilterToAudioInput("noise_suppress_filter");

    // 创建窗口捕获源，它是 scene 里唯一的一个 scene item
    captureSource = obs_source_create("window_capture", TAG "-WindowsCapture",
                                      NULL, nullptr);
    if (captureSource) {
        obs_scene_atomic_update(scene, AddSource, captureSource);
    } else {
        blog(LOG_ERROR, "create source failed.");
        emit errorOccurred(Init, QStringLiteral("创建源失败"));
        return;
    }

    // 添加窗口捕获源的剪裁过滤器，可以实现录制窗口特区域
    addFilterToSource(captureSource, VIDEO_CROP_FILTER_ID);
    videoCrop(sourceRegion);

    // 设置窗口捕获原的窗口
    obs_data_t *setting = obs_data_create();
    obs_data_t *curSetting = obs_source_get_settings(captureSource);
    obs_data_apply(setting, curSetting);
    obs_data_release(curSetting);

    blog(LOG_INFO, OBS_SEPARATOR);
    QFileInfo fi(QCoreApplication::applicationFilePath());
    QString desc = QString("[%1]: %2").arg(fi.fileName()).arg(windowTitle);
    blog(LOG_INFO, "exe desc %s", desc.toStdString().c_str());
    properties = obs_source_properties(captureSource);
    obs_property_t *property = obs_properties_first(properties);
    while (property) {
        const char *name = obs_property_name(property);
        if (strcmp(name, "window") == 0) {
            size_t count = obs_property_list_item_count(property);
            const char *string = nullptr;
            for (size_t i = 0; i < count; i++) {
                const char *name = obs_property_list_item_name(property, i);
                blog(LOG_INFO, "window list item name=%s", name);
                if (desc == QString::fromUtf8(name)) {
                    string = obs_property_list_item_string(property, i);
                    blog(LOG_INFO, "!!!Found item=%s", string);
                    break;
                }
            }
            if (string) {
                obs_data_set_string(setting, name, string);
                obs_source_update(captureSource, setting);
                break;
            } else {
                blog(LOG_INFO, "find application window failed.");
                obs_data_release(setting);
                emit errorOccurred(Init, QStringLiteral("查找应用窗口失败"));
                return;
            }
        }
        obs_property_next(&property);
    }
    obs_data_release(setting);
    blog(LOG_INFO, OBS_SEPARATOR);

    // 场景元素放缩
    obs_scene_enum_items(scene, FindSceneItemAndScale, (void *)this);

    blog(LOG_INFO, OBS_INIT_END);

    emit initialized();
}

void QtOBSContext::addFilterToSource(obs_source_t *source, const char *id)
{
    if (!id || *id == '\0')
        return;

    obs_source_t *existing_filter;
    std::string name = obs_source_get_display_name(id);
    existing_filter = obs_source_get_filter_by_name(source, name.c_str());
    if (existing_filter) {
        blog(LOG_WARNING, "filter %s exists.", id);
        obs_source_release(existing_filter);
        return;
    }

    name = std::string(TAG) + id;
    obs_source_t *filter = obs_source_create(id, name.c_str(), nullptr, nullptr);
    if (filter) {
        const char *sourceName = obs_source_get_name(source);
        blog(LOG_INFO, "add filter '%s' (%s) to source '%s'",
             name.c_str(), id, sourceName);
        obs_source_filter_add(source, filter);
        obs_source_release(filter);
    }
}

void QtOBSContext::videoCrop(const QRect &rect)
{
    if (!captureSource) return;

    if (rect.isEmpty()) {
        blog(LOG_WARNING, "source crop rect(%d %d %d %d) is empty.",
             rect.left(), rect.top(), rect.right(), rect.bottom());
        return;
    }

    scaleScene(rect.width(), rect.height());

    bool relative = false;
    std::string name = TAG VIDEO_CROP_FILTER_ID;
    obs_source_t *existing_filter =
            obs_source_get_filter_by_name(captureSource, name.c_str());
    if (existing_filter) {
        obs_data_t *settings = obs_source_get_settings(existing_filter);
        obs_data_set_bool(settings, "relative", relative);
        obs_data_set_int(settings, "left", rect.left());
        obs_data_set_int(settings, "top", rect.top());
        if (relative) {
            obs_data_set_int(settings, "right", rect.right());
            obs_data_set_int(settings, "bottom", rect.bottom());
            blog(LOG_INFO, "update source crop [Left:%d Top:%d Right:%d Bottom:%d]",
                 rect.left(), rect.top(), rect.right(), rect.bottom());
        } else {
            obs_data_set_int(settings, "cx", rect.width());
            obs_data_set_int(settings, "cy", rect.height());
            blog(LOG_INFO, "update source crop [Left:%d Top:%d Width:%d Height:%d]",
                 rect.left(), rect.top(), rect.width(), rect.height());
        }
        obs_source_update(existing_filter, settings);
        obs_data_release(settings);
        obs_source_release(existing_filter);
    }
}

bool QtOBSContext::initService()
{
    if (!rtmpService) {
        rtmpService = obs_service_create("rtmp_custom", TAG "-RtmpService",
                                         nullptr, nullptr);
        if (!rtmpService) {
            blog(LOG_ERROR, "create service failed.");
            return false;
        }
        obs_service_release(rtmpService);
    }
    return true;
}

/**
 * OBS Output 分为 SimpleOutput 和 AdvancedOutput
 * 我们使用 AdvancedOutput，可以进行更多的配置
 * 参见 window-basic-main-outputs.cpp -> AdvancedOutput::AdvancedOutput(...)
 */
bool QtOBSContext::resetOutputs()
{
    if (!streamOutput) {
        streamOutput = obs_output_create("rtmp_output",
                                         TAG "-AdvRtmpOutput",
                                         nullptr, nullptr);
        if (!streamOutput) {
            blog(LOG_ERROR, "create stream output failed.");
            return false;
        }
        obs_output_release(streamOutput);
    }

    if (!recordOutput) {
        recordOutput = obs_output_create("ffmpeg_output", TAG "-AdvFFmpegOutput",
                                         nullptr, nullptr);
        if (!recordOutput) {
            blog(LOG_ERROR, "create record output failed.");
            return false;
        }
        obs_output_release(recordOutput);
    }

    if (!h264Streaming) {
        OBSData streamEncSettings = getStreamEncSettings();
        h264Streaming = obs_video_encoder_create("obs_x264",
                                                 TAG "-StreamingH264",
                                                 streamEncSettings, nullptr);
        if (!h264Streaming) {
            blog(LOG_ERROR, "create streaming encoder fail");
            return false;
        }
        obs_encoder_release(h264Streaming);

        // 禁用放缩
        obs_encoder_set_scaled_size(h264Streaming, 0, 0);
        obs_encoder_set_video(h264Streaming, obs_get_video());
        obs_output_set_video_encoder(streamOutput, h264Streaming);
        obs_service_apply_encoder_settings(rtmpService, streamEncSettings, nullptr);
        obs_output_set_service(streamOutput, rtmpService);
    }

    for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
        std::string name = TAG "-AdvACCTrack";
        name += std::to_string(i + 1);
        obs_data_t *setting = obs_data_create();
        obs_data_set_int(setting, "bitrate", 128);
        if (!aacTrack[i]) {
            if (!CreateAACEncoder(aacTrack[i], aacEncoderID[i], name.c_str(), i,
                                  setting)) {
                blog(LOG_ERROR, "create audio encoder %d", i);
                obs_data_release(setting);
                return false;
            }
        }
        if (i == 0) {
            obs_service_apply_encoder_settings(rtmpService, nullptr, setting);
            obs_output_set_audio_encoder(streamOutput, aacTrack[0], 0);
        }
        obs_data_release(setting);

        obs_encoder_set_audio(aacTrack[i], obs_get_audio());
    }

    recordingStarted.Connect(obs_output_get_signal_handler(recordOutput),
                             "start", RecordingStarted, this);
    recordingStopping.Connect(obs_output_get_signal_handler(recordOutput),
                              "stopping", RecordingStopping, this);
    recordingStopped.Connect(obs_output_get_signal_handler(recordOutput),
                             "stop", RecordingStopped, this);
    streamingStarted.Connect(obs_output_get_signal_handler(streamOutput),
                             "start", StreamingStarted, this);
    streamingStopping.Connect(obs_output_get_signal_handler(streamOutput),
                              "stopping", StreamingStopping, this);
    streamingStopped.Connect(obs_output_get_signal_handler(streamOutput),
                             "stop", StreamingStopped, this);

    return true;
}

bool QtOBSContext::resetAudio()
{
    struct obs_audio_info oai;
    oai.samples_per_sec = 44100;
    oai.speakers        = SPEAKERS_STEREO;
    return obs_reset_audio(&oai);
}

int QtOBSContext::resetVideo()
{
    struct obs_video_info ovi;
    ovi.fps_num         = VIDEO_FPS; // 设置帧率，可自行调整
    ovi.fps_den         = 1;
    ovi.graphics_module = DL_D3D11;  // Win32 默认使用 Direct3D 11 参见 obs-app.cpp OBSApp::InitGlobalConfigDefaults()
    ovi.base_width      = this->baseWidth;
    ovi.base_height     = this->baseHeight;
    ovi.output_width    = this->outputWidth;
    ovi.output_height   = this->outputHeight;
    ovi.output_format   = VIDEO_FORMAT_I420;
    ovi.colorspace      = VIDEO_CS_601; // 参见 https://obsproject.com/forum/resources/obs-studio-color-space-color-format-color-range-settings-guide-test-charts.442/
    ovi.range           = VIDEO_RANGE_PARTIAL;
    ovi.adapter         = 0;         // 显示适配器索引，可自行调整
    ovi.gpu_conversion  = true;
    ovi.scale_type      = OBS_SCALE_BICUBIC;

    int ret = obs_reset_video(&ovi);

    if (IS_WIN32 && ret != OBS_VIDEO_SUCCESS) {
        if (strcmp(ovi.graphics_module, DL_OPENGL) != 0) {
            blog(LOG_WARNING, "failed to initialize obs video (%d) "
                              "with graphics_module='%s', retrying "
                              "with graphics_module='%s'",
                 ret, ovi.graphics_module, DL_OPENGL);
            ovi.graphics_module = DL_OPENGL;
            ret = obs_reset_video(&ovi);
        }
    }

    return ret;
}

void QtOBSContext::resetRecordFilePath(const QString &path)
{
    if (filePath) free(filePath);
    filePath = _strdup(path.toStdString().c_str());
    blog(LOG_INFO, "reset output file path %s", filePath);
}

void QtOBSContext::resetStreamLiveUrl(const QString &server,
                                      const QString &key)
{
    if (liveServer) free(liveServer);
    if (liveKey) free(liveKey);
    liveServer = _strdup(server.toStdString().c_str());
    liveKey = _strdup(key.toStdString().c_str());
    blog(LOG_INFO, "reset output rtmp url %s%s", liveServer, liveKey);
}

/**
 * @brief 设置音频输入、输出设备
 *        输出设备序号： 1， 2
 *        输入设备序号： 3， 4， 5
 */
void QtOBSContext::resetAudioInput(const QString &/*deviceId*/,
                                   const QString &deviceDesc)
{
    obs_properties_t *input_props =
            obs_get_source_properties(INPUT_AUDIO_SOURCE);
    bool find = false;
    const char *currentDeviceId = nullptr;

    obs_source_t *source =
            obs_get_output_source(SOURCE_CHANNEL_AUDIO_INPUT);
    if (source) {
        obs_data_t *settings = nullptr;
        settings = obs_source_get_settings(source);
        if (settings)
            currentDeviceId = obs_data_get_string(settings, "device_id");
        obs_data_release(settings);
        obs_source_release(source);
    }

    if (input_props) {
        obs_property_t *inputs = obs_properties_get(input_props, "device_id");
        size_t count = obs_property_list_item_count(inputs);
        for (size_t i = 0; i < count; i++) {
            const char *name = obs_property_list_item_name(inputs, i);
            const char *id = obs_property_list_item_string(inputs, i);
            if (QString(name).contains(deviceDesc)) {
                blog(LOG_INFO, "reset audio input use %s", name);
                ResetAudioDevice(INPUT_AUDIO_SOURCE, id, name,
                                 SOURCE_CHANNEL_AUDIO_INPUT);
                find = true;
                break;
            }
        }
        obs_properties_destroy(input_props);
    }

    if (!find) {
        if (QString(currentDeviceId) != "default") {
            blog(LOG_INFO, "reset audio input use \"default\"");
            ResetAudioDevice(INPUT_AUDIO_SOURCE, "default",
                             TAG " Default Mic/Aux",
                             SOURCE_CHANNEL_AUDIO_INPUT);
        }
    }

    AddFilterToAudioInput("noise_suppress_filter");
}

void QtOBSContext::resetAudioOutput(const QString &/*deviceId*/,
                                    const QString &deviceDesc)
{
    obs_properties_t *output_props =
            obs_get_source_properties(OUTPUT_AUDIO_SOURCE);
    bool find = false;
    const char *currentDeviceId = nullptr;

    obs_source_t *source =
            obs_get_output_source(SOURCE_CHANNEL_AUDIO_OUTPUT);
    if (source) {
        obs_data_t *settings = nullptr;
        settings = obs_source_get_settings(source);
        if (settings)
            currentDeviceId = obs_data_get_string(settings, "device_id");
        obs_data_release(settings);
        obs_source_release(source);
    }

    if (output_props) {
        obs_property_t *outputs = obs_properties_get(output_props, "device_id");
        size_t count = obs_property_list_item_count(outputs);
        for (size_t i = 0; i < count; i++) {
            const char *name = obs_property_list_item_name(outputs, i);
            const char *id = obs_property_list_item_string(outputs, i);
            if (QString(name).contains(deviceDesc)) {
                blog(LOG_INFO, "reset audio output use %s.", name);
                ResetAudioDevice(OUTPUT_AUDIO_SOURCE, id, name,
                                 SOURCE_CHANNEL_AUDIO_OUTPUT);
                find = true;
                break;
            }
        }
        obs_properties_destroy(output_props);
    }

    if (!find) {
        if (QString(currentDeviceId) != "default") {
            blog(LOG_INFO, "reset audio output use \"default\".");
            ResetAudioDevice(OUTPUT_AUDIO_SOURCE, "default",
                             TAG " Default Desktop Audio",
                             SOURCE_CHANNEL_AUDIO_OUTPUT);
        }
    }
}

static void AudioDownmixMono(obs_source_t *source, bool enable)
{
    if (!source) return;

    uint32_t flags = obs_source_get_flags(source);
    bool forceMonoActive = (flags & OBS_SOURCE_FLAG_FORCE_MONO) != 0;
    blog(LOG_INFO, "audio force mono %d", forceMonoActive);
    if (forceMonoActive != enable) {
        if (enable)
            flags |= OBS_SOURCE_FLAG_FORCE_MONO;
        else
            flags &= ~OBS_SOURCE_FLAG_FORCE_MONO;
        obs_source_set_flags(source, flags);
    }
}

void QtOBSContext::downmixMonoInput(bool enable)
{
    obs_source_t *source = obs_get_output_source(SOURCE_CHANNEL_AUDIO_INPUT);
    if (source) {
        AudioDownmixMono(source, enable);
        obs_source_release(source);
    }
}

void QtOBSContext::downmixMonoOutput(bool enable)
{
    obs_source_t *source = obs_get_output_source(SOURCE_CHANNEL_AUDIO_OUTPUT);
    if (source) {
        AudioDownmixMono(source, enable);
        obs_source_release(source);
    }
}

void QtOBSContext::muteAudioInput(bool mute)
{
    obs_source_t *source =
            obs_get_output_source(SOURCE_CHANNEL_AUDIO_INPUT);
    if (source) {
        bool old = obs_source_muted(source);
        //        if (old != mute) {
        blog(LOG_INFO, "mute audio input from:%d to:%d", old, mute);
        obs_source_set_muted(source, mute);
        //        }
        obs_source_release(source);
    }
}

void QtOBSContext::muteAudioOutput(bool mute)
{
    obs_source_t *source =
            obs_get_output_source(SOURCE_CHANNEL_AUDIO_OUTPUT);
    if (source) {
        bool old = obs_source_muted(source);
        //        if (old != mute) {
        blog(LOG_INFO, "mute audio output from:%d to:%d.", old, mute);
        obs_source_set_muted(source, mute);
        //        }
        obs_source_release(source);
    }
}

OBSData QtOBSContext::getStreamEncSettings()
{
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "preset", "medium");
    obs_data_set_string(settings, "tune", "stillimage");
    obs_data_set_string(settings, "x264opts", "");
    obs_data_set_bool(settings, "vfr", false);
    obs_data_set_string(settings, "rate_control", "CRF");
    obs_data_set_int(settings, "crf", 22);            // 23 标准值，值越小码率越大，文件越大
    obs_data_set_string(settings, "profile", "main");
    obs_data_set_int(settings, "keyint_sec", 10);

    OBSData dataRet(settings);
    obs_data_release(settings);
    return dataRet;
}

bool QtOBSContext::setupRecord()
{
    obs_data_t *settings = obs_data_create();

    obs_data_set_string(settings, "url", filePath);
    obs_data_set_string(settings, "format_name", RECORD_OUTPUT_FORMAT);
    obs_data_set_string(settings, "format_mime_type", RECORD_OUTPUT_FORMAT_MIME);
    obs_data_set_string(settings, "muxer_settings", "movflags=faststart"); // moov 前置
    obs_data_set_int(settings, "gop_size", VIDEO_FPS * 10);
    obs_data_set_string(settings, "video_encoder", VIDEO_ENCODER_NAME);
    obs_data_set_int(settings, "video_encoder_id", VIDEO_ENCODER_ID);
    if (VIDEO_ENCODER_ID == AV_CODEC_ID_H264)
        obs_data_set_string(settings, "video_settings", "profile=main x264-params=crf=22");
    else if (VIDEO_ENCODER_ID == AV_CODEC_ID_FLV1)
        obs_data_set_int(settings, "video_bitrate", VIDEO_BITRATE);
    obs_data_set_int(settings, "audio_bitrate", AUDIO_BITRATE);
    obs_data_set_string(settings, "audio_encoder", "aac");
    obs_data_set_int(settings, "audio_encoder_id", AV_CODEC_ID_AAC);
    obs_data_set_string(settings, "audio_settings", NULL);

    obs_data_set_int(settings, "scale_width", outputWidth);
    obs_data_set_int(settings, "scale_height", outputHeight);

    //obs_output_set_mixer(fileOutput, 0);
    obs_output_set_media(recordOutput, obs_get_video(), obs_get_audio());
    obs_output_update(recordOutput, settings);

    obs_data_release(settings);

    return true;
}

bool QtOBSContext::setupStream()
{
    OBSData settings = obs_data_create();
    obs_data_release(settings);

    obs_data_set_string(settings, "server", liveServer);
    obs_data_set_string(settings, "key", liveKey);
    obs_data_set_bool(settings, "use_auth", false);
    obs_service_update(rtmpService, settings);

    obs_output_set_reconnect_settings(streamOutput, 0, 0);

    return true;
}

void QtOBSContext::startRecord(const QString &output)
{
    if (output.isEmpty()) {
        blog(LOG_ERROR, "record parameter invalid, outputPath=%s.",
             output.toStdString().c_str());
        emit errorOccurred(Record, QStringLiteral("参数错误"));
        return;
    }

    if (QString(filePath).compare(output) != 0) {
        if (filePath) free(filePath);
        filePath = _strdup(output.toStdString().c_str());
        blog(LOG_INFO, "record output file path %s", filePath);
    }

    setupRecord();

    if (!obs_output_start(recordOutput)) {
        blog(LOG_ERROR, "record start fail");
        emit errorOccurred(Record, QStringLiteral("启动失败"));
        return;
    }
}

void QtOBSContext::stopRecord(bool force)
{
    if (obs_output_active(recordOutput)) {
        if (force) {
            obs_output_force_stop(recordOutput);
        } else {
            obs_output_stop(recordOutput);
        }
    }
}

void QtOBSContext::startStream(const QString &server, const QString &key)
{
    if (server.isEmpty() || key.isEmpty()) {
        blog(LOG_ERROR, "stream parameter invalid, server=%s, key=%s",
             server.toStdString().c_str(), key.toStdString().c_str());
        emit errorOccurred(Stream, QStringLiteral("参数错误"));
        return;
    }

    if (QString(liveServer).compare(server) != 0 ||
            QString(liveKey).compare(key) != 0) {
        if (liveServer) free(liveServer);
        if (liveKey) free(liveKey);
        liveServer = _strdup(server.toStdString().c_str());
        liveKey = _strdup(key.toStdString().c_str());
        blog(LOG_INFO, "stream url server:%s key:%s", liveServer, liveKey);
    }

    setupStream();

    if (!obs_output_start(streamOutput)) {
        blog(LOG_ERROR, "stream start fail");
        emit errorOccurred(Stream, QStringLiteral("启动失败"));
        return;
    }

    if (recordWhenStreaming)
        startRecord(QString(filePath));

    firstTotal = obs_output_get_total_frames(streamOutput);
    firstDropped = obs_output_get_frames_dropped(streamOutput);
    blog(LOG_INFO, "first total:%d, dropped:%d", firstTotal, firstDropped);
}

void QtOBSContext::stopStream(bool force)
{
    if (obs_output_active(streamOutput)) {
        if (force) {
            obs_output_force_stop(streamOutput);
        } else {
            obs_output_stop(streamOutput);
        }
    }

    if (recordWhenStreaming)
        stopRecord(force);
}

void QtOBSContext::updateVideoSettings(bool cursor, bool compatibility,
                                       bool useWildcards)
{
    if (!captureSource) return;

    blog(LOG_INFO, "update source cursor=%d, compatibility=%d, "
                   "use_wildcards=%d", cursor, compatibility, useWildcards);
    obs_data_t *settings = obs_source_get_settings(captureSource);
    obs_data_set_bool(settings, "cursor", cursor);
    obs_data_set_bool(settings, "compatibility", compatibility);
    obs_data_set_bool(settings, "use_wildcards", useWildcards);
    obs_source_update(captureSource, settings);
    obs_data_release(settings);
}

void QtOBSContext::scaleScene(int w, int h)
{
    if (orgWidth == w && orgHeight == h)
        return;

    orgWidth = w;
    orgHeight = h;
    if (scene) {
        obs_scene_enum_items(scene, FindSceneItemAndScale, (void *)this);
        blog(LOG_INFO, "scale scene to %dx%d.", w, h);
    }
}

void QtOBSContext::logStreamStats()
{
    if (!streamOutput) return;

    uint64_t totalBytes = obs_output_get_total_bytes(streamOutput);
    uint64_t curTime = os_gettime_ns();
    uint64_t bytesSent = totalBytes;

    if (bytesSent < lastBytesSent)
        bytesSent = 0;
    if (bytesSent == 0)
        lastBytesSent = 0;

    uint64_t bitsBetween = (bytesSent - lastBytesSent) * 8;
    long double timePassed = (long double)(curTime - lastBytesSentTime) /
                             1000000000.0l;
    long double kbps = (long double)bitsBetween /
                       timePassed / 1000.0l;

    if (timePassed < 0.01l)
        kbps = 0.0l;

    long double num = 0;
    int total = obs_output_get_total_frames(streamOutput);
    int dropped = obs_output_get_frames_dropped(streamOutput);
    if (total < firstTotal || dropped < firstDropped) {
        firstTotal   = 0;
        firstDropped = 0;
    }
    total   -= firstTotal;
    dropped -= firstDropped;
    num = total ? (long double)dropped / (long double)total * 100.0l : 0.0l;

    blog(LOG_INFO, "obs stream stat, bitrate:%.2lf kb/s, frames:%d / %d (%.2lf%%)",
         kbps, dropped, total, num);

    lastBytesSent     = bytesSent;
    lastBytesSentTime = curTime;
}
