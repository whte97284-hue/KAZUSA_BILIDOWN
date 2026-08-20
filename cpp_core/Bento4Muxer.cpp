#include "Bento4Muxer.hpp"
#include "Ap4.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall MultiByteToWideChar(
    unsigned int CodePage,
    unsigned long dwFlags,
    const char *lpMultiByteStr,
    int cbMultiByte,
    wchar_t *lpWideCharStr,
    int cchWideChar
);
#endif

static std::wstring u8ToWide(const std::string &u8Str) {
#ifdef _WIN32
    if (u8Str.empty()) return L"";
    int req = MultiByteToWideChar(65001 /*CP_UTF8*/, 0, u8Str.c_str(), -1, nullptr, 0);
    if (req <= 0) return L"";
    std::wstring w(req - 1, L'\0');
    MultiByteToWideChar(65001 /*CP_UTF8*/, 0, u8Str.c_str(), -1, &w[0], req);
    return w;
#else
    return std::wstring(u8Str.begin(), u8Str.end());
#endif
}

static void ensureParentDir(const std::string &filePath) {
    try {
        std::wstring wPath = u8ToWide(filePath);
        fs::path p(wPath);
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path());
        }
    } catch (...) {
    }
}

bool Bento4Muxer::validateMp4File(const std::string &filePath, std::string &error) {
    std::wstring wPath = u8ToWide(filePath);
    FILE *fp = nullptr;
#ifdef _WIN32
    _wfopen_s(&fp, wPath.c_str(), L"rb");
#else
    fp = fopen(filePath.c_str(), "rb");
#endif
    if (!fp) {
        error = "文件不存在或无法打开: " + filePath;
        return false;
    }

    _fseeki64(fp, 0, SEEK_END);
    int64_t size = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);

    if (size < 64) {
        fclose(fp);
        error = "文件过小 (" + std::to_string(size) + " 字节)，判定为无效媒体: " + filePath;
        return false;
    }

    char header[64] = {0};
    size_t readBytes = fread(header, 1, sizeof(header), fp);
    fclose(fp);

    if (readBytes < 16) {
        error = "无法读取媒体头部魔数: " + filePath;
        return false;
    }

    std::string hStr(header, readBytes);
    if (hStr.find("ftyp") == std::string::npos &&
        hStr.find("moov") == std::string::npos &&
        hStr.find("moof") == std::string::npos &&
        hStr.find("mdat") == std::string::npos) {
        error = "文件不含合法 MP4 容器魔数 (ftyp/moov/moof/mdat): " + filePath;
        return false;
    }

    return true;
}

// 辅助函数: 从输入流中提取轨道并构建合成轨道（全面支持标准 MP4 与 B站 DASH fMP4）
static AP4_Track* createMuxTrack(AP4_Movie *srcMovie,
                                 AP4_ByteStream *srcStream,
                                 AP4_Track::Type trackType,
                                 AP4_UI32 outTrackId,
                                 std::string &error) {
    if (!srcMovie || !srcStream) {
        error = "源 Movie 或 ByteStream 为空";
        return nullptr;
    }

    AP4_Track *srcTrack = srcMovie->GetTrack(trackType);
    if (!srcTrack) {
        error = (trackType == AP4_Track::TYPE_VIDEO ? "未找到视频轨" : "未找到音频轨");
        return nullptr;
    }

    // 创建合成 SampleTable
    AP4_SyntheticSampleTable *sampleTable = new AP4_SyntheticSampleTable();
    for (unsigned int i = 0; ; ++i) {
        AP4_SampleDescription *desc = srcTrack->GetSampleDescription(i);
        if (!desc) break;
        sampleTable->AddSampleDescription(desc->Clone());
    }

    AP4_UI64 totalDuration = 0;

    if (srcMovie->HasFragments()) {
        // Fragmented MP4 (fMP4): 使用 AP4_LinearReader 线性流式提取所有 sample
        // 注意: 正常读完返回 AP4_ERROR_EOS, 其余错误码表示流中途损坏/截断
        AP4_LinearReader reader(*srcMovie, srcStream);
        reader.EnableTrack(srcTrack->GetId());
        AP4_Sample sample;
        AP4_DataBuffer data;
        AP4_Result readRes;
        while (AP4_SUCCEEDED(readRes = reader.ReadNextSample(srcTrack->GetId(), sample, data))) {
            sampleTable->AddSample(*srcStream,
                                   sample.GetOffset(),
                                   sample.GetSize(),
                                   sample.GetDuration(),
                                   sample.GetDescriptionIndex(),
                                   sample.GetDts(),
                                   sample.GetCtsDelta(),
                                   sample.IsSync());
            totalDuration += sample.GetDuration();
        }
        if (readRes != AP4_ERROR_EOS) {
            delete sampleTable;
            error = (trackType == AP4_Track::TYPE_VIDEO ? "视频轨" : "音频轨") +
                    std::string("读取中途出错 (错误码 ") + std::to_string(readRes) + ")，流可能损坏或截断";
            return nullptr;
        }
    } else {
        // Unfragmented MP4: 直接从 srcTrack 提取已解析的 sample
        AP4_Cardinal sampleCount = srcTrack->GetSampleCount();
        for (AP4_Ordinal i = 0; i < sampleCount; ++i) {
            AP4_Sample sample;
            if (AP4_SUCCEEDED(srcTrack->GetSample(i, sample))) {
                AP4_ByteStream *dataStream = sample.GetDataStream();
                AP4_ByteStream *streamToUse = dataStream ? dataStream : srcStream;
                sampleTable->AddSample(*streamToUse,
                                       sample.GetOffset(),
                                       sample.GetSize(),
                                       sample.GetDuration(),
                                       sample.GetDescriptionIndex(),
                                       sample.GetDts(),
                                       sample.GetCtsDelta(),
                                       sample.IsSync());
                totalDuration += sample.GetDuration();
                AP4_RELEASE(dataStream);
            }
        }
    }

    if (sampleTable->GetSampleCount() == 0) {
        delete sampleTable;
        error = (trackType == AP4_Track::TYPE_VIDEO ? "视频轨未读取到有效 Sample 数据" : "音频轨未读取到有效 Sample 数据");
        return nullptr;
    }

    AP4_Track *newTrack = new AP4_Track(sampleTable,
                                        outTrackId,
                                        srcTrack->GetMovieTimeScale(),
                                        srcTrack->GetDuration(),
                                        srcTrack->GetMediaTimeScale(),
                                        totalDuration,
                                        srcTrack);
    return newTrack;
}

