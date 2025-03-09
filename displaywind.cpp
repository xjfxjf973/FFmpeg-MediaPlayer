#include "displaywind.h"
#include "ui_displaywind.h"
#include <SDL.h>
#include <SDL_syswm.h>

DisplayWind::DisplayWind(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DisplayWind)
{
    ui->setupUi(this);
    
    // 初始化SDL
    if(SDL_Init(SDL_INIT_VIDEO) < 0) {
        return;
    }

    // 创建SDL窗口
    sdl_window_ = SDL_CreateWindowFrom((void*)this->winId());
    if(!sdl_window_) {
        return;
    }

    // 创建SDL渲染器
    sdl_renderer_ = SDL_CreateRenderer(sdl_window_, -1, SDL_RENDERER_ACCELERATED);
    if(!sdl_renderer_) {
        SDL_DestroyWindow(sdl_window_);
        sdl_window_ = nullptr;
        return;
    }
}

DisplayWind::~DisplayWind()
{
    // 释放SDL资源
    if(sdl_texture_) {
        SDL_DestroyTexture(sdl_texture_);
        sdl_texture_ = nullptr;
    }
    if(sdl_renderer_) {
        SDL_DestroyRenderer(sdl_renderer_);
        sdl_renderer_ = nullptr;
    }
    if(sdl_window_) {
        SDL_DestroyWindow(sdl_window_);
        sdl_window_ = nullptr;
    }
    SDL_Quit();
    
    delete ui;
}

int DisplayWind::Draw(const Frame *frame)
{
    if(!frame || !frame->frame || !sdl_renderer_) {
        return -1;
    }

    // 创建或更新SDL纹理
    if(!sdl_texture_ || frame->width != video_width || frame->height != video_height) {
        if(sdl_texture_) {
            SDL_DestroyTexture(sdl_texture_);
            sdl_texture_ = nullptr;
        }
        
        sdl_texture_ = SDL_CreateTexture(
            sdl_renderer_,
            SDL_PIXELFORMAT_IYUV,
            SDL_TEXTUREACCESS_STREAMING,
            frame->width,
            frame->height
        );
        
        if(!sdl_texture_) {
            return -1;
        }
        
        video_width = frame->width;
        video_height = frame->height;
    }

    // 更新纹理数据
    SDL_UpdateYUVTexture(
        sdl_texture_,
        nullptr,
        frame->frame->data[0], frame->frame->linesize[0],
        frame->frame->data[1], frame->frame->linesize[1],
        frame->frame->data[2], frame->frame->linesize[2]
    );

    // 清除并渲染
    //使用特定颜色清空当前渲染目标
    SDL_RenderClear(sdl_renderer_);
    //使用部分图像数据(texture)更新当前渲染目标
    SDL_RenderCopy(sdl_renderer_, sdl_texture_, nullptr, nullptr);
    //执行渲染，更新屏幕显示
    SDL_RenderPresent(sdl_renderer_);
    //SDL_Delay(20);
    return 0;
}

void DisplayWind::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
}

void DisplayWind::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    
    // 窗口大小变化时调整渲染
    if(sdl_renderer_) {
        SDL_RenderClear(sdl_renderer_);
        if(sdl_texture_) {
            SDL_RenderCopy(sdl_renderer_, sdl_texture_, nullptr, nullptr);
        }
        SDL_RenderPresent(sdl_renderer_);
    }
}

void DisplayWind::UpdateFrame()
{
    update();
}
