#!/usr/bin/env python3
"""
Test script for 4x4 Matrix Keypad on Raspberry Pi using modern gpiod (libgpiod)
Wiring:
  ROW_PINS = [13, 6, 5, 22]  (R1 -> GPIO13, R2 -> GPIO6, R3 -> GPIO5, R4 -> GPIO22)
  COL_PINS = [17, 26, 24, 23] (C1 -> GPIO17, C2 -> GPIO26, C3 -> GPIO24, C4 -> GPIO23)
"""

import time
import sys

ROW_PINS = [13, 6, 5, 22]
COL_PINS = [17, 26, 24, 23]

KEYMAP = [
    ['1', '2', '3', 'A'],
    ['4', '5', '6', 'B'],
    ['7', '8', '9', 'C'],
    ['*', '0', '#', 'D']
]

class KeypadGpiod:
    def __init__(self, rows=ROW_PINS, cols=COL_PINS):
        self.rows = rows
        self.cols = cols
        self.backend = None
        self.setup()

    def setup(self):
        # 1. Try gpiod v1 (Standard python3-libgpiod on Debian Bookworm / Raspberry Pi OS)
        try:
            import gpiod
            if hasattr(gpiod, 'Chip'):
                chip_found = None
                # Auto-detect chip (Pi 5 uses gpiochip4; Pi 4/3 use gpiochip0)
                for c in ['gpiochip4', 'gpiochip0', 4, 0]:
                    try:
                        cand = gpiod.Chip(f"gpiochip{c}" if isinstance(c, int) else c)
                        if cand.num_lines() > max(self.rows + self.cols):
                            chip_found = cand
                            print(f"[gpiod] Using chip: {cand.name()} ({cand.label()})")
                            break
                    except Exception:
                        continue

                if not chip_found:
                    # Fallback scan all chips 0..9
                    for i in range(10):
                        try:
                            cand = gpiod.Chip(f"gpiochip{i}")
                            if cand.num_lines() > max(self.rows + self.cols):
                                chip_found = cand
                                print(f"[gpiod] Using chip: {cand.name()}")
                                break
                        except Exception:
                            continue

                if chip_found:
                    self.chip = chip_found
                    # Request row lines as output (default HIGH)
                    self.row_lines = self.chip.get_lines(self.rows)
                    req_row = gpiod.line_request()
                    req_row.consumer = "keypad_rows"
                    req_row.request_type = gpiod.line_request.DIRECTION_OUTPUT
                    self.row_lines.request(req_row, default_values=[1, 1, 1, 1])

                    # Request col lines as input with pull-up
                    self.col_lines = self.chip.get_lines(self.cols)
                    req_col = gpiod.line_request()
                    req_col.consumer = "keypad_cols"
                    req_col.request_type = gpiod.line_request.DIRECTION_INPUT
                    if hasattr(gpiod.line_request, 'FLAG_BIAS_PULL_UP'):
                        req_col.flags = gpiod.line_request.FLAG_BIAS_PULL_UP
                    self.col_lines.request(req_col)

                    self.backend = "gpiod_v1"
                    return
        except Exception as e:
            print(f"[gpiod v1] Note: {e}")

        # 2. Try gpiod v2 (Modern pip gpiod >= 2.0.0)
        try:
            import gpiod
            from gpiod.line import Direction, Value, Bias
            if hasattr(gpiod, 'request_lines'):
                chip_path = None
                for c in ['/dev/gpiochip4', '/dev/gpiochip0']:
                    import os
                    if os.path.exists(c):
                        chip_path = c
                        break
                if not chip_path:
                    chip_path = '/dev/gpiochip0'

                settings = {}
                for r in self.rows:
                    settings[r] = gpiod.LineSettings(direction=Direction.OUTPUT, output_value=Value.ACTIVE)
                for c in self.cols:
                    settings[c] = gpiod.LineSettings(direction=Direction.INPUT, bias=Bias.PULL_UP)

                self.line_request = gpiod.request_lines(chip_path, consumer="keypad", config=settings)
                self.backend = "gpiod_v2"
                print(f"[gpiod v2] Successfully configured on {chip_path}")
                return
        except Exception as e:
            print(f"[gpiod v2] Note: {e}")

        # 3. Fallback to gpiozero (Built into Bookworm using lgpio/gpiod)
        try:
            from gpiozero import DigitalOutputDevice, Button
            self.row_devs = [DigitalOutputDevice(pin, active_high=True, initial_value=True) for pin in self.rows]
            self.col_devs = [Button(pin, pull_up=True) for pin in self.cols]
            self.backend = "gpiozero"
            print("[Keypad] Using gpiozero backend (backed by gpiod/lgpio)")
            return
        except Exception as e:
            print(f"[gpiozero] Note: {e}")

        print("\nERROR: No suitable GPIO library found!")
        print("Please install gpiod on Raspberry Pi with:")
        print("  sudo apt-get install -y python3-gpiod python3-libgpiod python3-gpiozero")
        sys.exit(1)

    def scan(self):
        if self.backend == "gpiod_v1":
            for r_idx in range(4):
                vals = [1, 1, 1, 1]
                vals[r_idx] = 0
                self.row_lines.set_values(vals)
                time.sleep(0.0005)

                col_vals = self.col_lines.get_values()
                for c_idx, val in enumerate(col_vals):
                    if val == 0:  # Pressed (LOW)
                        self.row_lines.set_values([1, 1, 1, 1])
                        return KEYMAP[r_idx][c_idx], r_idx, c_idx

            self.row_lines.set_values([1, 1, 1, 1])
            return None, None, None

        elif self.backend == "gpiod_v2":
            from gpiod.line import Value
            for r_idx, r_pin in enumerate(self.rows):
                for i, p in enumerate(self.rows):
                    self.line_request.set_value(p, Value.INACTIVE if i == r_idx else Value.ACTIVE)
                time.sleep(0.0005)

                for c_idx, c_pin in enumerate(self.cols):
                    if self.line_request.get_value(c_pin) == Value.INACTIVE:
                        for p in self.rows:
                            self.line_request.set_value(p, Value.ACTIVE)
                        return KEYMAP[r_idx][c_idx], r_idx, c_idx

            for p in self.rows:
                self.line_request.set_value(p, Value.ACTIVE)
            return None, None, None

        elif self.backend == "gpiozero":
            for r_idx, r_dev in enumerate(self.row_devs):
                for i, dev in enumerate(self.row_devs):
                    if i == r_idx:
                        dev.off()  # Pull LOW
                    else:
                        dev.on()   # Keep HIGH
                time.sleep(0.0005)

                for c_idx, c_dev in enumerate(self.col_devs):
                    if c_dev.is_pressed:
                        for dev in self.row_devs:
                            dev.on()
                        return KEYMAP[r_idx][c_idx], r_idx, c_idx

            for dev in self.row_devs:
                dev.on()
            return None, None, None

        return None, None, None

    def cleanup(self):
        try:
            if self.backend == "gpiod_v1":
                self.row_lines.release()
                self.col_lines.release()
                self.chip.close()
            elif self.backend == "gpiod_v2":
                self.line_request.release()
            elif self.backend == "gpiozero":
                for d in self.row_devs + self.col_devs:
                    d.close()
        except Exception:
            pass

def main():
    print("=" * 55)
    print("4x4 Numpad Test on Raspberry Pi (gpiod)")
    print(f"Row GPIOs (R1-R4): {ROW_PINS}")
    print(f"Col GPIOs (C1-C4): {COL_PINS}")
    print("Press any key on the keypad (Ctrl+C to stop)...")
    print("=" * 55)

    keypad = KeypadGpiod()
    last_key = None

    try:
        while True:
            key, r, c = keypad.scan()
            if key is not None and key != last_key:
                print(f"[PRESS] Key: '{key}' (Row {r+1} [GPIO{ROW_PINS[r]}], Col {c+1} [GPIO{COL_PINS[c]}])")
                last_key = key
                time.sleep(0.15)  # Debounce
            elif key is None:
                last_key = None
            time.sleep(0.02)
    except KeyboardInterrupt:
        print("\nTest stopped.")
    finally:
        keypad.cleanup()

if __name__ == "__main__":
    main()
