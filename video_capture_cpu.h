#ifndef _VIDEO_CAPTURE_H
#define _VIDEO_CAPTURE_H

#include <cstdint>
#include <string>
#include <thread>

// ffmpeg
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

//get stream info
typedef struct _VideoInfo {
    int32_t src_width;
    int32_t src_height;
    std::string codec_string;
    AVPixelFormat pix_fmt;
    //等等
} VideoInfo;


typedef void (*StreamOut)(uint8_t *data, int32_t datasize, VideoInfo *pStreamInfo);

class FFVideoCPU {
public:
    FFVideoCPU();
    virtual ~FFVideoCPU();

    static int32_t Init();
    static int32_t AVInterruptCallBackFun(void *param);

    int32_t ReadVideoData();

    int32_t Open(const char* url, StreamOut stream_out, void* userData);

    int32_t  ReOpen();

    int32_t Stop();

    inline bool IsRunning(){
        return m_IsRunning;
    }

protected:
    StreamOut m_StreamOut;

    //ffmpeg
    std::string m_Url;
    void* user_data_ = nullptr;
    int32_t m_RtBufSize = 0;
    int32_t m_StreamTimeOut = 5000000; // 单位微妙
    int32_t m_MaxDelay = 0;
    bool m_QuitBlock = false;
    bool m_IsUseUdp = false;
    bool m_IsRunning = false;

    int32_t m_VideoStreamIdx = -1;
    AVFormatContext *m_FmtCtx = nullptr;
    AVStream *m_VideoStream = nullptr;
    AVCodecContext *m_VideoDecCtx = nullptr;
    AVCodec* m_VideoDec = nullptr;
    AVBitStreamFilterContext* m_Avbsfc = nullptr;
    struct SwsContext * m_SCxt = nullptr;
    AVPacket m_FramePkt;
    AVFrame *m_pFrameYUV = nullptr;
    AVPicture m_Picture;
    uint8_t* m_YUVData = nullptr;
    uint8_t* m_RGBData = nullptr;
    std::thread* m_PlayThread = nullptr;
    VideoInfo m_VideoInfo;
    int32_t m_FrameIndex = 0;
};

#endif //_VIDEO_CAPTURE_H