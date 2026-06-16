#!/usr/bin/env python3
"""
ESP32 Magic Eye - Host CPU Monitor
==================================
Samples system CPU load using psutil and sends it to the ESP32 over UDP as a
single byte (0-255).  Run with --help to see all options.

Requirements:
    pip install psutil          # or: pip install -r requirements.txt

Usage examples:
    python monitor.py --host 192.168.1.42
    python monitor.py --host 192.168.1.42 --port 4210 --interval 0.1
    python monitor.py --host 192.168.1.42 --interval 0.2 --verbose
"""

import argparse
import signal
import socket
import sys
import time

try:
    import psutil
except ImportError:
    sys.exit(
        "ERROR: psutil is not installed.\n"
        "Install it with:  pip install psutil\n"
        "Or:               pip install -r requirements.txt"
    )


# Constants

DEFAULT_PORT     = 4210    # must match UDP_PORT in firmware
DEFAULT_INTERVAL = 0.1     # seconds between samples (10 Hz)
CPU_SAMPLE_INTERVAL = 0.05 # psutil interval for per-call CPU measurement


# Helpers

def cpu_to_byte(cpu_percent: float) -> int:
    """Map a CPU percentage (0.0-100.0) to a DAC value (0-255)."""
    clamped = max(0.0, min(100.0, cpu_percent))
    return round(clamped * 2.55)


def build_packet(value: int) -> bytes:
    """Pack a single byte as a UDP payload."""
    return bytes([value & 0xFF])


# Core loop

def run(host: str, port: int, interval: float, verbose: bool) -> None:
    """Open a UDP socket and stream CPU load to the ESP32 until interrupted."""

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (host, port)

    print(f"Streaming CPU load -> {host}:{port}  (every {interval*1000:.0f} ms)")
    print("Press Ctrl+C to stop.\n")

    # Warm up psutil so the first sample is not always 0 %
    psutil.cpu_percent(interval=None)
    time.sleep(0.1)

    try:
        while True:
            loop_start = time.monotonic()

            cpu = psutil.cpu_percent(interval=CPU_SAMPLE_INTERVAL)
            value = cpu_to_byte(cpu)
            packet = build_packet(value)

            sock.sendto(packet, target)

            if verbose:
                bar_len = round(cpu / 5)        # 20-char bar
                bar = "#" * bar_len + "-" * (20 - bar_len)
                print(f"\rCPU {cpu:5.1f}%  [{bar}]  -> byte {value:3d}", end="", flush=True)

            # Sleep for the remainder of the interval
            elapsed = time.monotonic() - loop_start
            sleep_time = interval - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    except KeyboardInterrupt:
        print("\n\nStopped by user.")
    finally:
        sock.close()


# Entry point

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stream CPU load to ESP32 Magic Eye over UDP.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--host", "-H",
        required=True,
        metavar="IP",
        help="ESP32 IP address (shown in serial monitor after boot)",
    )
    parser.add_argument(
        "--port", "-p",
        type=int,
        default=DEFAULT_PORT,
        metavar="PORT",
        help="UDP port (must match UDP_PORT in firmware)",
    )
    parser.add_argument(
        "--interval", "-i",
        type=float,
        default=DEFAULT_INTERVAL,
        metavar="SECONDS",
        help="Time between samples in seconds",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Print a live CPU bar in the terminal",
    )
    return parser.parse_args()


def main() -> None:
    # Graceful exit on SIGTERM (e.g. systemd stop)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    args = parse_args()
    run(
        host=args.host,
        port=args.port,
        interval=args.interval,
        verbose=args.verbose,
    )


if __name__ == "__main__":
    main()
