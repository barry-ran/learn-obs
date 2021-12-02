#ifndef DIALOG_H
#define DIALOG_H

#include <QDialog>
#include <QThread>

namespace Ui {
    class Dialog;
}

class QtOBSContext;

class Dialog : public QDialog
{
    Q_OBJECT

private:
    Ui::Dialog *ui;

    QThread    *obsThread;

    QtOBSContext *obsContext;
    bool       isOBSRecording;
    bool       isOBSInitialized;
    bool       obsCrop;

public:
    explicit Dialog(QWidget *parent = 0);
    ~Dialog();

signals:
    void obsInit(const QString &configPath, const QString &windowTitle,
                 const QSize &screenSize, const QRect &sourceRect);
    void obsScaleScene(int w, int h);
    void obsVideoCrop(const QRect &);
    void obsStartRecord(const QString &output);
    void obsStopRecord(bool force);

protected:
    void resizeEvent(QResizeEvent *);

private slots:
    void on_pushButtonStartRecord_clicked();
    void on_pushButtonStopRecord_clicked();

    void onOBSInitialized();
    void onOBSRecordStarted();
    void onOBSRecordStopped();
    void onOBSErrorOccurred(const int, const QString &);
    void onOBSScaleScene();

    void setupOBS();
    void startOBSRecord();
    void stopOBSRecord();
};

#endif // DIALOG_H
