#ifndef _RKMPP_H_
#define _RKMPP_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <vector>

/*
rk相关的库，需要使用rk的头文件和so。去rockchip上获取头文件和so
本次只提供源码框架，具体调试需要包含rk3588相关的头文件和so，可以去github上获取
*/

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "mpp_packet.h"
#include "rk_mpi.h"
#include "vpu.h"
#include "rk_type.h"

#define MPP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))

typedef enum {
    MPP_CODEC_DEC, 
    MPP_CODEC_ENC, 
} MppCodecType;

typedef enum {
    MPP_CODEC_SIMPLE, 
    MPP_CODEC_ADVANCE, 
} MppEasyType;


class RKMppCodec {

private:
    
    RK_U32 _width;
    RK_U32 _height;
    RK_U32 _hor_stride;
    RK_U32 _ver_stride;
    RK_S32 _rc_mode;
    RK_S32 _frame_num;
    MppFrameFormat _format;
    MppCodingType _type;
    RK_U32 _need_split;
    MppEasyType _easy_type;

    size_t _header_size;
    size_t _frame_size;
    size_t _mdinfo_size;

    MppEncCfg _enc_cfg;
    MppDecCfg _dec_cfg;

    MppCtx _ctx;
    MppApi *_mpi;

    MppBufferGroup _buf_grp;
    MppBuffer _frm_buf;
    MppBuffer _pkt_buf;
    MppBuffer _mdi_buf;

    MppPacket _packet;
    MppFrame _frame;

    RK_U8 *_base;
    MppTask _task;

    RK_S32 EncDefaultStride(RK_U32 width, MppFrameFormat fmt);
    size_t GetFrameSize(MppFrameFormat format, RK_U32 hor_stride, RK_U32 ver_stride);
    size_t GetMediaSize(MppCodingType code_type, RK_U32 hor_stride, RK_U32 ver_stride);
    size_t GetHeadSize(MppFrameFormat format, RK_U32 width, RK_U32 height);
    MPP_RET EncSetCfg();
    MPP_RET EncReadImg(void *buf,void *input,size_t len, RK_U32 width, RK_U32 height,
               RK_U32 hor_stride, RK_U32 ver_stride, MppFrameFormat format);
    MPP_RET DecReadImg(void *buf,void *input,size_t len);
    MPP_RET DumpToFrame(MppFrame frame,std::vector<unsigned char> & output);
public:
    RKMppCodec();
    ~RKMppCodec();

    RKMppCodec(const RKMppCodec &) = delete;
    RKMppCodec(RKMppCodec&&) = delete;

    RKMppCodec& operator=(const RKMppCodec &) = delete;
    RKMppCodec& operator=(RKMppCodec&&) = delete;

    void Init(MppCodecType codec_type, RK_U32 width, RK_U32 height,
            MppFrameFormat format,  MppCodingType code_type,
            RK_S32 need_split = 0, MppEasyType easy_type = MPP_CODEC_SIMPLE );
    
    MPP_RET Decode(void *input, size_t len, std::vector<unsigned char> & output);
    MPP_RET Encode(void *input, size_t len, std::vector<unsigned char> & output);

    MPP_RET ReSet();
};



#endif
