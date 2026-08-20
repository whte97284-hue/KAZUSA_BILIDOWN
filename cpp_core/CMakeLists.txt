cmake_minimum_required(VERSION 3.16)
project(KAZUSA_BILIDOWN VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network)

add_executable(KAZUSA_BILIDOWN
    main_qml.cpp
    app.rc
    qml_resources.qrc
    BiliTypes.hpp
    BiliParser.hpp
    BiliParser.cpp
    BiliHttpClient.hpp
    BiliHttpClient.cpp
    BiliSigner.hpp
    BiliSigner.cpp
    BiliDownloader.hpp
    BiliDownloader.cpp
    RateLimiter.hpp
    RateLimiter.cpp
    MultiThreadDownloader.hpp
    MultiThreadDownloader.cpp
    BiliAuth.hpp
    BiliAuth.cpp
    Bento4Muxer.hpp
    Bento4Muxer.cpp
    glue/BiliTrackModel.hpp
    glue/BiliTrackModel.cpp
    glue/BiliTaskModel.hpp
    glue/BiliTaskModel.cpp
    glue/BiliHistoryModel.hpp
    glue/BiliHistoryModel.cpp
    glue/BiliController.hpp
    glue/BiliController.cpp
    glue/FluTheme.hpp
)

target_include_directories(KAZUSA_BILIDOWN PRIVATE . glue)

target_link_libraries(KAZUSA_BILIDOWN PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Network
    winhttp
    winmm
    advapi32
    bento4
)

