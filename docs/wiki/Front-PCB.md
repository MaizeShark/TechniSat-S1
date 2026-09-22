> [!NOTE]
> **Hardware Scope Limitation**
>
> The information and pinouts documented here are exclusively for the **`TechniSat Digit Isio S`**.
>
> This does **not** apply to the `Digicorder Isio S` model, which is a physically wider unit with more buttons and is presumed to use a different PCB layout. A `Digicorder` unit is not available to me for analysis.

## Front PCB Analysis: D-Pad & VFD Assembly

This document outlines the pin connections for the front PCB based on hardware observation.

### Key Components

*   **Microcontroller (MCU) & VFD Driver:** `TMP87CH75FG-6CP9`
*   **MCU-SOC communication**: the TMP87CH75FG communicates as an I²C slave with the address ``0x70``
*   **Voltages**: Logic Level (at least I²C) = 3V3, VDD = 5V
*   **VFD (Vacuum Fluorescent Display):** 16x01 (16 characters, 1 line) 5x7 matrix

### FFC to MCU Pin Mapping

The following table details the primary connections from the FFC connector to the microcontroller.

| Pin Name | MCU Pin | FPC Pin | I/O | Implemented Function | Datasheet Alternate Functions |
| :--- | :---: | :---: | :---: | :--- | :--- |
| **`P31`** | 28 | 14 | I/O | **`SDA`** (I²C Serial Data) | `SO0` |
| **`P30`** | 27 | 12 | I/O | **`SCL`** (I²C Serial Clock) | `SI0` |
| **`P22`** | 15 | 5 | I/O | General-Purpose I/O | `XOUT` (Oscillator Out) |
| **`P21`** | 16 | 3 | I/O | General-Purpose I/O | `XTIN` (Oscillator In) |
| **`P20`** | 18 | 2 | I/O | General-Purpose I/O | `INT5`, `STOP` Release |

The ``General-Purpose I/O`` (P20-P22) are probably all some kind of interrupt.

### FFC Pinout (Front PCB)

A pinout of the 16-pin FFC connector from the Front PCB side.

| Pin | Signal | Description |
| :--: | :--- | :--- |
| 1 | `GND` | Ground |
| 2 | `P20` | MCU Port 20 (General I/O) |
| 3 | `P21` | MCU Port 21 (General I/O) | 5k Pull up |
| 4 | `GND` | Ground |
| 5 | `P22` | MCU Port 22 (General I/O) |
| 6 | `NC` | No Connection |
| 7 | `VDD` | Power Supply |
| 8 | `VDD` | Power Supply |
| 9 | `VDD` | Power Supply |
| 10 | `NC` | Not Connected | Going to DNP resistor |
| 11 | `GND` | Ground |
| 12 | `SCL` | I²C Clock (to P30) | 33R series |
| 13 | `GND` | Ground |
| 14 | `SDA` | I²C Data (to P31) | 33R series |
| 15 | `NC` | No Connection |
| 16 | `NC` | No Connection |


### Complete FFC Pinout (Mainboard)

A full pinout of the 16-pin FFC connector from the Mainboard side.

| Pin | Signal | Description |
| :--: | :--- | :--- |
| 1 | `GND` | Ground |
| 2 | `` |  (Pulled Up) |
| 3 | `` |  |
| 4 | `GND` | Ground |
| 5 | `` |  |
| 6 | `` |  |
| 7 | `VDD` | Power Supply |
| 8 | `VDD` | Power Supply |
| 9 | `VDD` | Power Supply |
| 10 | `` |  |
| 11 | `GND` | Ground |
| 12 | `` | SCL (pp) |
| 13 | `GND` | Ground |
| 14 | `` | SDA (pp) |
| 15 | `NC` | No Connection |
| 16 | `NC` | No Connection |