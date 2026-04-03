# MC3416 Basic Example

This example demonstrates basic usage of the MC3416 3-axis accelerometer driver:

- I2C bus initialization
- Chip ID verification
- Range and sample rate configuration
- Continuous XYZ acceleration polling via a FreeRTOS task

## Wiring

| MC3416 Pin | ESP32 Pin |
|------------|-----------|
| SDA        | GPIO 7    |
| SCL        | GPIO 8    |
| VDD/VDDIO  | 3.3V      |
| GND        | GND       |
| VPP        | GND       |

## Expected Output

```
I (xxx) MC3416 example: Chip ID: 0xa0
I (xxx) MC3416 example: Chip ID is correct
I (xxx) MC3416 example: Range: 0x09
I (xxx) MC3416 example: Sample rate: 0x00
I (xxx) MC3416 example: Mode: WAKE
I (xxx) MC3416 example: x=0, y=0, z=16384
```
