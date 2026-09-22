This page documents the primary serial interface accessible via an unpopulated 4-pin header, which I have named **J1**. This interface provides a one-way stream of crucial boot messages and system logs.

## Connection Parameters

*   **Header:** J1
*   **Baud Rate:** 115200
*   **Data Bits:** 8
*   **Parity:** None
*   **Stop Bits:** 1
*   **Voltage Level:** 3.3V

## Pinout

The 4-pin header `J1` is located on the mainboard but not marked. The pinout is as follows, viewed from the top of the PCB:

```
           Front
     ┌───────┬───────┐
     │ 3.3V  │  TX   │  (Top Row)
CPU  ├───────┼───────┤
     │  GND  │  RX   │  (Bottom Row)
     └───────┴───────┘
            Back
```

*   `GND`: Ground
*   `3.3V`: Power supply (do not connect unless you are sure your adapter needs it)
*   `TX` (Transmit): Data output *from* the TechniSat receiver. Connect this to the `RX` pin of your USB-to-Serial adapter.
*   `RX` (Receive): Data input *to* the TechniSat receiver. Connect this to the `TX` pin of your adapter.

---

## Full Bootlog Dump

The complete UART log from a cold boot can be found here:

➡️ **[View full boot log](https://github.com/MaizeShark/TechniSat-S1/blob/main/captures/uart/j1-boot.log)**

Sadly it isn't much and you cant send anything.