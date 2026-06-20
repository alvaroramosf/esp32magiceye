#!/usr/bin/env python3
"""
ESP32 Magic Eye - Host Audio VU Meter
====================================
Captures system audio (or microphone input) and streams the volume envelope
to the ESP32 over UDP as a single byte (0-255).

Requirements:
    pip install sounddevice numpy

Usage:
    # Auto-detects system audio monitor and streams to ESP32:
    python audio_monitor.py --host 192.168.1.135 --verbose

    # List all audio devices:
    python audio_monitor.py --list
"""

import argparse
import queue
import socket
import sys
import numpy as np

try:
    import sounddevice as sd
except ImportError:
    sys.exit(
        "ERROR: sounddevice is not installed.\n"
        "Install it with:  pip install sounddevice numpy\n"
        "You might also need to install portaudio headers on Linux:\n"
        "  sudo apt install libportaudio2"
    )

# Constants
DEFAULT_PORT = 4210
DEFAULT_FPS = 40       # packets per second (25ms interval)
SAMPLE_RATE = 44100    # audio sampling rate

# Thread-safe queue to pass audio blocks from the callback to the main thread
audio_queue = queue.Queue()


def audio_callback(indata, frames, time_info, status):
    """This is called by sounddevice for each audio block."""
    if status:
        print(status, file=sys.stderr)
    # Put a copy of the input data into the queue
    audio_queue.put(indata.copy())


def find_monitor_device():
    """Scan available audio input devices and try to auto-detect a system monitor loopback."""
    devices = sd.query_devices()
    
    # 1. First priority: look for a monitor loopback device (PulseAudio/PipeWire monitor)
    for idx, dev in enumerate(devices):
        if dev['max_input_channels'] > 0:
            name = dev['name'].lower()
            # Common names for desktop loopback / monitors in Linux
            if 'monitor' in name or 'loopback' in name:
                return idx
                
    # 2. Second priority: look for default input device
    default_input = sd.default.device[0]
    if default_input >= 0:
        return default_input
        
    # 3. Fallback to the first device with inputs
    for idx, dev in enumerate(devices):
        if dev['max_input_channels'] > 0:
            return idx
            
    return None


