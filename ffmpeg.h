#ifndef _FFMEPG_H_
#define _FFMEPG_H_

#include <cstdint>
#include <string>
#include <thread>

// ffmpeg，编译ffmepg 头文件
extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
};

//自定义信息流
typedef struct _FfmpegInfo {
    int32_t width;
    int32_t height;
    void *handle;
    //其他根据需要补充
} FFmpegInfo;

//回调函数，拉取到完整一帧后做相应得处理，只给伪代码
typedef void (*StreamOut)();

class FFMPEG {
public:
    FFMPEG();
    virtual ~FFMPEG();

    static int32_t Initialization();
    int32_t Release();
    int32_t OpenStream(const char* url, StreamOut stream_out);
    int32_t CaptureData();
    static int32_t AVStreamCallBack(void *param);
protected:
    StreamOut m_StreamOut;
    std::string m_Url;
    bool m_Quit = false;
    bool m_Running = false;

    FFmpegInfo m_FFInfo;
    std::thread* m_Thread = nullptr;

    int32_t m_VideoStreamIdx = -1;
    AVPacket m_Packet;
    AVPicture m_Picture;

    AVStream *m_VideoStream = nullptr;
    AVCodecContext *m_VideoDecCtx = nullptr;
    AVCodec* m_VideoDec = nullptr;
    AVBitStreamFilterContext* m_Avbsfc = nullptr;
    struct SwsContext * m_SCxt = nullptr;
    AVFrame *m_Frame = nullptr;
    AVFormatContext *m_FmtCtx = nullptr;
};

#endif