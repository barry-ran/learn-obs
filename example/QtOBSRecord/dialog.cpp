#include "dialog.h"
#include "ui_dialog.h"

#include "obs-wrapper.h"

#include <QResizeEvent>
#include <QStandardPaths>
#include <QMessageBox>
#include <QDateTime>
#include <QScreen>
#include <QDir>
#include <QDebug>

Dialog::Dialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Dialog)
{
    ui->setupUi(this);

    this->setWindowTitle("QtOBSRecordDialog");

    for (int i = 0; i < 100; ++i)
        ui->listWidget->addItem(QString("Item -> ") + QString::number(i+1));

    setupOBS();
}

Dialog::~Dialog()
{
    stopOBSRecord();

    if (obsThread->isRunning()) {
        obsThread->quit();
        obsThread->wait(3 * 1000);
    }
    delete obsContext;

    delete ui;
}

void Dialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);

    onOBSScaleScene();
}

void Dialog::on_pushButtonStartRecord_clicked()
{
    if (isOBSInitialized)
        startOBSRecord();
    else {
        QString dataDirPath =
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir dataDir(dataDirPath);
        if (!dataDir.exists())
            dataDir.mkpath(dataDirPath);
        QSize screenSize = QApplication::primaryScreen()->geometry().size();
        qreal ratio = QApplication::primaryScreen()->devicePixelRatio();
        emit obsInit(dataDirPath, this->windowTitle(), screenSize * ratio,
                     QRect(QPoint(0, 0), this->size() * ratio));
    }
}

void Dialog::on_pushButtonStopRecord_clicked()
{
    if (isOBSRecording)
        stopOBSRecord();
}

void Dialog::setupOBS()
{
    isOBSRecording = false;
    isOBSInitialized = false;
    obsCrop = true;

    obsThread = new QThread(this);
    obsContext = new QtOBSContext;
    obsContext->moveToThread(obsThread);

    connect(obsContext, &QtOBSContext::initialized,
            this,       &Dialog::onOBSInitialized);
    connect(obsContext, &QtOBSContext::recordStarted,
            this,       &Dialog::onOBSRecordStarted);
    connect(obsContext, &QtOBSContext::recordStopped,
            this,       &Dialog::onOBSRecordStopped);
    connect(obsContext, &QtOBSContext::errorOccurred,
            this,       &Dialog::onOBSErrorOccurred);

    connect(this,       &Dialog::obsInit,
            obsContext, &QtOBSContext::initialize);
    connect(this,       &Dialog::obsScaleScene,
            obsContext, &QtOBSContext::scaleScene);
    connect(this,       &Dialog::obsVideoCrop,
            obsContext, &QtOBSContext::videoCrop);
    connect(this,       &Dialog::obsStartRecord,
            obsContext, &QtOBSContext::startRecord);
    connect(this,       &Dialog::obsStopRecord,
            obsContext, &QtOBSContext::stopRecord);

    obsThread->start();
}

void Dialog::startOBSRecord()
{
    isOBSRecording = true;

    if (obsCrop)
        onOBSScaleScene();

    QString moviesDirPath =
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QString filePath = QString("%1/QtOBS-%2.%3").arg(moviesDirPath)
                       .arg(QDateTime::currentDateTime().
                            toString("yyyy-MM-dd-hh-mm-ss"))
                       .arg(OUTPUT_FLV ? "flv" : "mp4");
    emit obsStartRecord(filePath);
}

void Dialog::stopOBSRecord()
{
    emit obsStopRecord(true);
}

void Dialog::onOBSInitialized()
{
    isOBSRecording = false;
    isOBSInitialized = true;

    startOBSRecord();
}

void Dialog::onOBSRecordStarted()
{
    isOBSRecording = true;
    ui->pushButtonStartRecord->setEnabled(false);
}

void Dialog::onOBSRecordStopped()
{
    isOBSRecording = false;
    ui->pushButtonStartRecord->setEnabled(true);
}

void Dialog::onOBSErrorOccurred(const int type, const QString &err)
{
    QString title;
    QString msg = err;
    switch (type) {
    case QtOBSContext::Init:
        title = "初始化";
        break;
    case QtOBSContext::Record:
        title = "录制";
        isOBSRecording = false;
        break;
    default:
        return;
    }

    QMessageBox::warning(this, title, err);
}

void Dialog::onOBSScaleScene()
{
    qreal ratio = QApplication::primaryScreen()->devicePixelRatio();

    if (obsCrop) {
        QRect recordRect = ui->groupBox->geometry();
        emit obsVideoCrop(QRect(recordRect.topLeft() * ratio,
                                recordRect.size() * ratio));
    } else {
        if (isOBSInitialized)
            emit obsScaleScene(this->width() * ratio, this->height() * ratio);
    }
}
