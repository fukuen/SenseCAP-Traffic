namespace lgfx
{
  struct Panel_SenseCapD1 : public lgfx::Panel_ST7701
  {
    static constexpr int32_t i2c_freq = 400000;
    static constexpr int_fast16_t pca9535_i2c_addr = 0x20;
    static constexpr int_fast16_t i2c_port = I2C_NUM_0;
    static constexpr int_fast16_t i2c_sda = GPIO_NUM_39;
    static constexpr int_fast16_t i2c_scl = GPIO_NUM_40;
    Panel_SenseCapD1(void)
    {
    }
    bool init(bool use_reset) override
    {
      lgfx::i2c::init(i2c_port, i2c_sda, i2c_scl);

      lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x06, 0, ~(1<<4), i2c_freq);  // IO0_4: output
      lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x06, 0, ~(1<<5), i2c_freq);  // IO0_5: output
      lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x07, 0, ~(1<<0), i2c_freq);  // IO1_0: output
      lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x07, 0, ~(1<<2), i2c_freq);  // IO1_10: output

      if (!Panel_RGB::init(use_reset))
      {
        return false;
      }

      int32_t pin_mosi = _config_detail.pin_mosi;
      int32_t pin_sclk = _config_detail.pin_sclk;
      if (pin_mosi >= 0 && pin_sclk >= 0)
      {
        lgfx::gpio::pin_backup_t backup_pins[] = { (gpio_num_t)pin_mosi, (gpio_num_t)pin_sclk };

        lgfx::gpio_lo(pin_mosi);
        lgfx::pinMode(pin_mosi, pin_mode_t::output);
        lgfx::gpio_lo(pin_sclk);
        lgfx::pinMode(pin_sclk, pin_mode_t::output);


  //      int32_t pin_cs = _config_detail.pin_cs;
  //      lgfx::gpio_lo(pin_cs);
        cs_control(false);

        writeCommand(0xFF, 1);
        writeData(0x77, 1);
        writeData(0x01, 1);
        writeData(0x00, 2);
        writeData(0x10, 1);

        // 0xC0 : LNSET : Display Line Setting
        writeCommand(0xC0, 1);
        uint32_t line1 = (_cfg.panel_height >> 3) + 1;
        uint32_t line2 = (_cfg.panel_height >> 1) & 3;
        writeData(line1 + (line2 ? 0x80 : 0x00), 1);
        writeData(line2, 1);

        // 0xC3 : RGBCTRL
        auto cfg = ((Bus_RGB*)_bus)->config();
        writeCommand(0xC3, 1);
        uint32_t rgbctrl = 0;
        if ( cfg.de_idle_high  ) rgbctrl += 0x01;
        if ( cfg.pclk_idle_high) rgbctrl += 0x02;
        if (!cfg.hsync_polarity) rgbctrl += 0x04;
        if (!cfg.vsync_polarity) rgbctrl += 0x08;
        writeData(rgbctrl, 1);
        writeData(0x10, 1);
        writeData(0x08, 1);

        for (uint8_t i = 0; auto cmds = getInitCommands(i); i++)
        {
          command_list(cmds);
        }

  //      lgfx::gpio_hi(pin_cs);
        cs_control(true);
        for (auto &bup : backup_pins) { bup.restore(); }
      }

      return true;
    }

//    void reset(void) override
    void reset(void)
    {
      lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x02, 0, ~(1<<5), i2c_freq);  // LCD_RST
      lgfx::delay(40);
      lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x02, (1<<5), ~0, i2c_freq);  // LCD_RST
      lgfx::delay(140);
    }

    void cs_control(bool flg) override
    {
      if (flg)
      {
        lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x02, (1<<4), ~0, i2c_freq);  // LCD_CS
      }
      else
      {
        lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x02, 0, ~(1<<4), i2c_freq);  // LCD_CS
      }
    }
  };

  struct Touch_SenseCapD1 : public lgfx::Touch_FT5x06
  {
    static constexpr int32_t i2c_freq = 400000;
    static constexpr int_fast16_t pca9535_i2c_addr = 0x20;
    static constexpr int_fast16_t i2c_port = I2C_NUM_0;
    Touch_SenseCapD1(void)
    {
    }

    bool init(void) override
    {
      lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x02, 0, ~(1<<7), i2c_freq);  // TP_RST
      lgfx::delay(10);
      lgfx::i2c::writeRegister8(i2c_port, pca9535_i2c_addr, 0x02, (1<<7), ~0, i2c_freq);  // TP_RST
      lgfx::delay(10);

      return lgfx::Touch_FT5x06::init();
    }
  };
}

