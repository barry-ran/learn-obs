#include <QApplication>
#include <QDebug>
#include <QDir>
#include "widget.h"
#include "libobs/obs.h"
#include "util.h"

int main(int argc, char *argv[])
{
    // 此时调用太早了，是false
    qDebug() << "is bundle " << Util::is_in_bundle();

    QApplication a(argc, argv);

    // 这里才能拿到bundleIdentifier
    qDebug() << "is bundle " << Util::is_in_bundle();

    // 初始化obs
    auto retb = obs_startup("zh-CN", nullptr, nullptr);
    if (!retb) {
        qDebug() << "obs_startup failed";
        return 0;
    }

    // 加载模块
    qDebug() << "applicationDirPath:" << QApplication::applicationDirPath();
    QString obs_plugins = QApplication::applicationDirPath() + "/../obs-plugins/%module%";
    QString data_plugins = QApplication::applicationDirPath() + "/../Resources/data/obs-plugins/%module%";
    obs_add_module_path(obs_plugins.toStdString().c_str(), data_plugins.toStdString().c_str());
    obs_load_all_modules();
    obs_post_load_modules();

    // 设置video参数
    struct obs_video_info ovi;
    ovi.adapter = 0;
    ovi.fps_num = 30;
    ovi.fps_den = 1;
    ovi.graphics_module = "libobs-opengl";
    ovi.output_format = VIDEO_FORMAT_NV12;
    ovi.scale_type = OBS_SCALE_DISABLE;
    ovi.colorspace = VIDEO_CS_709;
    ovi.range = VIDEO_RANGE_FULL;
    ovi.gpu_conversion = true;
    ovi.base_width = 1280;
    ovi.base_height = 720;
    ovi.output_width = 1280;
    ovi.output_height = 720;
    auto reti = obs_reset_video(&ovi);
    if (OBS_VIDEO_SUCCESS != reti) {
        qDebug() << "obs_reset_video failed:" << reti;
        obs_shutdown();
        return 0;
    }

    // 设置audio参数
    struct obs_audio_info ai;
    ai.samples_per_sec = 44100;
    ai.speakers = SPEAKERS_MONO;
    retb = obs_reset_audio(&ai);
    if (!retb) {
        qDebug() << "obs_reset_audio failed";
        return 0;
    }

    Widget* w = new Widget;
    w->show();
    reti = a.exec();
    delete w;

    // 清理obs
    obs_shutdown();

    int active_obs_num = bnum_allocs();
    qDebug() << "*******active_obs_num:" << active_obs_num;
    if (active_obs_num > 0) {
        qDebug() << "*******memory leaks:" << active_obs_num;
    }

    return reti;
}
