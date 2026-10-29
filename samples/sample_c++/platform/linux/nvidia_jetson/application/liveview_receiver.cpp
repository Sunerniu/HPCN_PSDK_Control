/**
 ********************************************************************
 * @file    liveview_receiver.cpp
 * @brief   PSDK Liveview H.264 receiver with an embedded RTSP server.
 *********************************************************************
 */

#include "liveview_receiver.hpp"
#include "rtsp_liveview_server.hpp"

#include <arpa/inet.h>
#include <dji_error.h>
#include <dji_liveview.h>
#include <dji_logger.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <strings.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

const uint16_t kDefaultRtspPort = 8554;
const char *const kDefaultRtspPath = "/m350/fpv";
const unsigned long kDefaultIFrameIntervalSeconds = 2;
const size_t kDefaultQueueBytes = 8U * 1024U * 1024U;

std::atomic<bool> g_running(false);
std::atomic<bool> g_streamStarted(false);
std::atomic<bool> g_requestIntraframe(false);
std::atomic<uint64_t> g_receivedBytes(0);
std::atomic<uint64_t> g_receivedCallbacks(0);
std::atomic<uint64_t> g_droppedCallbacks(0);

std::mutex g_queueMutex;
std::condition_variable g_queueCondition;
std::deque<std::vector<uint8_t> > g_queue;
size_t g_queuedBytes = 0;
size_t g_maxQueueBytes = kDefaultQueueBytes;
unsigned long g_iFrameIntervalSeconds = kDefaultIFrameIntervalSeconds;
std::thread g_worker;

FILE *g_outputFile = NULL;
E_DjiLiveViewCameraPosition g_position = DJI_LIVEVIEW_CAMERA_POSITION_FPV;
E_DjiLiveViewCameraSource g_source = DJI_LIVEVIEW_CAMERA_SOURCE_DEFAULT;

bool IsEnabled(const char *value)
{
    return value != NULL &&
           (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 || strcasecmp(value, "on") == 0);
}

unsigned long ParseUnsigned(const char *value, unsigned long defaultValue,
                            unsigned long minimum, unsigned long maximum)
{
    if (value == NULL || value[0] == '\0') {
        return defaultValue;
    }

    char *end = NULL;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed < minimum || parsed > maximum) {
        return 0;
    }
    return parsed;
}

size_t ParseQueueBytes(const char *value)
{
    const unsigned long megabytes = ParseUnsigned(value, 8, 1, 256);
    return megabytes == 0 ? 0 : static_cast<size_t>(megabytes) * 1024U * 1024U;
}

bool IsValidRtspPath(const std::string &path)
{
    if (path.empty() || path[0] != '/' || path.size() > 128) {
        return false;
    }
    for (size_t i = 0; i < path.size(); ++i) {
        const char character = path[i];
        const bool valid = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') ||
                           character == '/' || character == '-' || character == '_' || character == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

std::string ResolveAdvertisedHost()
{
    const char *configuredHost = getenv("RTSP_ADVERTISED_HOST");
    if (configuredHost != NULL && configuredHost[0] != '\0') {
        return configuredHost;
    }

    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) != 0) {
        return "<AGX_IP>";
    }

    std::string bestAddress;
    int bestScore = -1;
    for (struct ifaddrs *entry = interfaces; entry != NULL; entry = entry->ifa_next) {
        if (entry->ifa_addr == NULL || entry->ifa_addr->sa_family != AF_INET ||
            (entry->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        char address[INET_ADDRSTRLEN] = {0};
        const struct sockaddr_in *ipv4 =
            reinterpret_cast<const struct sockaddr_in *>(entry->ifa_addr);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address)) == NULL) {
            continue;
        }

        const std::string interfaceName = entry->ifa_name == NULL ? "" : entry->ifa_name;
        int score = 0;
        if (strncmp(address, "10.29.", 6) == 0) {
            score += 100;
        }
        if (interfaceName.find("eth") == 0 || interfaceName.find("en") == 0 ||
            interfaceName.find("wl") == 0) {
            score += 20;
        }
        if (interfaceName.find("docker") == 0 || interfaceName.find("virbr") == 0 ||
            interfaceName.find("l4tbr") == 0 || interfaceName.find("usb") == 0) {
            score -= 20;
        }
        if (score > bestScore) {
            bestScore = score;
            bestAddress = address;
        }
    }
    freeifaddrs(interfaces);
    return bestAddress.empty() ? "<AGX_IP>" : bestAddress;
}

