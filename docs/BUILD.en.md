# Build and configuration

## Requirements

- VS Code + PlatformIO or PlatformIO Core.
- PlatformIO-managed ESP8266 toolchain.
- USB data cable and any required HW-364A USB driver.

## Build / upload

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

Default environment: `hw364a_diag_web`.

## Wi-Fi without repository secrets

No real Wi-Fi credentials are stored in the repository. Recommended: leave compile-time values empty and use the captive portal, which stores configuration in LittleFS. Optional fallback: create a local `include/secrets.h` defining `WIFI_SSID` and `WIFI_PASSWORD`. The file is ignored by Git.

## PlatformIO defaults

- `DCF_DATA_PIN=13`
- `DCF_PON_PIN=5`
- `DCF_PON_ACTIVE_LOW=1`
- `DCF_IDLE_LOW=1`
- `OLED_ENABLED=1`
- `OLED_SDA_PIN=14`
- `OLED_SCL_PIN=12`
- `OLED_I2C_ADDR=0x3C`

Runtime OLED probing also tests reversed SDA/SCL and address 0x3D.

## First boot

1. Wire the RC8000.
2. Power the board.
3. The OLED should stay dark while searching — this is intentional.
4. Configure Wi-Fi if needed.
5. Wait for 1 Hz lock and valid frames.
6. After N valid syncs the receiver powers down and the OLED shows holdover time.
