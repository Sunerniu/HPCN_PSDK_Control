/**
 ********************************************************************
 * @file    rtsp_liveview_server.cpp
 * @brief   GStreamer RTSP server for a PSDK Annex-B H.264 stream.
 *********************************************************************
 */

#include "rtsp_liveview_server.hpp"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <dji_logger.h>

#include <atomic>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::mutex g_serverMutex;
std::atomic<bool> g_running(false);
std::atomic<bool> g_hasMedia(false);
std::thread g_mainLoopThread;

GMainContext *g_mainContext = NULL;
GMainLoop *g_mainLoop = NULL;
GstRTSPServer *g_server = NULL;
GstElement *g_appSource = NULL;
guint g_serverSourceId = 0;
RtspLiveviewClientReadyCallback g_clientReadyCallback = NULL;

void ReleaseAppSourceLocked()
{
    if (g_appSource != NULL) {
        gst_object_unref(g_appSource);
        g_appSource = NULL;
    }
    g_hasMedia = false;
}

void OnMediaUnprepared(GstRTSPMedia *, gpointer userData)
{
    GstElement *source = GST_ELEMENT(userData);
    {
        std::lock_guard<std::mutex> lock(g_serverMutex);
        if (source == g_appSource) {
            ReleaseAppSourceLocked();
        }
    }
    gst_object_unref(source);
}

void OnMediaConfigure(GstRTSPMediaFactory *, GstRTSPMedia *media, gpointer)
{
    GstElement *element = gst_rtsp_media_get_element(media);
    if (element == NULL) {
        USER_LOG_ERROR("RTSP media has no GStreamer element");
        return;
    }

    GstElement *source = gst_bin_get_by_name_recurse_up(GST_BIN(element), "live_source");
    gst_object_unref(element);
    if (source == NULL) {
        USER_LOG_ERROR("RTSP pipeline appsrc was not found");
        return;
    }

    g_object_set(G_OBJECT(source),
                 "format", GST_FORMAT_TIME,
                 "is-live", TRUE,
                 "block", FALSE,
                 "do-timestamp", TRUE,
                 NULL);

    RtspLiveviewClientReadyCallback clientReadyCallback = NULL;
    bool publishSource = false;
    {
        std::lock_guard<std::mutex> lock(g_serverMutex);
        if (g_running) {
            ReleaseAppSourceLocked();
            g_appSource = GST_ELEMENT(gst_object_ref(source));
            g_hasMedia = true;
            clientReadyCallback = g_clientReadyCallback;
            publishSource = true;
        }
    }
    if (!publishSource) {
        gst_object_unref(source);
        return;
    }

    // Keep a separate reference until the media is unprepared. The callback
    // only clears the globally published source when it is still the active one.
    g_signal_connect(media, "unprepared", G_CALLBACK(OnMediaUnprepared), gst_object_ref(source));
    gst_object_unref(source);

    USER_LOG_INFO("RTSP client media pipeline is ready");
    if (clientReadyCallback != NULL) {
        clientReadyCallback();
    }
}

void MainLoopEntry()
{
    g_main_context_push_thread_default(g_mainContext);
    g_main_loop_run(g_mainLoop);
    g_main_context_pop_thread_default(g_mainContext);
}

bool RequireElement(const char *name)
{
    GstElementFactory *factory = gst_element_factory_find(name);
    if (factory == NULL) {
        USER_LOG_ERROR("Required GStreamer element is missing: %s", name);
        return false;
    }
    gst_object_unref(factory);
    return true;
}

} // namespace

