# MC3416 AnyMotion Interrupt Example

This example demonstrates how to configure and use the AnyMotion interrupt feature of the MC3416 accelerometer.

The INTN pin of the MC3416 is connected to a GPIO on the ESP32. When motion exceeding the configured threshold is detected, the MC3416 asserts INTN, triggering an interrupt on the ESP32.

## Wiring

| MC3416 Pin | ESP32 Pin     |
|------------|---------------|
| SDA        | GPIO 7        |
| SCL        | GPIO 8        |
| INTN       | GPIO 4        |
| VDD/VDDIO  | 3.3V          |
| GND        | GND           |
| VPP        | GND           |

> **Note:** The INTN pin is configured as active-low open-drain by default. Add a 4.7 kΩ pull-up resistor to VDD/VDDIO, or configure the driver for push-pull output by enabling `IPP` in `mc3416_set_mode_config()`.

## Expected Output

```
I (xxx) MC3416 anymotion: Chip ID: 0xa0
I (xxx) MC3416 anymotion: Chip ID is correct
I (xxx) MC3416 anymotion: AnyMotion threshold: 400
I (xxx) MC3416 anymotion: Interrupt ctrl register: 0x04
I (xxx) MC3416 anymotion: Motion ctrl register: 0x04
I (xxx) MC3416 anymotion: Mode: WAKE
I (xxx) MC3416 anymotion: [INTN] Interrupt on GPIO[4] detected
I (xxx) MC3416 anymotion: Interrupt status: 0x04 (ANYM_INT)
```
