
#pragma once

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

extern TFT_eSPI tft;
extern XPT2046_Touchscreen touch;
extern SPIClass touchSPI;

void initializeDisplay();
void initializeTouch();
