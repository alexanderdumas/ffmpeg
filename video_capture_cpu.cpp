#include "video_capture_cpu.h"
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>

using namespace std;

FFVideoCPU::FFVideoCPU()
{
}

FFVideoCPU::~FFVideoCPU()
{
    avformat_network_deinit();
}

int32_t FFVideoCPU::AVInterruptCallBackFun(void* param)
{
    FFVideoCPU* pFFmpeg = static_cast<FFVideoCPU*>(param);
    if (pFFmpeg->m_QuitBlock) {
        return -1;
    }

    return 0;
}

int32_t FFVideoCPU::Init()
{
    av_register_all();
    //avdevice_register_all();
    avformat_network_init();
    return 0;
}

// snprintf(url, 255, "rtsp://%s:%s@%s/h264/ch1/main/av_stream", user, pwd, ip);
int32_t FFVideoCPU::Open(const char* url, StreamOut stream_out, void* userData)
{
    int32_t ret = 0;
    m_Url = url;
    m_StreamOut = stream_out;
    m_VideoInfo.user_data = userData;

    AVDictionary* opts = nullptr;
    if (m_RtBufSize > 0)
        av_dict_set_int(&opts, "rtbufsize", m_RtBufSize, 0);

    if (0 == strncmp(m_Url.c_str(), "rtsp://", 7)) {
        if (m_IsUseUdp) {
            av_dict_set(&opts, "rtsp_transport", "udp", 0);
        } else {
            av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        }

        av_dict_set(&opts, "stimeout", std::to_string(m_StreamTimeOut).c_str(), 0);
    }

    if (m_MaxDelay > 0) {
        av_dict_set_int(&opts, "max_delay", m_MaxDelay, 0);
    }

    this->m_FmtCtx = avformat_alloc_context();
    this->m_FmtCtx->interrupt_callback.callback = AVInterruptCallBackFun;
    this->m_FmtCtx->interrupt_callback.opaque = this;
    this->m_QuitBlock = false;

    ret = avformat_open_input(&m_FmtCtx, m_Url.c_str(), nullptr, &opts);
    if (ret < 0) {
        char errmsg[1024] = { 0 };
        av_strerror(ret, errmsg, 1023);
        return -1;
    }

    ret = avformat_find_stream_info(m_FmtCtx, nullptr);
    if (ret < 0) {
        char errmsg[1024] = { 0 };
        av_strerror(ret, errmsg, 1023);
        return -1;
    }

    int index = av_find_best_stream(this->m_FmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, 0, 0);
    if (index < 0) {
        char errmsg[1024] = { 0 };
        av_strerror(ret, errmsg, 1023);
        return -1;
    } else {
        this->m_VideoStreamIdx = ret;
        this->m_VideoStream = this->m_FmtCtx->streams[this->m_VideoStreamIdx];
    }

    char buf[256];
    avcodec_string(buf, sizeof(buf), this->m_VideoStream->codec, 0);
    if (nullptr != strstr(buf, "mp4v")) {
        return -1;
    }
    
    switch (this->m_VideoStream->codec->codec_id) {
    case AV_CODEC_ID_H264:
        this->m_Avbsfc = av_bitstream_filter_init("h264_mp4toannexb");
        break;
    case AV_CODEC_ID_HEVC:
        this->m_Avbsfc = av_bitstream_filter_init("hevc_mp4toannexb");
        break;
    default:
        break;
    }

    m_VideoStream = m_FmtCtx->streams[m_VideoStreamIdx];
    this->m_VideoDecCtx = this->m_VideoStream->codec;

    switch (this->m_VideoDecCtx->pix_fmt) {
    case -1:
        break;
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        break;
    default:
        break;
    }

    av_dump_format(this->m_FmtCtx, 0, m_Url.c_str(), 0);

    m_VideoDec = avcodec_find_decoder(m_VideoDecCtx->codec_id);
    if (nullptr == m_VideoDec) {
        return -1;
    }
    
    av_opt_set(m_VideoDecCtx->priv_data, "tune", "zerolatency", 0);

    ret = avcodec_open2(m_VideoDecCtx, m_VideoDec, nullptr);
    if (ret < 0) {
        return -1;
    }

    if (nullptr != opts) {
        av_dict_free(&opts);
    }

    if (m_YUVData == nullptr) {
        m_YUVData = new uint8_t[m_VideoDecCtx->width * m_VideoDecCtx->height * 3 / 2];
    }
    if (m_RGBData == nullptr) {
        m_RGBData = new uint8_t[m_VideoDecCtx->width * m_VideoDecCtx->height * 3];
    }

    m_SCxt = sws_getContext(m_VideoDecCtx->width, m_VideoDecCtx->height, AV_PIX_FMT_YUV420P,
        m_VideoDecCtx->width, m_VideoDecCtx->height, AV_PIX_FMT_RGB24, SWS_POINT, nullptr, nullptr, nullptr);
    if (m_SCxt == nullptr) {
        return -1;
    }

    m_VideoInfo.src_width = m_VideoDecCtx->width;
    m_VideoInfo.src_height = m_VideoDecCtx->height;
    m_VideoInfo.handle = this;
    ret = avpicture_alloc(&m_Picture, AV_PIX_FMT_RGB24, m_VideoDecCtx->width, m_VideoDecCtx->height);
    if (ret != 0) {
        return -1;
    }

    this->m_IsRunning = true;
    m_PlayThread = new thread(&FFVideoCPU::ReadVideoData, this);
    return 0;
}

