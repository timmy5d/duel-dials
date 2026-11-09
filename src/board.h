// Pin map and display driver for the Elecrow CrowPanel 1.28" ESP32-S3 rotary display.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Power rails for the display/touch (must be driven high at boot).
#define PIN_PWR_EN_1 1
#define PIN_PWR_EN_2 2
#define PIN_POWER_LED 40

#define PIN_BACKLIGHT 46

#define PIN_TP_SDA 6
#define PIN_TP_SCL 7
#define PIN_TP_INT 5
#define PIN_TP_RST 13

#define PIN_ENC_A 45
#define PIN_ENC_B 42
#define PIN_ENC_SW 41

#define PIN_LEDS 48
#define NUM_LEDS 5

#define SCREEN_W 240
#define SCREEN_H 240

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 10;
      cfg.pin_mosi = 11;
      cfg.pin_miso = -1;
      cfg.pin_dc = 3;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 9;
      cfg.pin_rst = 14;
      cfg.pin_busy = -1;
      cfg.memory_width = SCREEN_W;
      cfg.memory_height = SCREEN_H;
      cfg.panel_width = SCREEN_W;
      cfg.panel_height = SCREEN_H;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