class LGFX : public lgfx::LGFX_Device
{
public:

  lgfx::Bus_RGB          _bus_instance;
  lgfx::Panel_SenseCapD1 _panel_instance;
  lgfx::Touch_SenseCapD1 _touch_instance;
  lgfx::Light_PWM        _light_instance;

  LGFX(void)
  {
    {
      auto cfg = _panel_instance.config();

      cfg.memory_width  = 480;
      cfg.memory_height = 480;
      cfg.panel_width  = 480;
      cfg.panel_height = 480;

      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation  = 2;

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _panel_instance.config_detail();

      cfg.use_psram = 1;
      cfg.pin_cs = -1;
      cfg.pin_sclk = 41;
//      cfg.pin_miso = 47;
      cfg.pin_mosi = 48;

      _panel_instance.config_detail(cfg);
    }

    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0  = GPIO_NUM_15; // B0
      cfg.pin_d1  = GPIO_NUM_14; // B1
      cfg.pin_d2  = GPIO_NUM_13; // B2
      cfg.pin_d3  = GPIO_NUM_12; // B3
      cfg.pin_d4  = GPIO_NUM_11; // B4
      cfg.pin_d5  = GPIO_NUM_10; // G0
      cfg.pin_d6  = GPIO_NUM_9;  // G1
      cfg.pin_d7  = GPIO_NUM_8;  // G2
      cfg.pin_d8  = GPIO_NUM_7;  // G3
      cfg.pin_d9  = GPIO_NUM_6;  // G4
      cfg.pin_d10 = GPIO_NUM_5;  // G5
      cfg.pin_d11 = GPIO_NUM_4;  // R0
      cfg.pin_d12 = GPIO_NUM_3;  // R1
      cfg.pin_d13 = GPIO_NUM_2;  // R2
      cfg.pin_d14 = GPIO_NUM_1;  // R3
      cfg.pin_d15 = GPIO_NUM_0;  // R4

      cfg.pin_henable = GPIO_NUM_18;
      cfg.pin_vsync   = GPIO_NUM_17;
      cfg.pin_hsync   = GPIO_NUM_16;
      cfg.pin_pclk    = GPIO_NUM_21;
      cfg.freq_write  = 16000000;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 10;
      cfg.hsync_pulse_width = 8;
      cfg.hsync_back_porch  = 50;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 10;
      cfg.vsync_pulse_width = 8;
      cfg.vsync_back_porch  = 20;
      cfg.pclk_idle_high    = 0;
      cfg.de_idle_high      = 1;
      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);

    {
      auto cfg = _touch_instance.config();
      cfg.x_min      = 0;
      cfg.x_max      = 480;
      cfg.y_min      = 0;
      cfg.y_max      = 480;
      cfg.bus_shared = true;

      cfg.i2c_port   = I2C_NUM_0;
      cfg.i2c_addr   = 0x48;

      cfg.pin_int    = GPIO_NUM_42;
      cfg.pin_sda    = GPIO_NUM_39;
      cfg.pin_scl    = GPIO_NUM_40;
      cfg.pin_rst    = GPIO_NUM_NC;

      cfg.freq       = 400000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = GPIO_NUM_45;
      cfg.pwm_channel = 0;
      cfg.invert = false;
      _light_instance.config(cfg);
    }
      _panel_instance.light(&_light_instance);

    setPanel(&_panel_instance);
  }
};
