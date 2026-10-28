"""Record the E72 debug UART (115200 8N1) and survive USB unplug/replug.

Usage: python3 uart_log.py [port] [seconds]
Lines are printed with a host timestamp; the port is reopened as soon as it comes back.
DTR/RTS are kept deasserted so opening the port does not disturb the stick.
"""
import sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-2130"
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 600
end = time.time() + duration
s = None
line = b""

def out(text):
    sys.stdout.write(time.strftime("%H:%M:%S ") + text + "\n")
    sys.stdout.flush()

while time.time() < end:
    if s is None:
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port, 115200, 0.5
            s.dtr = s.rts = False
            s.open()
            out(f"[host] {port} opened")
        except (serial.SerialException, OSError):
            s = None
            time.sleep(0.2)
            continue
    try:
        data = s.read(256)
    except (serial.SerialException, OSError):
        out("[host] port lost (unplugged?)")
        s = None
        continue
    for b in data:
        if b in (0x0A, 0x0D):
            if line:
                out(line.decode(errors="replace"))
                line = b""
        else:
            line += bytes([b])
