#ifndef MAINWIND_H
#define MAINWIND_H

#include <QMainWindow>
#include "ijkmediaplayer.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWind; }
QT_END_NAMESPACE

class MainWind : public QMainWindow
{
    Q_OBJECT

public:
    MainWind(QWidget *parent = nullptr);
    ~MainWind();

    int InitSignalsAndSlots();

    int message_loop(void *arg);
    int OutputVideo(const Frame *frame);
    void OnPlayOrPause();
    void OnStop();

private:
    Ui::MainWind *ui;
    IjkMediaPlayer *mp_=NULL;
};
#endif // MAINWIND_H
