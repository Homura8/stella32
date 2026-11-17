#ifndef STELLA_CYD_TFT_SETUP_H
#define STELLA_CYD_TFT_SETUP_H
// TFT_eSPI configuration for the ESP32-2432S028R "Cheap Yellow Display".
// TFT_eSPI 2.5.43+ discovers a sketch-local tft_setup.h for both the sketch
// and the library. Keep these values in sync with stella/boards/cyd/cyd_board.h
// (a static_assert checks the backlight pin).

#define USER_SETUP_INFO "Stella CYD ESP32-2432S028R"
#define USER_SETUP_ID 2432028

#define ILI9341_2_DRIVER
#define TFT_INVERSION_ON

// LCD on HSPI. Touch (XPT2046) is driven separately on VSPI by the board.
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH
#define USE_HSPI_PORT

// 20 MHz is what Celestia Nightstand ran on this hardware; try 40 MHz once verified.
#define SPI_FREQUENCY      20000000
#define SPI_READ_FREQUENCY 10000000

// Stella renders its own anti-aliased fonts; no TFT_eSPI fonts are needed.

#endif
