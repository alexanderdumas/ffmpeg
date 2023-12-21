#include "mpp.h"

RKMppCodec::RKMppCodec() : _width(0),_height(0),_hor_stride(0),
            _ver_stride(0),_rc_mode(MPP_ENC_RC_MODE_FIXQP),
            _frame_num(0),
            _format(MPP_FMT_BGR888),_type(MPP_VIDEO_CodingMJPEG),
            _need_split(0),_easy_type(MPP_CODEC_SIMPLE),
            _header_size(0),_frame_size(0),
            _mdinfo_size(0),
            _enc_cfg(NULL),_dec_cfg(NULL),
            _ctx(NULL),_mpi(NULL),_buf_grp(NULL),_frm_buf(NULL),
            _pkt_buf(NULL),_mdi_buf(NULL),_packet(NULL),_frame(NULL),
            _base(NULL),_task(NULL)

{
    
}

RKMppCodec::~RKMppCodec() 
{
    if (_ctx) {
        mpp_destroy(_ctx);
        _ctx = NULL;
    }

    if (_frm_buf) {
        mpp_buffer_put(_frm_buf);
        _frm_buf = NULL;
    }

    if (_pkt_buf) {
        mpp_buffer_put(_pkt_buf);
        _pkt_buf = NULL;
    }

    if (_mdi_buf) {
        mpp_buffer_put(_mdi_buf);
        _mdi_buf = NULL;
    }

    if (_buf_grp) {
        mpp_buffer_group_put(_buf_grp);
        _buf_grp = NULL;
    }

    if (_packet) {
        mpp_packet_deinit(&_packet);
        _packet = NULL;
    }

    if (_frame) {
        mpp_frame_deinit(&_frame);
        _frame = NULL;
    }

    if (_base) {
        free(_base);
    }
}

RK_S32 RKMppCodec::EncDefaultStride(RK_U32 width, MppFrameFormat fmt) {
    RK_S32 stride = 0;
    switch (fmt & MPP_FRAME_FMT_MASK) {
        case MPP_FMT_YUV420SP : {
            stride = MPP_ALIGN(width, 8);
        } break;
        case MPP_FMT_RGB888 :
        case MPP_FMT_BGR888 : {
            stride = MPP_ALIGN(width, 8) * 3;
        } break;
        default : {
            printf("do not support type %d\n", fmt);
        } break;
    }

    return stride;
}

size_t RKMppCodec::GetFrameSize(MppFrameFormat format, RK_U32 hor_stride, RK_U32 ver_stride) {
    size_t frame_size = 0;
    switch (format & MPP_FRAME_FMT_MASK) {
        case MPP_FMT_YUV420SP:{
            frame_size = MPP_ALIGN(hor_stride, 64) * MPP_ALIGN(ver_stride, 64) * 3 / 2;
        } break;
        case MPP_FMT_RGB888 :
        case MPP_FMT_BGR888 : {
            frame_size = MPP_ALIGN(hor_stride, 64) * MPP_ALIGN(ver_stride, 64);
        } break;
        default: {
            frame_size = MPP_ALIGN(hor_stride, 64) * MPP_ALIGN(ver_stride, 64) * 4;
        } break;
    }
    return frame_size;
}

size_t RKMppCodec::GetMediaSize(MppCodingType code_type, RK_U32 hor_stride, RK_U32 ver_stride) {
    size_t mdinfo_size = 0;
    mdinfo_size  = (MPP_VIDEO_CodingHEVC == code_type) ?
                      (MPP_ALIGN(hor_stride, 32) >> 5) *
                      (MPP_ALIGN(ver_stride, 32) >> 5) * 16 :
                      (MPP_ALIGN(hor_stride, 64) >> 6) *
                      (MPP_ALIGN(ver_stride, 16) >> 4) * 16;
    return mdinfo_size;
}

