# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-04-03

### Added
- MC3416 3-axis accelerometer driver for ESP-IDF 5.x
- I2C read/write support (up to 1 MHz)
- STANDBY / WAKE operational mode control with read-modify-write safety
- Full mode register configuration (IAH, IPP, I2C watchdog, state)
- Acceleration range configuration (±2g / ±4g / ±8g / ±12g / ±16g)
- Output data rate configuration (128 / 256 / 512 / 1024 Hz)
- Raw 16-bit XYZ acceleration data reading
- Motion detection: AnyMotion, Shake, Tilt/Flip, Tilt-35
- Interrupt enable register configuration
- Interrupt status register read and clear
- AnyMotion threshold and debounce configuration
- Tilt/Flip threshold and debounce configuration
- Shake threshold configuration
- Kconfig file for I2C pin and address configuration
- Basic and AnyMotion example applications
