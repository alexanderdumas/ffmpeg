# ffmpeg
* ffmpeg 实现软解码和硬解码。通常硬解码需要具体的平台支持。这里具体以英伟达Xavier平台为例子。 后续补充瑞芯微RK3588平台MPP。
* 依赖ffmpeg编译安装，编译安装参考官网

# CPU 模式
* cpu软解码，直接按照ffmpeg功能实现即可，这里需要安装ffmpeg。具体安装参考官方文档https://github.com/FFmpeg/FFmpeg

# GPU 模式
* 平台支持硬件解码。一般情况下需要安装特定的版本的ffmepg。针对Xavier，有个版本，需要安装这个https://github.com/LinusCDE/mad-jetson-ffmpeg

