# LIS2DH DK Test (MDBT50Q-DB-40)

- nRF Connect SDK 3.4.1 / board: `raytac_mdbt50q_db_40/nrf52840`
- LIS2DH (LIS2DH12 compatible): SCL=P0.08, SDA=P0.06, 3.3 V, GND.
- UART0 intentionally disabled because its default RX/TX pins conflict with I2C.
- I2C uses nRF TWIM0 at 100 kHz; auto-probes addresses 0x18 and 0x19.
- Sensor config: ODR 100 Hz, +/-2g, HR=1, BDU=1.
- USB CDC ACM virtual COM: output XYZ in mg each 100 ms; open Tera Term (DTR active).
- P0.13 green LED toggles every 500 ms, even without host terminal/sensor response.
- No `k_sleep()` or `k_busy_wait()`: Zephyr `k_timer` schedules `k_work`.

## Build

From an initialized NCS v3.4.1 shell, in this directory:

```sh
west build -b raytac_mdbt50q_db_40/nrf52840 -p always .
west flash
```

Make sure the USB cable is connected to the nRF52840 USB device port, and use
SWD/J-Link for flashing. The board's USB-to-TTL UART terminal is **not** the
same interface as the nRF52840's native USB CDC ACM virtual COM port.

## Testing

1. Connect LIS2DH module VCC=3.3 V, GND, SCL=P0.08, SDA=P0.06.
2. Keep the module's CS high for I2C mode, if a CS pin is exposed.
3. Ensure I2C pullups to 3.3 V (often 4.7k on a breakout, not always populated).
4. Flash, then open the USB virtual COM port in Tera Term (DTR asserted).
5. WHO_AM_I should read 0x33, and XYZ should change with orientation.
6. At rest, one axis should be roughly +/-1000 mg depending on orientation.

The sensor driver is portable; it does not include Zephyr headers or any USB/GPIO code.
For the Apslim PCB, adapt `app.overlay` and board adapter in `accel_app.c`.

## Host-side driver test

```sh
gcc -std=c11 -Wall -Wextra -Werror -Iinclude src/lis2dh.c tests/test_lis2dh.c -o /tmp/test_lis2dh
/tmp/test_lis2dh
```

## Correction: aliases not found

`accel_app.c` uses `DT_ALIAS(accel_i2c)` and `DT_ALIAS(status_led)`.
Both aliases are defined in `app.overlay`. The CMakeLists.txt in this
revision explicitly sets `DTC_OVERLAY_FILE` before `find_package(Zephyr ...)`.

When using the VS Code nRF Connect extension, make sure `app.overlay` is
in the project root (`DK_Board/app.overlay`), remove any conflicting
`DTC_OVERLAY_FILE` build setting, and do a **Pristine Build**.
In the configure output look for `Found devicetree overlay: .../DK_Board/app.overlay`.

Generated output `build/DK_Board/zephyr/zephyr.dts` (sysbuild) should have
`accel-i2c` and `status-led` aliases. If not, inspect the active overlay and
CMake cache. Do not treat an ordinary non-pristine rebuild as proof.


## 2026-10-16 — Fix missing Devicetree aliases

The application now directly selects `DT_NODELABEL(i2c0)` and `DT_NODELABEL(led0)`.
Both node labels are defined by the nRF52840 / Raytac board DTS; the LED maps to P0.13.
This avoids relying on app-specific `accel-i2c` or `status-led` aliases.
A compile-time check detects if `app.overlay` is not applied (USB CDC node absent,
or I2C0 left disabled).

**Important:** Copy the files *inside this `DK_Board` directory* to your
`dl-KM-Apslim/test/DK_Board` source directory. Do not extract the project into
an extra nested directory. In particular, replace `src/accel_app.c`.
Then delete the old `build` folder using Pristine Build or manually remove it.

Verify the CMake configure log says:
`Found devicetree overlay: .../test/DK_Board/app.overlay`

If this line does not appear, investigate the VS Code build configuration:
remove stale `DTC_OVERLAY_FILE` overrides and re-create the build configuration.
