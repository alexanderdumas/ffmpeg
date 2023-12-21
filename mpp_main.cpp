#include "
int main()
{
    AVFormatContext *pFormatCtx = NULL;
    AVDictionary *options = NULL;
    AVPacket *av_packet = NULL;
    char filepath[] = "rtsp://user:passwd@10.x.x.x:230/h264/ch01/sub/av_stream";// rtsp 地址

    av_register_all();  //该函数在ffmpeg4.0以上版本已经被废弃，4.0以下版本需要
    avformat_network_init();
    av_dict_set(&options, "buffer_size", "1024000", 0); //设置缓存大小
    av_dict_set(&options, "rtsp_transport", "tcp", 0); //以tcp的方式打开,
    av_dict_set(&options, "stimeout", "6000000", 0); //设置超时断开链接时间，单位us
    av_dict_set(&options, "max_delay", "600000", 0); //设置最大时延

    pFormatCtx = avformat_alloc_context(); //用来申请AVFormatContext类型变量并初始化默认参数,申请的空间


    //打开网络流或文件流
    if (avformat_open_input(&pFormatCtx, filepath, NULL, &options) != 0)
    {
        printf("Couldn't open input stream.\n");
        return 0;
    }

    //获取视频文件信息
    if (avformat_find_stream_info(pFormatCtx, NULL)<0)
    {
        printf("Couldn't find stream information.\n");
        return 0;
    }

    //查找码流中是否有视频流
    int videoindex = -1;
    unsigned i = 0;
    for (i = 0; i<pFormatCtx->nb_streams; i++)
    if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        videoindex = i;
        break;
    }
    if (videoindex == -1)
    {
        printf("Didn't find a video stream.\n");
        return 0;
    }

    av_packet = (AVPacket *)av_malloc(sizeof(AVPacket)); // 申请空间，存放的每一帧数据 （h264、h265）


    //初始化MPP
    //Init decoder
    MppCodec decoder;
    decoder.Init(MPP_CODEC_DEC, 1280, 720, MPP_FMT_YUV420SP, MPP_VIDEO_CodingMJPEG);
    std::vector<uchar> out_buff;

    //循环拉取数据
    while (1)
    {
        if (av_read_frame(pFormatCtx, av_packet) >= 0)
        {
            if (av_packet->stream_index == videoindex)
            {
                decoder.Decode(av_packet->dat, av_packet->size, out_buff); 
            }
            if (av_packet != NULL) {
                av_packet_unref(av_packet);
            }
        }
    }    

    av_free(av_packet);
    avformat_close_input(&pFormatCtx);

    return 0;
}
