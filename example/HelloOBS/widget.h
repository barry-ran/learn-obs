#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include "libobs/obs.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void on_startBtn_clicked();

    void on_stopBtn_clicked();

private:
    Ui::Widget *ui;
    // 注意OBSScene和obs_scene_t*的区别，前者用cpp对象管理obs_scene_t*生命周期
    // 其他obs对象一样
    OBSScene m_scene = nullptr;
    OBSSource m_video_source = nullptr;
    OBSSource m_audio_source = nullptr;
    OBSEncoder m_video_encoder = nullptr;
    OBSEncoder m_audio_encoder = nullptr;
    OBSOutput m_output = nullptr;
};
#endif // WIDGET_H