bool ParseCamera(const char *value, E_DjiLiveViewCameraPosition *position)
{
    const std::string camera = value == NULL ? "fpv" : value;
    if (camera == "fpv" || camera == "0") {
        *position = DJI_LIVEVIEW_CAMERA_POSITION_FPV;
    } else if (camera == "main" || camera == "1") {
        *position = DJI_LIVEVIEW_CAMERA_POSITION_NO_1;
    } else if (camera == "vice" || camera == "2") {
        *position = DJI_LIVEVIEW_CAMERA_POSITION_NO_2;
    } else if (camera == "top" || camera == "3") {
        *position = DJI_LIVEVIEW_CAMERA_POSITION_NO_3;
    } else {
        return false;
    }
    return true;
}

bool ParseSource(const char *value, E_DjiLiveViewCameraSource *source)
{
    const std::string streamSource = value == NULL ? "default" : value;
    if (streamSource == "default" || streamSource == "0") {
        *source = DJI_LIVEVIEW_CAMERA_SOURCE_DEFAULT;
    } else if (streamSource == "wide" || streamSource == "1") {
        *source = static_cast<E_DjiLiveViewCameraSource>(1);
    } else if (streamSource == "zoom" || streamSource == "2") {
        *source = static_cast<E_DjiLiveViewCameraSource>(2);
    } else if (streamSource == "ir" || streamSource == "3") {
        *source = static_cast<E_DjiLiveViewCameraSource>(3);
    } else {
        return false;
    }
    return true;
}

void OnRtspClientReady()
{
    // A newly connected decoder must begin at SPS/PPS + IDR rather than in the
    // middle of a GOP. rtph264pay repeats SPS/PPS; this requests the new IDR.
    g_requestIntraframe = true;
    g_queueCondition.notify_one();
}

void RequestIntraframe()
{
    if (!g_streamStarted) {
        return;
    }
    const T_DjiReturnCode code = DjiLiveview_RequestIntraframeFrameData(g_position, g_source);
    if (code != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_WARN("Request Liveview intraframe failed: 0x%08llX", code);
    }
}

void WorkerLoop()
{
    std::chrono::steady_clock::time_point lastFlush = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastStats = lastFlush;
    std::chrono::steady_clock::time_point lastIFrameRequest = lastFlush;

    while (true) {
        std::vector<uint8_t> buffer;
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCondition.wait_for(lock, std::chrono::milliseconds(200), [] {
                return !g_running || !g_queue.empty() || g_requestIntraframe.load();
            });
            if (g_queue.empty() && !g_running) {
                break;
            }
            if (!g_queue.empty()) {
                buffer.swap(g_queue.front());
                g_queuedBytes -= buffer.size();
                g_queue.pop_front();
            }
        }

        if (!buffer.empty()) {
            if (g_outputFile != NULL) {
                const size_t written = fwrite(&buffer[0], 1, buffer.size(), g_outputFile);
                if (written != buffer.size()) {
                    USER_LOG_ERROR("Liveview file write failed");
                }
            }
            RtspLiveviewServer_PushH264(&buffer[0], buffer.size());
        }

        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const bool periodicIFrame =
            now - lastIFrameRequest >= std::chrono::seconds(g_iFrameIntervalSeconds);
        if (g_requestIntraframe.exchange(false) || periodicIFrame) {
            RequestIntraframe();
            lastIFrameRequest = now;
        }

        if (g_outputFile != NULL && now - lastFlush >= std::chrono::seconds(1)) {
            fflush(g_outputFile);
            lastFlush = now;
        }
        if (now - lastStats >= std::chrono::seconds(5)) {
            size_t queuedBytes = 0;
            {
                std::lock_guard<std::mutex> lock(g_queueMutex);
                queuedBytes = g_queuedBytes;
            }
            USER_LOG_INFO("Liveview: callbacks=%llu bytes=%llu dropped=%llu queued=%zu rtsp_client=%s",
                          static_cast<unsigned long long>(g_receivedCallbacks.load()),
                          static_cast<unsigned long long>(g_receivedBytes.load()),
                          static_cast<unsigned long long>(g_droppedCallbacks.load()), queuedBytes,
                          RtspLiveviewServer_HasMedia() ? "yes" : "no");
            lastStats = now;
        }
    }

    if (g_outputFile != NULL) {
        fflush(g_outputFile);
    }
}

