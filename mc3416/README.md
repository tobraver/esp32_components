# I2C Driver Component for the MC3416 3-Axis Accelerometer

## The MC3416

The MC3416 is a small form factor, integrated digital output 3-axis accelerometer with a feature set optimized for cell phones and consumer product motion sensing. Applications include user interface control, gaming motion input, electronic compass tilt compensation, game controllers, remote controls and portable media products.

The MC3416 features a dedicated motion block which implements algorithms to support "any motion" and shake detection, tilt/flip and tilt-35 position detection. Internal sample rate can be set from 128 to 1024 samples/second.

> This driver targets the I2C interface only.

## Features

- Set operational mode (STANDBY / WAKE)
- Set output data rate (128 / 256 / 512 / 1024 Hz)
- Set acceleration range (±2g / ±4g / ±8g / ±12g / ±16g)
- Read 16-bit XYZ acceleration data
- Configure interrupt pin polarity and drive mode (IAH / IPP)
- Configure I2C watchdog timer
- Configure motion detection: AnyMotion, Shake, Tilt/Flip, Tilt-35
- Read and clear interrupt status

## Key Differences from MC3479

| Feature              | MC3416         | MC3479         |
|----------------------|----------------|----------------|
| Chip ID              | 0xA0           | 0xA4           |
| Operational states   | STANDBY / WAKE | SLEEP / CWAKE / STANDBY |
| Output data rate     | 128–1024 Hz    | 50–2000 Hz     |
| FIFO                 | No             | Yes            |
| GPIO control register| No (IAH/IPP in MODE reg) | Yes (0x33) |
| I2C watchdog         | Yes (in MODE reg) | No          |

## Directory Structure

```
mc3416/
├── include/
│   └── mc3416.h          # Public API header
├── examples/
│   ├── example_mc3416_basic/        # Basic XYZ polling example
│   └── example_mc3416_anymotion/    # AnyMotion interrupt example
├── mc3416.c              # Driver implementation
├── CMakeLists.txt
├── idf_component.yml
├── Kconfig
├── CHANGELOG.md
└── README.md
```

## Get Started

Copy the `mc3416` component into your project's `components/` directory, then add it as a dependency in your `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES mc3416
)
```

## Basic Usage

```c
#include "mc3416.h"

// Initialize I2C bus first (i2c_param_config + i2c_driver_install)

// Create sensor handle
mc3416_handle_t sensor = mc3416_create(I2C_NUM_0, MC3416_I2C_ADDR_0);

// Verify chip ID
uint8_t chip_id;
mc3416_get_chip_id(sensor, &chip_id);
// chip_id should be MC3416_DEFAULT_CHIP_ID (0xA0)

// Configure in STANDBY state
mc3416_set_mode(sensor, MC3416_MODE_STANDBY);
mc3416_set_range(sensor, MC3416_RANGE_2G);
mc3416_set_sample_rate(sensor, MC3416_SAMPLE_128Hz);

// Start continuous sampling
mc3416_set_mode(sensor, MC3416_MODE_WAKE);

// Read acceleration
int16_t x, y, z;
mc3416_get_acceleration(sensor, &x, &y, &z);

// Cleanup
mc3416_delete(sensor);
```

## Resources

- [MC3416 Datasheet (APS-045-0020 v2.2)](https://www.memsic.com)
