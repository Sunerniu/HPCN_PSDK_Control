/**
 ********************************************************************
 * @file    rtsp_liveview_server.hpp
 * @brief   Publish Annex-B H.264 as a shared RTSP stream.
 *********************************************************************
 */

#ifndef RTSP_LIVEVIEW_SERVER_HPP
#define RTSP_LIVEVIEW_SERVER_HPP

#include <cstddef>
#include <cstdint>
#include <string>

typedef void (*RtspLiveviewClientReadyCallback)(void);

struct T_RtspLiveviewServerConfig {
    uint16_t port;
    std::string mountPoint;
    uint16_t width;
    uint16_t height;
    uint16_t framesPerSecond;
    uint32_t bitrateKbps;
    uint16_t idrIntervalSeconds;
};

bool RtspLiveviewServer_Start(const T_RtspLiveviewServerConfig &config,
                              RtspLiveviewClientReadyCallback clientReadyCallback);
void RtspLiveviewServer_Stop(void);
bool RtspLiveviewServer_PushH264(const uint8_t *data, size_t length);
bool RtspLiveviewServer_HasMedia(void);

#endif // RTSP_LIVEVIEW_SERVER_HPP
