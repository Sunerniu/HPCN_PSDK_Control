/**
 ********************************************************************
 * @file    liveview_receiver.hpp
 * @brief   Receive aircraft H.264 Liveview streams on the Jetson.
 *********************************************************************
 */

#ifndef LIVEVIEW_RECEIVER_HPP
#define LIVEVIEW_RECEIVER_HPP

#include <dji_typedef.h>

/**
 * Start Liveview when LIVEVIEW_ENABLE=1.
 *
 * Environment variables:
 *   LIVEVIEW_CAMERA       fpv, main, vice, or top (default: fpv)
 *   LIVEVIEW_SOURCE       default, wide, zoom, ir, or numeric 0..3
 *   LIVEVIEW_OUTPUT_FILE  optional raw Annex-B H.264 output path
 *   LIVEVIEW_QUEUE_MB     bounded callback queue size (default: 8)
 *   RTSP_ENABLE           enable the Jetson hardware transcoder and RTSP server
 *   RTSP_PORT             RTSP listen port (default: 8554)
 *   RTSP_PATH             RTSP mount path (default: /m350/fpv)
 *   RTSP_ADVERTISED_HOST  optional IP printed in the final URL
 *   VIDEO_WIDTH           encoded width (default: 640)
 *   VIDEO_HEIGHT          encoded height (default: 360)
 *   VIDEO_FPS             encoded frame rate (default: 10)
 *   VIDEO_BITRATE_KBPS    CBR target (default: 800)
 *   VIDEO_IDR_INTERVAL_SECONDS  encoder/source IDR interval (default: 2)
 */
T_DjiReturnCode LiveviewReceiver_InitFromEnvironment(void);

/** Stop the stream, worker thread, file output, and RTSP server. */
T_DjiReturnCode LiveviewReceiver_DeInit(void);

/** Return true after a stream has started successfully. */
bool LiveviewReceiver_IsRunning(void);

#endif // LIVEVIEW_RECEIVER_HPP