# 隐藏控制台窗口: 链接为 Windows GUI 子系统 (Subsystem:WINDOWS), 日志走 bili_ui.log
if(WIN32 AND MSVC)
    target_link_options(KAZUSA_BILIDOWN PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# 无头下载链路端到端验证工具 (驱动与 GUI 相同的 BiliController)
add_executable(BiliChainTest
    test_chain.cpp
    BiliTypes.hpp
    BiliParser.hpp
    BiliParser.cpp
    BiliHttpClient.hpp
    BiliHttpClient.cpp
    BiliSigner.hpp
    BiliSigner.cpp
    BiliDownloader.hpp
    BiliDownloader.cpp
    RateLimiter.hpp
    RateLimiter.cpp
    MultiThreadDownloader.hpp
    MultiThreadDownloader.cpp
    BiliAuth.hpp
    BiliAuth.cpp
    Bento4Muxer.hpp
    Bento4Muxer.cpp
    glue/BiliTrackModel.hpp
    glue/BiliTrackModel.cpp
    glue/BiliTaskModel.hpp
    glue/BiliTaskModel.cpp
    glue/BiliHistoryModel.hpp
    glue/BiliHistoryModel.cpp
    glue/BiliController.hpp
    glue/BiliController.cpp
)
target_include_directories(BiliChainTest PRIVATE . glue)
target_link_libraries(BiliChainTest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    winhttp
    winmm
    advapi32
    bento4
)

# CDN 流下载探针: 绕过队列直接测 CDN 连通性, 定位下载卡点
add_executable(BiliProbe
    probe_stream.cpp
    BiliTypes.hpp
    BiliParser.hpp
    BiliParser.cpp
    BiliHttpClient.hpp
    BiliHttpClient.cpp
    BiliSigner.hpp
    BiliSigner.cpp
    BiliDownloader.hpp
    BiliDownloader.cpp
    RateLimiter.hpp
    RateLimiter.cpp
    MultiThreadDownloader.hpp
    MultiThreadDownloader.cpp
    BiliAuth.hpp
    BiliAuth.cpp
)
target_include_directories(BiliProbe PRIVATE . glue)
target_link_libraries(BiliProbe PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    winhttp
    advapi32
)

# ================= Bento4 静态库与无损混流引擎 =================
set(BENTO4_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Bento4/Source/C++)

file(GLOB BENTO4_SOURCES
    ${BENTO4_SRC_DIR}/Codecs/*.cpp
    ${BENTO4_SRC_DIR}/Core/*.cpp
    ${BENTO4_SRC_DIR}/Crypto/*.cpp
    ${BENTO4_SRC_DIR}/MetaData/*.cpp
    ${BENTO4_SRC_DIR}/System/StdC/*.cpp
)
if(WIN32)
    list(APPEND BENTO4_SOURCES ${BENTO4_SRC_DIR}/System/Win32/Ap4Win32Random.cpp)
endif()

add_library(bento4 STATIC ${BENTO4_SOURCES})
target_include_directories(bento4 PUBLIC
    ${BENTO4_SRC_DIR}/Core
    ${BENTO4_SRC_DIR}/Codecs
    ${BENTO4_SRC_DIR}/Crypto
    ${BENTO4_SRC_DIR}/MetaData
    ${BENTO4_SRC_DIR}/System/StdC
)
if(MSVC)
    target_compile_definitions(bento4 PRIVATE _CRT_SECURE_NO_WARNINGS _LIB)
endif()

# Bento4 独立混流测试可执行程序 (Phase 0 / Phase 5 验证)
add_executable(Bento4Test
    test_mux.cpp
    Bento4Muxer.hpp
    Bento4Muxer.cpp
)
target_include_directories(Bento4Test PRIVATE . ${BENTO4_SRC_DIR}/Core)
target_link_libraries(Bento4Test PRIVATE bento4)

# 番剧多P解析完备性临时验证 (验证后移除)
add_executable(PgcTest
    test_pgc.cpp
    BiliTypes.hpp
    BiliParser.hpp
    BiliParser.cpp
    BiliHttpClient.hpp
    BiliHttpClient.cpp
    BiliSigner.hpp
    BiliSigner.cpp
    BiliDownloader.hpp
    BiliDownloader.cpp
    RateLimiter.hpp
    RateLimiter.cpp
    MultiThreadDownloader.hpp
    MultiThreadDownloader.cpp
    BiliAuth.hpp
    BiliAuth.cpp
)
target_include_directories(PgcTest PRIVATE . glue)
target_link_libraries(PgcTest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    winhttp
    advapi32
)

# 番剧多P批量下载状态完备性验证 (入队→逐P启动→产物校验)
add_executable(PgcMultiTest
    test_pgc_multi.cpp
    BiliTypes.hpp
    BiliParser.hpp
    BiliParser.cpp
    BiliHttpClient.hpp
    BiliHttpClient.cpp
    BiliSigner.hpp
    BiliSigner.cpp
    BiliDownloader.hpp
    BiliDownloader.cpp
    RateLimiter.hpp
    RateLimiter.cpp
    MultiThreadDownloader.hpp
    MultiThreadDownloader.cpp
    BiliAuth.hpp
    BiliAuth.cpp
    Bento4Muxer.hpp
    Bento4Muxer.cpp
    glue/BiliTrackModel.hpp
    glue/BiliTrackModel.cpp
    glue/BiliTaskModel.hpp
    glue/BiliTaskModel.cpp
    glue/BiliHistoryModel.hpp
    glue/BiliHistoryModel.cpp
    glue/BiliController.hpp
    glue/BiliController.cpp
)
target_include_directories(PgcMultiTest PRIVATE . glue)
target_link_libraries(PgcMultiTest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    winhttp
    winmm
    advapi32
    bento4
)

# 多线程分片下载 Phase 0 硬门可行性验证
add_executable(Phase0Test
    test_phase0_multithread.cpp
    BiliTypes.hpp
    BiliParser.hpp
    BiliParser.cpp
    BiliHttpClient.hpp
    BiliHttpClient.cpp
    BiliSigner.hpp
    BiliSigner.cpp
    BiliDownloader.hpp
    BiliDownloader.cpp
    RateLimiter.hpp
    RateLimiter.cpp
    MultiThreadDownloader.hpp
    MultiThreadDownloader.cpp
    BiliAuth.hpp
    BiliAuth.cpp
    Bento4Muxer.hpp
    Bento4Muxer.cpp
)
target_include_directories(Phase0Test PRIVATE . glue)
target_link_libraries(Phase0Test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    winhttp
    advapi32
    bento4
)
