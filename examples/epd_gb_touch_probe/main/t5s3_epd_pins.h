#pragma once

#include <stdint.h>

namespace t5s3_epd {

static constexpr const char *kBoardName = "LilyGO T5S3-4.7-e-paper-PRO";

static constexpr uint16_t kPanelWidth = 960;
static constexpr uint16_t kPanelHeight = 540;

static constexpr uint16_t kActiveX = 0;
static constexpr uint16_t kActiveY = 0;
static constexpr uint16_t kActiveWidth = 960;
static constexpr uint16_t kActiveHeight = 540;

static constexpr uint8_t kI2cSda = 39;
static constexpr uint8_t kI2cScl = 40;
static constexpr uint8_t kTouchInt = 3;
static constexpr uint8_t kTouchRst = 9;

static constexpr uint8_t kPca9535Address = 0x20;
static constexpr uint8_t kTps65185Address = 0x68;

static constexpr uint8_t kPcaBitEpdOe = 0;
static constexpr uint8_t kPcaBitEpdMode = 1;
static constexpr uint8_t kPcaBitButton = 2;
static constexpr uint8_t kPcaBitTpsPwrup = 3;
static constexpr uint8_t kPcaBitVcomCtrl = 4;
static constexpr uint8_t kPcaBitTpsWakeup = 5;
static constexpr uint8_t kPcaBitTpsPwrGood = 6;
static constexpr uint8_t kPcaBitTpsInt = 7;

static constexpr uint8_t kPcaMaskButton = 1U << kPcaBitButton;
static constexpr uint8_t kPcaMaskTpsPwrGood = 1U << kPcaBitTpsPwrGood;
static constexpr uint8_t kPcaMaskTpsInt = 1U << kPcaBitTpsInt;
static constexpr uint8_t kPcaMaskShutdownOutputs =
    (1U << kPcaBitEpdOe) |
    (1U << kPcaBitEpdMode) |
    (1U << kPcaBitTpsPwrup) |
    (1U << kPcaBitVcomCtrl) |
    (1U << kPcaBitTpsWakeup);

}  // namespace t5s3_epd

#ifndef TARGET_FPS
#define TARGET_FPS 18
#endif

#ifndef EPD_PARTIAL_PASSES
#define EPD_PARTIAL_PASSES 1
#endif

#ifndef EPD_DIRTY_PADDING
#define EPD_DIRTY_PADDING 6
#endif

#ifndef EPD_BUS_HZ
#define EPD_BUS_HZ 26600000UL
#endif

#ifndef EPD_VIDEO_TOP_DUMMY_LINES
#define EPD_VIDEO_TOP_DUMMY_LINES 30
#endif

#ifndef EPD_VIDEO_BOTTOM_DUMMY_LINES
#define EPD_VIDEO_BOTTOM_DUMMY_LINES 30
#endif

#ifndef EPD_VCOM_MV
#define EPD_VCOM_MV -1600
#endif
