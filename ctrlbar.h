#ifndef CTRLBAR_H
#define CTRLBAR_H

#include <QWidget>

namespace Ui {
class CtrlBar;
}

class CtrlBar : public QWidget
{
    Q_OBJECT

public:
    explicit CtrlBar(QWidget *parent = nullptr);
    ~CtrlBar();

signals:
    void SigPlayOrPause();
    void SigStop();

private slots:
    void on_playOrpauseBtn_clicked();

    void on_stopBtn_clicked();

private:
    Ui::CtrlBar *ui;
};

#endif // CTRLBAR_H
