#include <fstream>
#include <iostream>

#include "ffmpeg.h"


using namespace std;

FFMPEG::FFMPEG()
{
}

FFMPEG::~FFMPEG()
{
    avformat_network_deinit();
}

int32_t FFMPEG::AVStreamCallBack(void* param)
{
    FFMPEG* pFFmpeg = static_cast<FFMPEG*>(param);
    if (pFFmpeg->m_Quit) {
        return -1;
    }

    return 0;
}

int32_t FFMPEG::Initialization()
{
    av_register_all();
    avformat_network_init();
    return 0;
}

//rtsp格式参考
//rtsp://user:pwd@ip/h264/ch1/main/av_stream
int32_t FFMPEG::OpenStream(const char* url, StreamOut stream_out)
{
    int32_t ret = 0;
    m_Url = url;
    m_StreamOut = stream_out;

    //设置参数，根据具体业务
    AVDictionary* opts = nullptr;
    av_dict_set_int(&opts, "rtbufsize", 1382400, 0);
    av_dict_set(&opts, "rtsp_transport", "udp", 0);
    //av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    //...
    
    //上下文信息
    this->m_FmtCtx = avformat_alloc_context();
    this->m_FmtCtx->interrupt_callback.callback = AVStreamCallBack;
    this->m_FmtCtx->interrupt_callback.opaque = this;
    this->m_Quit = false;

    //打开设备
    ret = avformat_open_input(&m_FmtCtx, m_Url.c_str(), nullptr, &opts);
    if (ret < 0) {
        return -1;
    }

    //查找流信息
    ret = avformat_find_stream_info(m_FmtCtx, nullptr);
    if (ret < 0) {
        return -1;
    }

    int index = av_find_best_stream(this->m_FmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, 0, 0);
    if (index < 0) {
        return -1;
    } else {
        this->m_VideoStreamIdx = ret;
        this->m_VideoStream = this->m_FmtCtx->streams[this->m_VideoStreamIdx];
    }

    //视频流
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

    /*
    //GPU 用名称检索
    char decoder_name[128] = {0};
    switch (this->m_VideoStream->codec->codec_id) {
    case AV_CODEC_ID_H264:
        decoder_name = "h264_nvmpi";
        this->m_Avbsfc = av_bitstream_filter_init("h264_mp4toannexb");
        break;
    case AV_CODEC_ID_HEVC:
        decoder_name = "hevc_nvmpi";
        this->m_Avbsfc = av_bitstream_filter_init("hevc_mp4toannexb");
        break;
    default:
        decoder_name = "h264_nvmpi"; // default h264
        break;
    }
    */

    m_VideoStream = m_FmtCtx->streams[m_VideoStreamIdx];
    this->m_VideoDecCtx = this->m_VideoStream->codec;

    switch (this->m_VideoDecCtx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        break;
    default:
        break;
    }

    av_dump_format(this->m_FmtCtx, 0, m_Url.c_str(), 0);

    m_VideoDec = avcodec_find_decoder(m_VideoDecCtx->codec_id);
    if (nullptr == m_VideoDec) {
        return -1;
    }
    /*
    //GPU 查找解码器
    m_VideoDec = avcodec_find_decoder_by_name(decoder_name);
    if (nullptr == m_VideoDec) {
        return -1;
    }
    */
    
    //打开解码器
    ret = avcodec_open2(m_VideoDecCtx, m_VideoDec, nullptr);
    if (ret < 0) {
        return -1;
    }

    m_SCxt = sws_getContext(m_VideoDecCtx->width, m_VideoDecCtx->height, AV_PIX_FMT_YUV420P,
        m_VideoDecCtx->width, m_VideoDecCtx->height, AV_PIX_FMT_RGB24, SWS_POINT, nullptr, nullptr, nullptr);
    if (m_SCxt == nullptr) {
        return -1;
    }

    m_FFInfo.width = m_VideoDecCtx->width;
    m_FFInfo.height = m_VideoDecCtx->height;
    m_FFInfo.handle = this;
    ret = avpicture_alloc(&m_Picture, AV_PIX_FMT_RGB24, m_VideoDecCtx->width, m_VideoDecCtx->height);
    if (ret != 0) {
        return -1;
    }

    //启动线程抓取视频流
    this->m_Running = true;
    m_Thread = new thread(&FFVideoCPU::CaptureData, this);
    return 0;
}

int32_t FFMPEG::CaptureData()
{
    av_init_packet(&this->m_Packet);

    while (this->m_Running) {
        int32_t ret = av_read_frame(this->m_FmtCtx, &this->m_Packet);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        }

        if (ret < 0) {
            av_free_packet(&this->m_Packet);
            break;

        if (this->m_VideoStreamIdx != this->m_Packet.stream_index) {
            av_free_packet(&this->m_Packet);
            continue;
        }

        int is_succ = 0;
        ret = avcodec_decode_video2(m_VideoDecCtx, m_Frame, &is_succ, &m_Packet);
        if (ret < 0) {
            continue;
        }

        if (is_succ) {
            //从流中拷贝数据memcpy...
            //执行回调
            if (m_StreamOut) {
                m_StreamOut();
            }
        }

        av_free_packet(&this->m_Packet);
    }

    av_free_packet(&this->m_Packet);
    if (m_Frame) {
        av_frame_free(&m_Frame);
    }
    this->m_Running = false;
    return 0;
}

int32_t FFMPEG::Release()
{
    m_Running = false;
    if (m_Thread->joinable()) {
        m_Thread->join();
    }
    m_Quit = true;
    //ffmpeg释放
    if (m_FmtCtx) {
        avformat_close_input(&m_FmtCtx);
        m_FmtCtx = nullptr;
    }
    //....

    return 0;
}