bool RtspLiveviewServer_Start(const T_RtspLiveviewServerConfig &config,
                              RtspLiveviewClientReadyCallback clientReadyCallback)
{
    if (g_running) {
        return true;
    }

    GError *error = NULL;
    if (!gst_init_check(NULL, NULL, &error)) {
        USER_LOG_ERROR("GStreamer initialization failed: %s",
                       error == NULL ? "unknown error" : error->message);
        if (error != NULL) {
            g_error_free(error);
        }
        return false;
    }

    // Refuse to start if Jetson hardware codecs are unavailable. There is no
    // implicit CPU decoder/encoder fallback in this production path.
    const char *requiredElements[] = {
        "appsrc", "h264parse", "nvv4l2decoder", "nvvidconv",
        "videorate", "nvv4l2h264enc", "rtph264pay", "queue"
    };
    for (size_t i = 0; i < sizeof(requiredElements) / sizeof(requiredElements[0]); ++i) {
        if (!RequireElement(requiredElements[i])) {
            return false;
        }
    }

    g_mainContext = g_main_context_new();
    if (g_mainContext != NULL) {
        g_mainLoop = g_main_loop_new(g_mainContext, FALSE);
    }
    if (g_mainLoop != NULL) {
        g_server = gst_rtsp_server_new();
    }
    if (g_mainContext == NULL || g_mainLoop == NULL || g_server == NULL) {
        USER_LOG_ERROR("Allocate RTSP server objects failed");
        RtspLiveviewServer_Stop();
        return false;
    }

    const std::string service = std::to_string(config.port);
    gst_rtsp_server_set_address(g_server, "0.0.0.0");
    gst_rtsp_server_set_service(g_server, service.c_str());

    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(g_server);
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
    if (mounts == NULL || factory == NULL) {
        USER_LOG_ERROR("Allocate RTSP mount point or media factory failed");
        if (mounts != NULL) {
            g_object_unref(mounts);
        }
        if (factory != NULL) {
            g_object_unref(factory);
        }
        RtspLiveviewServer_Stop();
        return false;
    }

    const uint32_t idrFrames = static_cast<uint32_t>(config.framesPerSecond) *
                               static_cast<uint32_t>(config.idrIntervalSeconds);
    std::ostringstream pipeline;
    pipeline
        << "( appsrc name=live_source "
        << "caps=\"video/x-h264,stream-format=(string)byte-stream\" "
        << "! queue leaky=downstream max-size-buffers=90 max-size-bytes=0 max-size-time=0 "
        << "! h264parse config-interval=-1 "
        << "! video/x-h264,stream-format=(string)byte-stream,alignment=(string)au "
        << "! nvv4l2decoder disable-dpb=true "
        << "! nvvidconv interpolation-method=1 "
        << "! video/x-raw(memory:NVMM),format=(string)NV12,width=(int)" << config.width
        << ",height=(int)" << config.height << " "
        // videorate only drops timestamped NVMM buffers; decode, scaling and
        // encode remain on NVIDIA hardware. drop-only prevents duplication.
        << "! videorate drop-only=true max-rate=" << config.framesPerSecond << " "
        << "! video/x-raw(memory:NVMM),format=(string)NV12,width=(int)" << config.width
        << ",height=(int)" << config.height << ",framerate=(fraction)"
        << config.framesPerSecond << "/1 "
        << "! queue leaky=downstream max-size-buffers=20 max-size-bytes=0 max-size-time=0 "
        << "! nvv4l2h264enc name=live_encoder control-rate=1 bitrate="
        << config.bitrateKbps * 1000U
        << " EnableTwopassCBR=true vbv-size=10"
        << " iframeinterval=" << idrFrames
        << " idrinterval=" << idrFrames
        << " insert-sps-pps=true insert-aud=true maxperf-enable=true "
        << "! video/x-h264,stream-format=(string)byte-stream,alignment=(string)au "
        << "! h264parse config-interval=-1 "
        << "! rtph264pay name=pay0 pt=96 config-interval=-1 mtu=1200 )";
    const std::string pipelineDescription = pipeline.str();
    gst_rtsp_media_factory_set_launch(factory, pipelineDescription.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_set_protocols(
        factory, static_cast<GstRTSPLowerTrans>(GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_TCP));
    gst_rtsp_media_factory_set_latency(factory, 0);
    g_signal_connect(factory, "media-configure", G_CALLBACK(OnMediaConfigure), NULL);
    gst_rtsp_mount_points_add_factory(mounts, config.mountPoint.c_str(), factory);
    g_object_unref(mounts);

    {
        std::lock_guard<std::mutex> lock(g_serverMutex);
        g_clientReadyCallback = clientReadyCallback;
    }
    g_running = true;
    g_serverSourceId = gst_rtsp_server_attach(g_server, g_mainContext);
    if (g_serverSourceId == 0) {
        USER_LOG_ERROR("Bind RTSP server to 0.0.0.0:%u failed",
                       static_cast<unsigned int>(config.port));
        RtspLiveviewServer_Stop();
        return false;
    }

    g_mainLoopThread = std::thread(MainLoopEntry);
    USER_LOG_INFO("RTSP hardware transcoder: %ux%u %u fps %u kbps IDR=%u s",
                  static_cast<unsigned int>(config.width),
                  static_cast<unsigned int>(config.height),
                  static_cast<unsigned int>(config.framesPerSecond),
                  static_cast<unsigned int>(config.bitrateKbps),
                  static_cast<unsigned int>(config.idrIntervalSeconds));
    USER_LOG_INFO("RTSP server listening at rtsp://0.0.0.0:%u%s",
                  static_cast<unsigned int>(config.port), config.mountPoint.c_str());
    return true;
}

void RtspLiveviewServer_Stop(void)
{
    g_running = false;

    GstElement *source = NULL;
    {
        std::lock_guard<std::mutex> lock(g_serverMutex);
        if (g_appSource != NULL) {
            source = GST_ELEMENT(gst_object_ref(g_appSource));
        }
    }
    if (source != NULL) {
        gst_app_src_end_of_stream(GST_APP_SRC(source));
        gst_object_unref(source);
    }

    if (g_mainLoop != NULL) {
        g_main_loop_quit(g_mainLoop);
    }
    if (g_mainLoopThread.joinable()) {
        g_mainLoopThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_serverMutex);
        ReleaseAppSourceLocked();
        g_clientReadyCallback = NULL;
    }

    if (g_serverSourceId != 0 && g_mainContext != NULL) {
        GSource *source = g_main_context_find_source_by_id(g_mainContext, g_serverSourceId);
        if (source != NULL) {
            g_source_destroy(source);
        }
        g_serverSourceId = 0;
    }
    if (g_server != NULL) {
        g_object_unref(g_server);
        g_server = NULL;
    }
    if (g_mainLoop != NULL) {
        g_main_loop_unref(g_mainLoop);
        g_mainLoop = NULL;
    }
    if (g_mainContext != NULL) {
        g_main_context_unref(g_mainContext);
        g_mainContext = NULL;
    }
}

bool RtspLiveviewServer_PushH264(const uint8_t *data, size_t length)
{
    if (!g_running || data == NULL || length == 0) {
        return false;
    }

    GstElement *source = NULL;
    {
        std::lock_guard<std::mutex> lock(g_serverMutex);
        if (g_appSource != NULL) {
            source = GST_ELEMENT(gst_object_ref(g_appSource));
        }
    }
    if (source == NULL) {
        return false;
    }

    GstBuffer *buffer = gst_buffer_new_allocate(NULL, length, NULL);
    if (buffer == NULL) {
        gst_object_unref(source);
        return false;
    }
    gst_buffer_fill(buffer, 0, data, length);

    GstFlowReturn flowReturn = GST_FLOW_ERROR;
    g_signal_emit_by_name(source, "push-buffer", buffer, &flowReturn);
    gst_buffer_unref(buffer);
    gst_object_unref(source);

    return flowReturn == GST_FLOW_OK;
}

bool RtspLiveviewServer_HasMedia(void)
{
    return g_hasMedia;
}
