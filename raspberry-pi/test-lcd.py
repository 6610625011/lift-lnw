#!/usr/bin/env python3
"""
Test script for 16x2 I2C Character LCD on Raspberry Pi
Standard I2C Pins:
  SDA: Pin 3 (GPIO 2)
  SCL: Pin 5 (GPIO 3)
"""

import time
import sys

try:
    import smbus2 as smbus
except ImportError:
    try:
        import smbus
    except ImportError:
        print("Error: smbus module not found. Run: sudo apt-get install python3-smbus python3-smbus2")
        sys.exit(1)

# PCF8574 Pin mapping to HD44780
# Bit 0: RS, Bit 1: RW, Bit 2: EN, Bit 3: Backlight, Bits 4-7: Data (D4-D7)
LCD_BACKLIGHT = 0x08
ENABLE = 0b00000100

class I2cLcd:
    def __init__(self, bus_num=1, address=0x27):
        self.bus = smbus.SMBus(bus_num)
        self.address = address
        self.init_lcd()

    def write_byte(self, data):
        self.bus.write_byte(self.address, data | LCD_BACKLIGHT)

    def toggle_enable(self, data):
        time.sleep(0.0005)
        self.bus.write_byte(self.address, (data | ENABLE | LCD_BACKLIGHT))
        time.sleep(0.0005)
        self.bus.write_byte(self.address, (data & ~ENABLE | LCD_BACKLIGHT))
        time.sleep(0.0005)

    def send_nibble(self, nibble, mode):
        # mode: 0 = command, 1 = char
        byte = (nibble << 4) | mode
        self.toggle_enable(byte)

    def send(self, value, mode):
        self.send_nibble(value >> 4, mode)
        self.send_nibble(value & 0x0F, mode)

    def command(self, cmd):
        self.send(cmd, 0)

    def write_char(self, char_val):
        self.send(char_val, 1)

    def init_lcd(self):
        time.sleep(0.05)
        self.send_nibble(0x03, 0)
        time.sleep(0.005)
        self.send_nibble(0x03, 0)
        time.sleep(0.001)
        self.send_nibble(0x03, 0)
        self.send_nibble(0x02, 0)  # 4-bit mode

        self.command(0x28)  # 2 lines, 5x8
        self.command(0x0C)  # Display on, cursor off
        self.command(0x06)  # Increment cursor
        self.command(0x01)  # Clear
        time.sleep(0.005)

    def clear(self):
        self.command(0x01)
        time.sleep(0.002)

    def set_cursor(self, row, col):
        row_offsets = [0x00, 0x40]
        self.command(0x80 | (col + row_offsets[row]))

    def write_string(self, text, row=0, col=0):
        self.set_cursor(row, col)
        for char in text[:16]:
            self.write_char(ord(char))

def find_lcd_address(bus_num=1):
    bus = smbus.SMBus(bus_num)
    for addr in [0x27, 0x3F]:
        try:
            bus.write_quick(addr)
            return addr
        except Exception:
            pass
    return None

def main():
    print("=" * 50)
    print("16x2 I2C LCD Hardware Test on Raspberry Pi")
    print("=" * 50)

    addr = find_lcd_address(1)
    if not addr:
        print("Warning: Could not auto-detect LCD at 0x27 or 0x3F.")
        print("Defaulting to 0x27. If nothing shows, run: i2cdetect -y 1")
        addr = 0x27
    else:
        print(f"LCD detected at I2C address: 0x{addr:02X}")

    try:
        lcd = I2cLcd(bus_num=1, address=addr)
        lcd.clear()
        lcd.write_string("CN360 HALL UNIT", row=0, col=0)
        lcd.write_string("LCD TEST OK! :)", row=1, col=0)
        print("Displayed test message on LCD!")
        print("Adjust the contrast screw (blue potentiometer) on the back of the LCD if faint.")
    except Exception as e:
        print(f"Error accessing LCD: {e}")

if __name__ == "__main__":
    main()