void H264Callback(E_DjiLiveViewCameraPosition position, const uint8_t *buffer, uint32_t length)
{
    if (!g_running || position != g_position || buffer == NULL || length == 0) {
        return;
    }

    std::vector<uint8_t> copy(buffer, buffer + length);
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        if (copy.size() > g_maxQueueBytes || g_queuedBytes + copy.size() > g_maxQueueBytes) {
            g_queue.clear();
            g_queuedBytes = 0;
            ++g_droppedCallbacks;
            g_requestIntraframe = true;
        }
        if (copy.size() <= g_maxQueueBytes) {
            g_queuedBytes += copy.size();
            g_queue.push_back(std::move(copy));
        }
    }

    ++g_receivedCallbacks;
    g_receivedBytes += length;
    g_queueCondition.notify_one();
}

void CloseOutputs()
{
    RtspLiveviewServer_Stop();
    if (g_outputFile != NULL) {
        fclose(g_outputFile);
        g_outputFile = NULL;
    }
}

} // namespace

T_DjiReturnCode LiveviewReceiver_InitFromEnvironment(void)
{
    if (!IsEnabled(getenv("LIVEVIEW_ENABLE"))) {
        USER_LOG_INFO("Liveview receiver disabled (set LIVEVIEW_ENABLE=1 to enable)");
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }
    if (g_running) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }
    if (!ParseCamera(getenv("LIVEVIEW_CAMERA"), &g_position) ||
        !ParseSource(getenv("LIVEVIEW_SOURCE"), &g_source)) {
        USER_LOG_ERROR("Invalid LIVEVIEW_CAMERA or LIVEVIEW_SOURCE");
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    g_maxQueueBytes = ParseQueueBytes(getenv("LIVEVIEW_QUEUE_MB"));
    g_iFrameIntervalSeconds = ParseUnsigned(getenv("VIDEO_IDR_INTERVAL_SECONDS"),
                                            kDefaultIFrameIntervalSeconds, 1, 60);
    const bool rtspEnabled = IsEnabled(getenv("RTSP_ENABLE"));
    const unsigned long rtspPort = ParseUnsigned(getenv("RTSP_PORT"),
                                                 kDefaultRtspPort, 1, 65535);
    const char *rtspPathValue = getenv("RTSP_PATH");
    const std::string rtspPath = rtspPathValue == NULL || rtspPathValue[0] == '\0'
                                     ? kDefaultRtspPath
                                     : rtspPathValue;
    const unsigned long videoWidth = ParseUnsigned(getenv("VIDEO_WIDTH"), 640, 160, 4096);
    const unsigned long videoHeight = ParseUnsigned(getenv("VIDEO_HEIGHT"), 360, 90, 2160);
    const unsigned long videoFps = ParseUnsigned(getenv("VIDEO_FPS"), 10, 1, 60);
    const unsigned long videoBitrateKbps =
        ParseUnsigned(getenv("VIDEO_BITRATE_KBPS"), 800, 64, 50000);
    if (g_maxQueueBytes == 0 || g_iFrameIntervalSeconds == 0 || rtspPort == 0 ||
        videoWidth == 0 || videoHeight == 0 || videoFps == 0 || videoBitrateKbps == 0 ||
        (videoWidth % 2) != 0 || (videoHeight % 2) != 0 ||
        !IsValidRtspPath(rtspPath)) {
        USER_LOG_ERROR("Invalid RTSP/video configuration (dimensions must be even)");
        return DJI_ERROR_SYSTEM_MODULE_CODE_INVALID_PARAMETER;
    }

    const char *outputPath = getenv("LIVEVIEW_OUTPUT_FILE");
    if (outputPath != NULL && outputPath[0] != '\0') {
        g_outputFile = fopen(outputPath, "wb");
        if (g_outputFile == NULL) {
            USER_LOG_ERROR("Open Liveview output failed (%s): %s", outputPath, strerror(errno));
            return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
        }
    }

    T_RtspLiveviewServerConfig rtspConfig;
    rtspConfig.port = static_cast<uint16_t>(rtspPort);
    rtspConfig.mountPoint = rtspPath;
    rtspConfig.width = static_cast<uint16_t>(videoWidth);
    rtspConfig.height = static_cast<uint16_t>(videoHeight);
    rtspConfig.framesPerSecond = static_cast<uint16_t>(videoFps);
    rtspConfig.bitrateKbps = static_cast<uint32_t>(videoBitrateKbps);
    rtspConfig.idrIntervalSeconds = static_cast<uint16_t>(g_iFrameIntervalSeconds);
    if (rtspEnabled && !RtspLiveviewServer_Start(rtspConfig, OnRtspClientReady)) {
        CloseOutputs();
        return DJI_ERROR_SYSTEM_MODULE_CODE_SYSTEM_ERROR;
    }

    T_DjiReturnCode code = DjiLiveview_Init();
    if (code != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Liveview init failed: 0x%08llX", code);
        CloseOutputs();
        return code;
    }

    g_receivedBytes = 0;
    g_receivedCallbacks = 0;
    g_droppedCallbacks = 0;
    g_requestIntraframe = false;
    g_running = true;
    g_worker = std::thread(WorkerLoop);

    code = DjiLiveview_StartH264Stream(g_position, g_source, H264Callback);
    if (code != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Start Liveview H264 stream failed: 0x%08llX", code);
        g_running = false;
        g_queueCondition.notify_all();
        g_worker.join();
        DjiLiveview_Deinit();
        CloseOutputs();
        return code;
    }

    g_streamStarted = true;
    g_requestIntraframe = true;
    g_queueCondition.notify_one();
    const std::string advertisedHost = ResolveAdvertisedHost();
    USER_LOG_INFO("Liveview receiver started: camera=%d source=%d file=%s queue=%zu MB",
                  static_cast<int>(g_position), static_cast<int>(g_source),
                  outputPath == NULL || outputPath[0] == '\0' ? "off" : outputPath,
                  g_maxQueueBytes / (1024U * 1024U));
    if (rtspEnabled) {
        USER_LOG_INFO("RTSP URL: rtsp://%s:%lu%s video=%lux%lu@%lu fps bitrate=%lu kbps IDR=%lu s",
                      advertisedHost.c_str(), rtspPort, rtspPath.c_str(), videoWidth, videoHeight,
                      videoFps, videoBitrateKbps, g_iFrameIntervalSeconds);
    } else {
        USER_LOG_INFO("RTSP output disabled (set RTSP_ENABLE=1 to enable)");
    }
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode LiveviewReceiver_DeInit(void)
{
    if (!g_running && !g_streamStarted) {
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    T_DjiReturnCode stopCode = DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    if (g_streamStarted.exchange(false)) {
        stopCode = DjiLiveview_StopH264Stream(g_position, g_source);
    }

    g_running = false;
    g_queueCondition.notify_all();
    if (g_worker.joinable()) {
        g_worker.join();
    }

    const T_DjiReturnCode deinitCode = DjiLiveview_Deinit();
    CloseOutputs();
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_queue.clear();
        g_queuedBytes = 0;
    }

    USER_LOG_INFO("Liveview receiver stopped");
    return stopCode != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ? stopCode : deinitCode;
}

bool LiveviewReceiver_IsRunning(void)
{
    return g_running && g_streamStarted;
}
