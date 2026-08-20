#pragma once

#include <string>

/**
 * @brief Bento4 无损 MP4 混流引擎
 * 
 * 基于 Bento4 C++ 静态库（Apache 2.0），实现原生的 DASH video.m4s + audio.m4s
 * 进程内容器级无损 remux（重新索引 sample offsets，直接复用 sample 数据包，零转码、极快且无音质画质损失）。
 */
class Bento4Muxer {
public:
    /**
     * @brief 校验媒体文件合法性 (存在、非空且前 64 字节包含 MP4 Box 魔数)
     * @param filePath 输入文件 UTF-8 路径
     * @param error 出错时填写的错误信息
     * @return true 通过校验, false 校验失败
     */
    static bool validateMp4File(const std::string &filePath, std::string &error);

    /**
     * @brief 无损合并视频轨与音频轨为单文件 MP4
     * @param videoM4s 视频轨 m4s 文件路径 (UTF-8)
     * @param audioM4s 音频轨 m4s 文件路径 (UTF-8)
     * @param outMp4 输出目标 MP4 文件路径 (UTF-8)
     * @param error 出错时填写的错误信息
     * @return true 成功, false 失败
     */
    static bool muxToMp4(const std::string &videoM4s,
                         const std::string &audioM4s,
                         const std::string &outMp4,
                         std::string &error);
};
