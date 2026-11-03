
#pragma once

#define APP_NAME "CERBSYS"
#define APP_VERSION "0.1.0"

#define WIFI_TIMEOUT_MS 20000

// CYD display
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240

// XPT2046 touchscreen
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_CLK 25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