bool Bento4Muxer::muxToMp4(const std::string &videoM4s,
                           const std::string &audioM4s,
                           const std::string &outMp4,
                           std::string &error) {
    error.clear();  // 每次调用先清空, 避免成功时残留旧错误信息

    // 1. 校验输入
    if (!validateMp4File(videoM4s, error)) return false;
    if (!validateMp4File(audioM4s, error)) return false;

    // 2. 打开视频输入流
    AP4_ByteStream *vStream = nullptr;
    AP4_Result res = AP4_FileByteStream::Create(videoM4s.c_str(), AP4_FileByteStream::STREAM_MODE_READ, vStream);
    if (AP4_FAILED(res) || !vStream) {
        error = "Bento4 无法打开视频轨文件 (" + std::to_string(res) + "): " + videoM4s;
        return false;
    }

    AP4_File vFile(*vStream, true);
    AP4_Movie *vMovie = vFile.GetMovie();
    if (!vMovie) {
        vStream->Release();
        error = "视频轨解析失败 (未找到 Movie 头部): " + videoM4s;
        return false;
    }

    // 3. 打开音频输入流
    AP4_ByteStream *aStream = nullptr;
    res = AP4_FileByteStream::Create(audioM4s.c_str(), AP4_FileByteStream::STREAM_MODE_READ, aStream);
    if (AP4_FAILED(res) || !aStream) {
        vStream->Release();
        error = "Bento4 无法打开音频轨文件 (" + std::to_string(res) + "): " + audioM4s;
        return false;
    }

    AP4_File aFile(*aStream, true);
    AP4_Movie *aMovie = aFile.GetMovie();
    if (!aMovie) {
        vStream->Release();
        aStream->Release();
        error = "音频轨解析失败 (未找到 Movie 头部): " + audioM4s;
        return false;
    }

    // 4. 构建输出 Movie
    AP4_UI64 creationTime = (AP4_UI64)time(nullptr) + 0x7C25B080;
    AP4_Movie *outMovie = new AP4_Movie(0, 0, creationTime, creationTime);

    // 5. 提取视频轨
    AP4_Track *outVTrack = createMuxTrack(vMovie, vStream, AP4_Track::TYPE_VIDEO, 1, error);
    if (!outVTrack) {
        delete outMovie;
        vStream->Release();
        aStream->Release();
        return false;
    }
    outMovie->AddTrack(outVTrack);

    // 6. 提取音频轨
    AP4_Track *outATrack = createMuxTrack(aMovie, aStream, AP4_Track::TYPE_AUDIO, 2, error);
    if (!outATrack) {
        delete outMovie;
        vStream->Release();
        aStream->Release();
        return false;
    }
    outMovie->AddTrack(outATrack);

    outMovie->GetMvhdAtom()->SetNextTrackId(3);

    // 7. 写入输出文件
    ensureParentDir(outMp4);

    AP4_ByteStream *outStream = nullptr;
    res = AP4_FileByteStream::Create(outMp4.c_str(), AP4_FileByteStream::STREAM_MODE_WRITE, outStream);
    if (AP4_FAILED(res) || !outStream) {
        delete outMovie;
        vStream->Release();
        aStream->Release();
        error = "无法创建输出文件 (" + std::to_string(res) + "): " + outMp4;
        return false;
    }

    AP4_Array<AP4_UI32> brands;
    brands.Append(AP4_FILE_BRAND_ISOM);
    brands.Append(AP4_FILE_BRAND_MP42);

    {
        AP4_File outFile(outMovie);
        outFile.SetFileType(AP4_FILE_BRAND_MP42, 1, &brands[0], brands.ItemCount());
        res = AP4_FileWriter::Write(outFile, *outStream);
    }

    outStream->Release();
    vStream->Release();
    aStream->Release();

    if (AP4_FAILED(res)) {
        // 写盘失败: 删除可能残留的半成品输出文件, 避免损坏文件占位
        try {
            fs::remove(fs::path(u8ToWide(outMp4)));
        } catch (...) {
        }
        error = "Bento4 写入 MP4 失败, 错误码: " + std::to_string(res);
        return false;
    }

    return true;
}