size_t RKMppCodec::GetHeadSize(MppFrameFormat format, RK_U32 width, RK_U32 height) {
    size_t header_size = 0;
    if (MPP_FRAME_FMT_IS_FBC(format)) {
        if ((format & MPP_FRAME_FBC_MASK) == MPP_FRAME_FBC_AFBC_V1) {
            header_size = MPP_ALIGN(MPP_ALIGN(width, 16) * MPP_ALIGN(height, 16) / 16, SZ_4K);
        }
        else {
            header_size = MPP_ALIGN(width, 16) * MPP_ALIGN(height, 16) / 16;
        }
    } else {
        header_size = 0;
    }
    return header_size;
}

MPP_RET RKMppCodec::EncSetCfg() {

    mpp_enc_cfg_set_s32(_enc_cfg, "prep:width", _width);
    mpp_enc_cfg_set_s32(_enc_cfg, "prep:height", _height);
    mpp_enc_cfg_set_s32(_enc_cfg, "prep:hor_stride", _hor_stride);
    mpp_enc_cfg_set_s32(_enc_cfg, "prep:ver_stride", _ver_stride);
    mpp_enc_cfg_set_s32(_enc_cfg, "prep:format", _format);
    mpp_enc_cfg_set_s32(_enc_cfg, "rc:mode", _rc_mode);

    /* setup qp for different codec and rc_mode */
    switch (_type) {
        case MPP_VIDEO_CodingMJPEG : {
            /* jpeg use special codec config to control qtable */
            mpp_enc_cfg_set_s32(_enc_cfg, "jpeg:q_factor", 95);
            mpp_enc_cfg_set_s32(_enc_cfg, "jpeg:qf_max", 99);
            mpp_enc_cfg_set_s32(_enc_cfg, "jpeg:qf_min", 1);
        } break;
        default : {
        } break;
    }

     /* setup codec  */
    mpp_enc_cfg_set_s32(_enc_cfg, "codec:type", _type);

    MPP_RET ret = MPP_OK;
    ret = _mpi->control(_ctx, MPP_ENC_SET_CFG, _enc_cfg);
    if (ret) {
        printf("mpi control enc set cfg failed ret %d\n", ret);
        return ret;
    }

    return ret;
}

