#ifndef DISPLAYWIND_H
#define DISPLAYWIND_H

#include <QWidget>
#include <QMutex>
#include "ijkmediaplayer.h"
#include "imagescaler.h"


namespace Ui {
class DisplayWind;
}

class DisplayWind : public QWidget
{
    Q_OBJECT

public:
    explicit DisplayWind(QWidget *parent = nullptr);
    ~DisplayWind();

    int Draw(const Frame *frame);

protected:
    //这里不要重载event事件，会导致paintEvent不被触发
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event);

private slots:
    void UpdateFrame();

private:
    Ui::DisplayWind *ui;

    int m_nLastFrameWidth; //< 记录视频宽高
    int m_nLastFrameHeight ;
    bool is_display_size_change_ = false;

    int x_=0;    //起始位置
    int y_=0;
    int video_width = 0;
    int video_height = 0;
    int img_width = 0;
    int img_height =0;
    QImage img;
    //VideoFrame dst_video_frame_ ;
    QMutex m_mutex;
    ImageScaler *ing_scaler_ = NULL;

    // SDL渲染相关
    SDL_Window *sdl_window_ = nullptr;
    SDL_Renderer *sdl_renderer_ = nullptr;
    SDL_Texture *sdl_texture_ = nullptr;


};

#endif // DISPLAYWIND_H