int32_t FFVideoCPU::ReadVideoData()
{
    uint64_t ntpts = 0;
    double rtspTime = 0;

    if (m_pFrameYUV == nullptr) {
        m_pFrameYUV = av_frame_alloc();
    }

    av_init_packet(&this->m_FramePkt);
    this->m_FramePkt.data = nullptr;
    this->m_FramePkt.size = 0;

    while (this->m_IsRunning) {
        int32_t ret = av_read_frame(this->m_FmtCtx, &this->m_FramePkt);
        if (ret == AVERROR(EAGAIN)) {
            av_usleep(1000);
            continue;
        }

        if (ret < 0) {
            av_free_packet(&this->m_FramePkt);
            char errmsg[1024] = { 0 };
            av_strerror(ret, errmsg, 1023);
            break;
        }

        if (this->m_VideoStreamIdx != this->m_FramePkt.stream_index) {
            av_free_packet(&this->m_FramePkt);
            continue;
        }

        int get_picture = 0;
        ret = avcodec_decode_video2(m_VideoDecCtx, m_pFrameYUV, &get_picture, &m_FramePkt);
        if (ret < 0) {
            char errmsg[1024] = { 0 };
            av_strerror(ret, errmsg, 1023);
            continue;
        }

        if (get_picture) {
            if (m_StreamOut) {
                m_StreamOut();
            }
        }

        av_free_packet(&this->m_FramePkt);
    }

    av_free_packet(&this->m_FramePkt);
    if (m_pFrameYUV) {
        av_frame_free(&m_pFrameYUV);
    }
    this->m_IsRunning = false;
    return 0;
}

int32_t FFVideoCPU::Stop()
{
    m_IsRunning = false;
    if (m_PlayThread && m_PlayThread->joinable()) {
        m_PlayThread->join();
        delete m_PlayThread;
        m_PlayThread = nullptr;
    }
    m_QuitBlock = true;
    if (m_FmtCtx) {
        avformat_close_input(&m_FmtCtx);
        m_FmtCtx = nullptr;
    }
    if (m_VideoDecCtx) {
        avcodec_close(m_VideoDecCtx);
        m_VideoDecCtx = nullptr;
    }
    if (m_SCxt) {
        sws_freeContext(m_SCxt);
        m_SCxt = nullptr;
    }

    avpicture_free(&m_Picture);

    if (m_YUVData) {
        delete[] m_YUVData;
        m_YUVData = nullptr;
    }
    if (m_RGBData) {
        delete[] m_RGBData;
        m_RGBData = nullptr;
    }

    return 0;
}

int32_t FFVideoCPU::ReOpen()
{

    return 0;
}