void RKMppCodec::Init (MppCodecType codec_type, RK_U32 width, RK_U32 height,
            MppFrameFormat format,  MppCodingType code_type,
            RK_S32 need_split, MppEasyType easy_type ) {
    MPP_RET ret = MPP_OK;
    if (codec_type == MPP_CODEC_ENC) {
        _width = width;
        _height = height;
        _type = code_type;
        _format = format;
        _need_split = need_split;
        _hor_stride = EncDefaultStride(_width,_format);
        _ver_stride = MPP_ALIGN(_height,16);

        _type == MPP_VIDEO_CodingMJPEG;
        _rc_mode = MPP_ENC_RC_MODE_FIXQP;
        _frame_num = 1;

        _frame_size = GetFrameSize(_format, _hor_stride, _ver_stride);
        _mdinfo_size = GetMediaSize(_type, _hor_stride, _ver_stride);
        _header_size = GetHeadSize(_format, _width, _width);

        ret = mpp_buffer_group_get_internal(&_buf_grp, MPP_BUFFER_TYPE_DRM);
        if (ret) {
            printf("failed to get mpp buffer group ret %d\n", ret);
            return;
        }

        ret = mpp_buffer_get(_buf_grp, &_frm_buf, _frame_size + _header_size);
        if (ret) {
            printf("failed to get buffer for input frame ret %d\n", ret);
            return;
        }

        ret = mpp_buffer_get(_buf_grp, &_pkt_buf, _frame_size);
        if (ret) {
            printf("failed to get buffer for output packet ret %d\n", ret);
            return;
        }

        ret = mpp_buffer_get(_buf_grp, &_mdi_buf, _mdinfo_size);
        if (ret) {
            printf("failed to get buffer for motion info output packet ret %d\n", ret);
            return;
        }

        ret = mpp_create(&_ctx, &_mpi);
        if (ret) {
            printf("mpp_create failed ret %d\n", ret);
            return;
        }

        MppPollType timeout = MPP_POLL_BLOCK;
        ret = _mpi->control(_ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
        if (MPP_OK != ret) {
            printf("mpi control set output timeout %d ret %d\n", timeout, ret);
            return;
        }

        ret = mpp_init(_ctx, MPP_CTX_ENC, _type);
        if (ret) {
            printf("mpp_init failed ret %d\n", ret);
            return;
        }

        ret = mpp_enc_cfg_init(&_enc_cfg);
        if (ret) {
            printf("mpp_enc_cfg_init failed ret %d\n", ret);
            return;
        }

        ret = _mpi->control(_ctx, MPP_ENC_GET_CFG, _enc_cfg);
        if (ret) {
            printf("get enc cfg failed ret %d\n", ret);
            return;
        }

        ret = EncSetCfg();
        if (ret) {
            printf("set enc cfg failed ret %d\n", ret);
            return;
        }
    }
    else if (codec_type == MPP_CODEC_DEC) {
        _width = width;
        _height = height;
        _type = code_type;
        _format = format;
        _need_split = need_split;
        _hor_stride = MPP_ALIGN(_width,16);
        _ver_stride = MPP_ALIGN(_height,16);

        ret = mpp_buffer_group_get_internal(&_buf_grp, MPP_BUFFER_TYPE_DRM);
        if (ret) {
            printf("failed to get buffer group for input frame ret %d\n", ret);
            return;
        }

        ret = mpp_frame_init(&_frame);
        if (ret) {
            printf("mpp_frame_init failed %d\n", ret);
            return;
        }
        
        ret = mpp_buffer_get(_buf_grp, &_frm_buf, _hor_stride * _ver_stride * 4);
        if (ret) {
            printf("mpp_buffer_get failed %d\n", ret);
            return;
        }
        
        ret = mpp_buffer_get(_buf_grp, &_pkt_buf, _hor_stride * _ver_stride );
        if (ret) {
            printf("failed to get buffer for output packet ret %d\n", ret);
            return;
        }

        mpp_frame_set_buffer(_frame, _frm_buf);

        ret = mpp_create(&_ctx, &_mpi);
        if (ret) {
            printf("mpp_create failed\n");
            return;
        }

        ret = mpp_init(_ctx, MPP_CTX_DEC, _type);
        if (ret) {
            printf("%p mpp_init failed\n", _ctx);
            return;
        }

        ret = mpp_dec_cfg_init(&_dec_cfg);
        if (ret != MPP_OK) {
            printf("%p failed to mpp_dec_cfg_init ret %d\n", _ctx, ret);
            return;
        }

        ret = _mpi->control(_ctx, MPP_DEC_GET_CFG, _dec_cfg);
        if (ret) {
            printf("%p failed to get decoder cfg ret %d\n", _ctx, ret);
            return;
        }

        ret = mpp_dec_cfg_set_u32(_dec_cfg, "base:split_parse", _need_split);
        if (ret) {
            printf("%p failed to set split_parse ret %d\n", _ctx, ret);
            return;
        }

        ret = _mpi->control(_ctx, MPP_DEC_SET_CFG, _dec_cfg);
        if (ret) {
            printf("%p failed to set cfg %p ret %d\n", _ctx, _dec_cfg, ret);
            return;
        }

        ret = _mpi->control(_ctx, MPP_DEC_SET_OUTPUT_FORMAT, &_format);
        if (ret != MPP_OK) {
            printf("failed to control MPP_DEC_SET_OUTPUT_FORMAT\n");
            return;
        }
    }
    else {
        printf("codec_type error=%d\n", codec_type);
    }
}

MPP_RET RKMppCodec::EncReadImg(void *buf, void *input, size_t len, RK_U32 width, RK_U32 height,
               RK_U32 hor_stride, RK_U32 ver_stride, MppFrameFormat format) {
    MPP_RET ret = MPP_OK;
    RK_U32 read_size;
    RK_U32 row = 0;
    RK_U8 *buf_y = (RK_U8 *)buf;
    RK_U8 *buf_u = buf_y + hor_stride * ver_stride; // NOTE: diff from gen_yuv_image
    RK_U8 *buf_v = buf_u + hor_stride * ver_stride / 4; // NOTE: diff from gen_yuv_image

    switch (format & MPP_FRAME_FMT_MASK) {
        case MPP_FMT_YUV420SP : {
            memcpy(buf_y, input, (width * height * 3 / 2));
        } break;
        case MPP_FMT_RGB888 :
        case MPP_FMT_BGR888 : {
            memcpy(buf_y , input, (width * height * 3));
        } break;
        default : {
            printf("read image do not support fmt %d\n", format);
            ret = MPP_ERR_VALUE;
            return ret;
        } break;
    }
    return ret;
}

MPP_RET RKMppCodec::Encode(void *input, size_t len, std::vector<unsigned char> & output) {
    MPP_RET ret = MPP_OK;
    MppMeta meta = NULL;
    _frame = NULL;
    _packet = NULL;
    void *buf = mpp_buffer_get_ptr(_frm_buf);
    ret = EncReadImg(buf,input,len,_width,_height,_hor_stride,_ver_stride,_format);
    if (ret) {
        printf("encode read image fail[%d]\n",ret);
        return ret;
    }

    ret = mpp_frame_init(&_frame);
    if (ret) {
        printf("frame init fail[%d]\n",ret);
        return ret;
    }

    mpp_frame_set_width(_frame, _width);
    mpp_frame_set_height(_frame, _height);
    mpp_frame_set_hor_stride(_frame, _hor_stride);
    mpp_frame_set_ver_stride(_frame, _ver_stride);
    mpp_frame_set_fmt(_frame, _format);
    mpp_frame_set_eos(_frame, 1);
    mpp_frame_set_buffer(_frame, _frm_buf);

    meta = mpp_frame_get_meta(_frame);
    mpp_packet_init_with_buffer(&_packet, _pkt_buf);
    mpp_packet_set_length(_packet, 0);
    mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, _packet);
    mpp_meta_set_buffer(meta, KEY_MOTION_INFO, _mdi_buf);

    ret = _mpi->encode_put_frame(_ctx, _frame); //输入frame
    if (ret) {
        printf("frame put fail[%d]\n",ret);
        mpp_frame_deinit(&_frame);
        return ret;
    }

    mpp_frame_deinit(&_frame);
    ret = _mpi->encode_get_packet(_ctx, &_packet); //输出packet
    if (ret) {
        printf("packet get fail[%d]\n",ret);
        return ret;
    }
    if (_packet) {
        void *ptr   = mpp_packet_get_pos(_packet);
        size_t len  = mpp_packet_get_length(_packet);
        printf("len=%d\n",len);
        output.resize(len);
        memcpy(&output[0], ptr, len);
    }
    return ret;
}