def run(host: str, port: int, device_idx: int, gain: float, fps: int, use_log: bool, min_val: int, db_min: float, db_max: float, auto_range: bool, verbose: bool):
    # Initialize UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (host, port)

    # Determine audio block size based on FPS
    block_size = int(SAMPLE_RATE / fps)

    print(f"Streaming Audio VU -> {host}:{port}  ({fps} packets/sec)")
    print(f"Using Audio Device Index: {device_idx} - {sd.query_devices(device_idx)['name']}")
    print(f"Settings: Gain={gain:.1f} | Scaling={'Logarithmic (dB)' if use_log else 'Linear'} | Offset={min_val}")
    if use_log:
        print(f"Limits: Min={db_min:.1f} dB | Max={db_max:.1f} dB | AutoRange={'ON' if auto_range else 'OFF'}")
    print("Press Ctrl+C to stop.\n")

    # Envelope filter parameters (attack/decay)
    attack_coeff = 0.6   # weight of new peak
    decay_coeff = 0.08   # weight of new peak when decaying (slower transition)
    smoothed_volume = 0.0

    # Auto-range dynamic thresholds
    running_db_min = db_min
    running_db_max = db_max

    try:
        # Start input stream
        with sd.InputStream(
            device=device_idx,
            channels=1,
            samplerate=SAMPLE_RATE,
            blocksize=block_size,
            callback=audio_callback
        ):
            while True:
                # Get block from queue (blocks if empty)
                block = audio_queue.get()
                
                # Remove DC offset (subtract the mean)
                block_ac = block - np.mean(block)
                
                # Calculate RMS (Root Mean Square) of the AC component
                rms = np.sqrt(np.mean(block_ac**2))
                
                # Apply digital gain
                amplified = rms * gain
                
                # Apply envelope filter (fast attack, slow decay)
                if amplified > smoothed_volume:
                    smoothed_volume = attack_coeff * amplified + (1.0 - attack_coeff) * smoothed_volume
                else:
                    smoothed_volume = decay_coeff * amplified + (1.0 - decay_coeff) * smoothed_volume

                # Map smoothed volume to a normalized 0.0 - 1.0 range
                if use_log:
                    db = 20 * np.log10(max(smoothed_volume, 1e-5))
                    silence_threshold = -45.0  # dB threshold below which we freeze adaptation
                    
                    if auto_range:
                        # Only adapt thresholds if we are not in silence (music is playing)
                        if db > silence_threshold:
                            # Adapt max threshold to audio peaks
                            if db > running_db_max:
                                running_db_max = db
                            else:
                                # Decay slowly back down (time constant ~10 seconds at 40 FPS)
                                running_db_max = 0.0025 * db + 0.9975 * running_db_max
                                
                            # Adapt min threshold to audio valleys
                            if db < running_db_min:
                                running_db_min = db
                            else:
                                # Decay slowly back up
                                running_db_min = 0.0025 * db + 0.9975 * running_db_min
                                
                            # Force a minimum range of 15 dB so it doesn't amplify pure noise
                            running_db_max = max(running_db_max, running_db_min + 15.0)
                        
                        # Normalize using dynamic range
                        norm = (db - running_db_min) / (running_db_max - running_db_min)
                    else:
                        # Normalize using static range
                        norm = (db - db_min) / (db_max - db_min)
                else:
                    # Linear mapping (peak scale of 0.3)
                    norm = smoothed_volume / 0.3

                norm = max(0.0, min(1.0, norm))

                # Map to output range [min_val, 255] to overcome the hardware diode forward voltage drop.
                # If we are in silence (paused), force value to 0 to keep the eye wide open.
                if use_log and db <= silence_threshold:
                    val = 0
                elif norm < 0.01:
                    val = 0
                else:
                    val = int(min_val + (255 - min_val) * norm)

                # Clamp between 0 and 255
                val = max(0, min(255, val))

                # Send packet
                sock.sendto(bytes([val]), target)

                if verbose:
                    bar_len = round(val / 12.75)  # 20-char bar
                    bar = "=" * bar_len + " " * (20 - bar_len)
                    if use_log and auto_range:
                        print(f"\rVolume: {val:3d} [{bar}] | Range: {running_db_min:.1f} to {running_db_max:.1f} dB", end="", flush=True)
                    else:
                        print(f"\rVolume: {val:3d} [{bar}]", end="", flush=True)

    except KeyboardInterrupt:
        print("\n\nStopped by user.")
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(
        description="Stream desktop audio volume (VU meter) to ESP32 Magic Eye over UDP.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--host", "-H",
        default="192.168.1.135",
        metavar="IP",
        help="ESP32 IP address",
    )
    parser.add_argument(
        "--port", "-p",
        type=int,
        default=DEFAULT_PORT,
        metavar="PORT",
        help="UDP port (must match UDP_PORT in firmware)",
    )
    parser.add_argument(
        "--device", "-d",
        type=int,
        default=-1,
        metavar="INDEX",
        help="Audio device index (use --list to see all)",
    )
    parser.add_argument(
        "--gain", "-g",
        type=float,
        default=1.0,
        metavar="FACTOR",
        help="Audio software gain multiplier",
    )
    parser.add_argument(
        "--fps", "-f",
        type=int,
        default=DEFAULT_FPS,
        metavar="HZ",
        help="Packets per second / frame rate",
    )
    parser.add_argument(
        "--linear",
        action="store_true",
        help="Use linear scaling instead of logarithmic (dB) scaling",
    )
    parser.add_argument(
        "--db-min",
        type=float,
        default=-35.0,
        metavar="DB",
        help="Decibel floor for zero volume (when auto-range is OFF)",
    )
    parser.add_argument(
        "--db-max",
        type=float,
        default=-10.0,
        metavar="DB",
        help="Decibel ceiling for maximum volume (when auto-range is OFF)",
    )
    parser.add_argument(
        "--no-auto",
        action="store_true",
        help="Disable automatic gain/range tracking (AGC)",
    )
    parser.add_argument(
        "--min-val", "-m",
        type=int,
        default=90,
        metavar="VALUE",
        help="Minimum active output value (to overcome diode forward drop, e.g. 80-120)",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Print a live volume bar in the terminal",
    )
    parser.add_argument(
        "--list", "-l",
        action="store_true",
        help="List all audio devices and exit",
    )

    args = parser.parse_args()

    if args.list:
        print("Available Audio Devices:")
        print(sd.query_devices())
        print("\nNote: On Linux (PulseAudio/PipeWire), select the 'monitor' device of your output card")
        print("to capture system audio. You can use 'pavucontrol' to route it dynamically.")
        sys.exit(0)

    device_idx = args.device
    if device_idx < 0:
        device_idx = find_monitor_device()
        if device_idx is None:
            sys.exit("ERROR: No suitable audio input devices found.")

    run(
        host=args.host,
        port=args.port,
        device_idx=device_idx,
        gain=args.gain,
        fps=args.fps,
        use_log=not args.linear,
        min_val=args.min_val,
        db_min=args.db_min,
        db_max=args.db_max,
        auto_range=not args.no_auto,
        verbose=args.verbose
    )


if __name__ == "__main__":
    main()
