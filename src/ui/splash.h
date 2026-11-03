
#pragma once

#include <TFT_eSPI.h>
#include "colors.h"

inline void drawSplash(TFT_eSPI &tft) {
    tft.fillScreen(Colors::BACKGROUND);

    tft.fillRect(0, 0, 320, 35, Colors::PANEL);

    tft.setTextColor(Colors::ACCENT);
    tft.setTextSize(2);
    tft.setCursor(12, 10);
    tft.print("CERBSYS");

    tft.setTextColor(Colors::WHITE);
    tft.setTextSize(1);
    tft.setCursor(12, 55);
    tft.print("NETTOOLS");

    tft.setTextColor(Colors::GRAY);
    tft.setCursor(12, 75);
    tft.print("Network Diagnostic Platform");

    tft.drawRoundRect(
        10, 105, 300, 55, 7,
        Colors::ACCENT
    );

    tft.setTextColor(Colors::WHITE);
    tft.setTextSize(2);
    tft.setCursor(22, 120);
    tft.print("HARDWARE TEST");

    tft.setTextSize(1);
    tft.setTextColor(Colors::GRAY);
    tft.setCursor(12, 180);
    tft.print("Display: INITIALIZING");

    tft.setCursor(12, 195);
    tft.print("Touch: INITIALIZING");

    tft.setCursor(12, 210);
    tft.print("Wi-Fi: INITIALIZING");
}
