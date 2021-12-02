#include <QDebug>
#include "widget.h"
#include "./ui_widget.h"
#include "libobs/obs.h"

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    // 构造函数ui还没准备好，这里创建source会失败
    //obs_source_t* source = obs_source_create("display_capture", "mac-capture", nullptr, nullptr);
}

Widget::~Widget()
{
    on_stopBtn_clicked();
    delete ui;
}

void output_stopped(void *data, calldata_t *cd) {
    qDebug() << "output_stopped:" << (int)calldata_int(cd, "code");
}

void Widget::on_startBtn_clicked()
{
    m_scene = obs_scene_create("mainscene");
    if (!m_scene) {
        qDebug() << "obs_scene_create failed";
        return;
    }
    // 注意m_scene是OBSScene类型，OBSScene用cpp对象管理obs_scene_t*生命周期
    // 给m_scene赋值对addref一次，所以这里需要release一次
    // 其他obs对象一样
    obs_scene_release(m_scene);

    // display_capture：源id，插件源码定义obs_source_info时会指定id
    // name随意，保证唯一即可
    // video source
    m_video_source = obs_source_create("display_capture", "myvideosource", nullptr, nullptr);
    if (!m_video_source) {
        qDebug() << "obs_source_create failed";
        return;
    }
    obs_source_release(m_video_source);

    // audio source
    // todo 不采集音频时，音频源是否必须？（目前看来是必须的）
    obs_data_t* audio_settings = obs_data_create();
    obs_data_set_string(audio_settings, "device_id", "default");
    m_audio_source = obs_source_create("coreaudio_input_capture", "myaudiosource", audio_settings, nullptr);
    obs_data_release(audio_settings);
    if (!m_audio_source) {
        qDebug() << "obs_source_create failed";
        return;
    }
    obs_source_release(m_audio_source);

    // mac下使用mac-vth264(VideoToolbox)来硬件编码性能更好
    m_video_encoder = obs_video_encoder_create("obs_x264", "myvideoencoder", nullptr, nullptr);
    if (!m_video_encoder) {
        qDebug() << "obs_video_encoder_create failed";
        return;
    }
    obs_encoder_release(m_video_encoder);

    m_audio_encoder = obs_audio_encoder_create("ffmpeg_aac", "myaudioencoder", nullptr, 0, nullptr);
    if (!m_audio_encoder) {
        qDebug() << "obs_audio_encoder_create failed";
        return;
    }
    obs_encoder_release(m_audio_encoder);

    m_output = obs_output_create("ffmpeg_muxer", "myoutput", nullptr, nullptr);
    if (!m_output) {
        qDebug() << "obs_output_create failed";
        return;
    }
    obs_output_release(m_output);

    // set
    obs_data_t* settings = obs_data_create();
    obs_data_set_string(settings, "path", "/Users/bytedance/test.mp4");
    obs_data_set_string(settings, "muxer_settings", nullptr);
    obs_output_update(m_output, settings);
    obs_data_release(settings);

    obs_data_t* VideoSetting = obs_data_create();
    obs_data_set_string(VideoSetting, "rate_control", "CRF");
    obs_data_set_int(VideoSetting, "crf", 24);
    obs_data_set_int(VideoSetting, "bitrate", 2500);
    obs_data_set_string(VideoSetting, "preset", "veryfast");
    obs_data_set_string(VideoSetting, "profile", "high");
    obs_encoder_update(m_video_encoder, VideoSetting);
    obs_data_release(VideoSetting);

    // 关联
    obs_set_output_source(0, obs_scene_get_source(m_scene));
    obs_set_output_source(1, m_audio_source);
    obs_sceneitem_set_visible(obs_scene_add(m_scene, m_video_source), true);
    obs_encoder_set_video(m_video_encoder, obs_get_video());
    obs_output_set_video_encoder(m_output, m_video_encoder);
    obs_encoder_set_audio(m_audio_encoder, obs_get_audio());
    obs_output_set_audio_encoder(m_output, m_audio_encoder, 0);

    signal_handler_t *handler = obs_output_get_signal_handler(m_output);
    signal_handler_connect(handler, "stop", output_stopped, this);

    // 启动
    if (!obs_output_start(m_output)) {
        qDebug() << "obs_output_start failed:" << obs_output_get_last_error(m_output);
    }
}


void Widget::on_stopBtn_clicked()
{
    if (!m_output) {
        return;
    }
    obs_output_stop(m_output);
    m_scene = nullptr;
    m_video_source = nullptr;
    m_audio_source = nullptr;
    m_video_encoder = nullptr;
    m_audio_encoder = nullptr;
    m_output = nullptr;
}

