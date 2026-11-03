
#include "display.h"
#include "../config.h"

TFT_eSPI tft = TFT_eSPI();

SPIClass touchSPI(VSPI);

XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

void initializeDisplay() {
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.invertDisplay(true);
    tft.fillScreen(TFT_BLACK);
}

void initializeTouch() {
    touchSPI.begin(
        TOUCH_CLK,
        TOUCH_MISO,
        TOUCH_MOSI,
        TOUCH_CS
    );

    touch.begin(touchSPI);
    touch.setRotation(1);
}