MPP_RET RKMppCodec::DecReadImg(void *buf,void *input,size_t len) {
    MPP_RET ret = MPP_OK;
    memcpy(buf, input, len);
    return ret;
}
MPP_RET RKMppCodec::DumpToFrame(MppFrame frame,std::vector<unsigned char> & output) {
    MPP_RET ret = MPP_OK;
    RK_U32 width    = 0;
    RK_U32 height   = 0;
    RK_U32 h_stride = 0;
    RK_U32 v_stride = 0;
    MppFrameFormat fmt  = MPP_FMT_BUTT;
    MppBuffer buffer    = NULL;
    RK_U8 *base = NULL;

    if (NULL == frame) {
        return MPP_NOK;
    }

    width    = mpp_frame_get_width(frame);
    height   = mpp_frame_get_height(frame);
    h_stride = mpp_frame_get_hor_stride(frame);
    v_stride = mpp_frame_get_ver_stride(frame);
    fmt      = mpp_frame_get_fmt(frame);
    buffer   = mpp_frame_get_buffer(frame);
    
    if (NULL == buffer) {
        printf("buffer is NULL\n");
        return MPP_NOK;
    }

    base = (RK_U8 *)mpp_buffer_get_ptr(buffer);
    switch (fmt ) {
        case MPP_FMT_YUV420SP : {
            RK_U32 i;
            RK_U8 *base_y = base;
            RK_U8 *base_c = base + h_stride * v_stride;
            if (output.size() != height * width * 3 /2) {
                output.resize(height * width * 3 /2);
            }
            memcpy(&output[0] , base_y, h_stride * v_stride);
            RK_U32 start = h_stride * v_stride;
            memcpy(&output[0] + start , base_c, h_stride * v_stride / 2);
        } break;
        case MPP_FMT_RGB888: {
            RK_U32 i;
            RK_U8 *base_y = base;
            if (output.size() != height * width * 3) {
                output.resize(height * width * 3);
            }
            memcpy(&output[0], base_y, height * width * 3);
        }break;
        default : {
            printf("not supported format %d\n", fmt);
        } break;
    }

    return ret;
}

