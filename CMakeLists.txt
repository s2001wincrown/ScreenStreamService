cmake_minimum_required(VERSION 3.15)
project(ScreenStream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SCREENSTREAM_ENABLE_QSV "Build Intel Media SDK encoder" ON)
option(SCREENSTREAM_ENABLE_NVENC "Build NVIDIA NVENC encoder" ON)
set(SCREENSTREAM_QSV_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/libmfx/VS2017x64" CACHE PATH
    "Intel Media SDK Windows x64 package root")
set(SCREENSTREAM_QSV_LIB "${SCREENSTREAM_QSV_ROOT}/lib/mfx.lib" CACHE FILEPATH
    "Path to the Intel Media SDK dispatcher library")

# Windows 平台
if (WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)
endif()

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 源文件
set(SOURCES
    src/Config.cpp
    src/Logger.cpp
    src/DxCapture.cpp
    src/DxgiConvert.cpp
    src/X264Encoder.cpp
    src/EncoderFactory.cpp
    src/FlvMuxer.cpp
    src/RtmpPublisher.cpp
    src/HttpPreviewServer.cpp
    src/JpegEncoder.cpp
    src/ScreenStreamService.cpp
    src/ServiceMain.cpp
    src/DesktopApp.cpp
)

add_executable(ScreenStream WIN32 ${SOURCES})

if (SCREENSTREAM_ENABLE_QSV)
    target_sources(ScreenStream PRIVATE src/QsvEncoder.cpp)
    target_compile_definitions(ScreenStream PRIVATE SCREENSTREAM_ENABLE_QSV=1)
    if (NOT SCREENSTREAM_QSV_LIB)
        message(FATAL_ERROR "SCREENSTREAM_ENABLE_QSV requires SCREENSTREAM_QSV_LIB")
    endif()
    target_link_libraries(ScreenStream PRIVATE "${SCREENSTREAM_QSV_LIB}")
endif()

if (SCREENSTREAM_ENABLE_NVENC)
    target_sources(ScreenStream PRIVATE src/NvencEncoder.cpp)
    target_compile_definitions(ScreenStream PRIVATE SCREENSTREAM_ENABLE_NVENC=1)
endif()

# 链接库
target_link_libraries(ScreenStream PRIVATE
    AdvAPI32
    Shell32
    d3d11
    dxgi
    windowscodecs
    ws2_32
    ${CMAKE_SOURCE_DIR}/thirdparty/libx264/lib/x64/libx264.lib
    ${CMAKE_SOURCE_DIR}/thirdparty/librtmp-and-rtmpdump-for-Windows/x64/Release/librtmp.lib
    ${CMAKE_SOURCE_DIR}/thirdparty/openssl1_0-win-build-main/build-VS2026/x64/Release/libeay32.lib
    ${CMAKE_SOURCE_DIR}/thirdparty/openssl1_0-win-build-main/build-VS2026/x64/Release/ssleay32.lib
)

set(SCREENSTREAM_RUNTIME_FILES
    "${CMAKE_SOURCE_DIR}/thirdparty/librtmp-and-rtmpdump-for-Windows/x64/Release/librtmp.dll"
    "${CMAKE_SOURCE_DIR}/thirdparty/openssl1_0-win-build-main/build-VS2026/x64/Release/libeay32.dll"
    "${CMAKE_SOURCE_DIR}/thirdparty/openssl1_0-win-build-main/build-VS2026/x64/Release/ssleay32.dll"
    "${CMAKE_SOURCE_DIR}/Config.ini"
)
foreach(runtime_file IN LISTS SCREENSTREAM_RUNTIME_FILES)
    if (NOT EXISTS "${runtime_file}")
        message(FATAL_ERROR "Required ScreenStream runtime file not found: ${runtime_file}")
    endif()
    add_custom_command(TARGET ScreenStream POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${runtime_file}" "$<TARGET_FILE_DIR:ScreenStream>"
        VERBATIM
    )
endforeach()

target_include_directories(ScreenStream PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/thirdparty/libx264/include
    ${CMAKE_SOURCE_DIR}/thirdparty/librtmp-and-rtmpdump-for-Windows/librtmp
)

if (MSVC)
    target_compile_options(ScreenStream PRIVATE /utf-8)
endif()

# 如果使用 NVIDIA Codec SDK，添加头文件路径
target_include_directories(ScreenStream PRIVATE
    ${CMAKE_SOURCE_DIR}/thirdparty/Video_Codec_SDK_13.1.24/Interface
    ${CMAKE_SOURCE_DIR}/thirdparty/librtmp-and-rtmpdump-for-Windows/include
    ${CMAKE_SOURCE_DIR}/thirdparty/MediaSDK-intel-mediasdk-22.5.4/api/include
    ${SCREENSTREAM_QSV_ROOT}/include/mfx
)