MPP_RET RKMppCodec::Decode(void *input, size_t len, std::vector<unsigned char> & output) {
    MPP_RET ret = MPP_OK;
    RK_U32 pkt_done = 0;
    RK_U32 frm_eos = 0;
    
    void *buf = mpp_buffer_get_ptr(_pkt_buf);
    DecReadImg(buf,input,len);
    mpp_packet_init_with_buffer(&_packet, _pkt_buf);
    mpp_packet_set_eos(_packet);
    mpp_packet_set_length(_packet, len);

    ret = _mpi->poll(_ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    if (ret) {
        printf("%p mpp input poll failed\n", _ctx);
        return ret;
    }

    ret = _mpi->dequeue(_ctx, MPP_PORT_INPUT, &_task);  /* input queue */
    if (ret) {
        printf("%p mpp task input dequeue failed\n", _ctx);
        return ret;
    }

    ret = mpp_task_meta_set_packet(_task, KEY_INPUT_PACKET, _packet);
    if (ret) {
        printf("%p mpp mpp_task_meta_set_packet failed\n", _ctx);
        return ret;
    }

    ret = mpp_task_meta_set_frame(_task, KEY_OUTPUT_FRAME, _frame);
    if (ret) {
        printf("%p mpp mpp_task_meta_set_frame failed\n", _ctx);
        return ret;
    }

    ret = _mpi->enqueue(_ctx, MPP_PORT_INPUT, _task);  /* input queue */
    if (ret) {
        printf("%p mpp task input enqueue failed\n", _ctx);
        return ret;
    }

    ret = _mpi->poll(_ctx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    if (ret) {
        printf("%p mpp output poll failed\n", _ctx);
        return ret;
    }

    ret = _mpi->dequeue(_ctx, MPP_PORT_OUTPUT, &_task); /* output queue */
    if (ret) {
        printf("%p mpp task output dequeue failed\n", _ctx);
        return ret;
    }

    if (_task) {
        if (_frame) {
            RK_U32 width = mpp_frame_get_width(_frame);
            RK_U32 height = mpp_frame_get_height(_frame);
            RK_U32 hor_stride = mpp_frame_get_hor_stride(_frame);
            RK_U32 ver_stride = mpp_frame_get_ver_stride(_frame);
            RK_U32 buf_size = mpp_frame_get_buf_size(_frame);
            RK_U32 err_info = mpp_frame_get_errinfo(_frame);
            RK_U32 discard = mpp_frame_get_discard(_frame);
            MppFrameFormat fmt = mpp_frame_get_fmt(_frame);
                        
            _frm_buf = mpp_frame_get_buffer(_frame);
            if (_frm_buf == NULL) {
                printf("%p mpp_frame_get_buffer null\n", _ctx);
                return ret;
            }
            
            frm_eos = mpp_frame_get_eos(_frame);
            if (frm_eos) {
                printf("%p found eos frame\n", _ctx);
            } else {
                printf("%p not found eos frame\n", _ctx);
            }
            DumpToFrame(_frame,output);
        }
        
        ret = _mpi->enqueue(_ctx, MPP_PORT_OUTPUT, _task);
        if (ret) {
            printf("%p mpp task output enqueue failed\n", _ctx);
            return ret;
        }
    }

    mpp_packet_deinit(&_packet);

    ret = ReSet();
    if (ret != MPP_OK) {
        printf("ReSet fail ret[%d]\n", ret);
        return ret;
    }

    return ret;
}

MPP_RET RKMppCodec::ReSet() {
    MPP_RET ret = MPP_OK;
    ret = _mpi->reset(_ctx);
    if (ret != MPP_OK) {
        printf("failed to exec mApi->reset.\n");
    }
    return ret;
}

