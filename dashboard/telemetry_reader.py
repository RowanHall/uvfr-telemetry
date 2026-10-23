#!/usr/bin/env python3
"""
UVFR telemetry reader — the laptop side of the link (step C).

Reads the base-pico's USB serial output, parses the "DATA," CSV frames it
prints (see base-pico/basicRX.c), applies the scale factors from
protocol/telemetry.h to recover real engineering units, and forwards each
frame to a sink.

Two sinks:
  --stdout  : print parsed frames to the terminal (no dependencies beyond
              pyserial). Use this first to confirm the link end to end.
  (default) : write to InfluxDB 2.x for Grafana.

Examples:
  # just watch parsed frames, no database needed:
  python3 telemetry_reader.py --port /dev/cu.usbmodem1101 --stdout

  # stream into InfluxDB (token/org/bucket via flags or env vars):
  python3 telemetry_reader.py --port /dev/ttyACM0 \
      --influx-url http://localhost:8086 --influx-token $INFLUX_TOKEN \
      --influx-org uvfr --influx-bucket telemetry

CSV column order (must match the printf in base-pico/basicRX.c):
  DATA,seq,ms,packV,packI,soc,cellTmax,cellVmin,rpm,motT,invT,torque,thr,brk,speed,flags
"""

import argparse
import sys
import time

import serial  # pip install pyserial

# --- single source of truth for the frame layout -------------------------
# Mirrors the channel block in protocol/telemetry.h. Each entry:
#   (name, scale, unit)  ->  real_value = raw_int * scale
# Keep this in lockstep with telemetry.h; bump nothing here without bumping
# TELEM_VERSION there.
FIELDS = [
    ("pack_voltage",  0.1,   "V"),
    ("pack_current",  0.1,   "A"),
    ("soc",           1,     "%"),
    ("cell_temp_max", 0.1,   "degC"),
    ("cell_volt_min", 0.001, "V"),    # frame carries mV
    ("motor_rpm",     1,     "rpm"),
    ("motor_temp",    0.1,   "degC"),
    ("inverter_temp", 0.1,   "degC"),
    ("torque",        0.1,   "Nm"),
    ("throttle",      1,     "%"),
    ("brake",         1,     "%"),
    ("speed",         0.1,   "km/h"),
    ("flags",         1,     ""),      # bitfield, leave as int
]
# Columns before the scaled channels: DATA, seq, ms
N_COLUMNS = 2 + len(FIELDS)  # seq + ms + channels


def parse_line(line):
    """Parse one 'DATA,...' line into (seq, device_ms, {name: real_value}).

    Returns None for anything that isn't a well-formed DATA frame (heartbeats,
    debug chatter, truncated lines) so the caller can simply skip it.
    """
    line = line.strip()
    if not line.startswith("DATA,"):
        return None
    parts = line.split(",")[1:]  # drop the "DATA" tag
    if len(parts) != N_COLUMNS:
        return None
    try:
        ints = [int(p) for p in parts]
    except ValueError:
        return None

    seq, device_ms = ints[0], ints[1]
    values = {}
    for (name, scale, _unit), raw in zip(FIELDS, ints[2:]):
        # ints stay ints (scale == 1), scaled values become floats
        values[name] = raw * scale if scale != 1 else raw
    return seq, device_ms, values


def open_serial(port, baud):
    while True:
        try:
            s = serial.Serial(port, baud, timeout=1)
            print(f"[reader] opened {port} @ {baud}", file=sys.stderr)
            return s
        except serial.SerialException as e:
            print(f"[reader] waiting for {port}: {e}", file=sys.stderr)
            time.sleep(2)


class StdoutSink:
    """Prints parsed frames; zero external dependencies."""

    def write(self, seq, device_ms, values):
        pretty = "  ".join(f"{k}={v}" for k, v in values.items())
        print(f"seq={seq:<6} t={device_ms/1000:8.1f}s  {pretty}")


class InfluxSink:
    """Writes each frame as a point to InfluxDB 2.x."""

    def __init__(self, url, token, org, bucket):
        from influxdb_client import InfluxDBClient, Point  # pip install influxdb-client
        from influxdb_client.client.write_api import SYNCHRONOUS

        self._Point = Point
        self._client = InfluxDBClient(url=url, token=token, org=org)
        self._write = self._client.write_api(write_options=SYNCHRONOUS)
        self._bucket = bucket
        self._org = org
        print(f"[reader] writing to InfluxDB {url} org={org} bucket={bucket}",
              file=sys.stderr)

    def write(self, seq, device_ms, values):
        p = self._Point("telemetry").tag("source", "car").field("seq", seq) \
            .field("device_ms", device_ms)
        for name, val in values.items():
            p = p.field(name, val)
        self._write.write(bucket=self._bucket, org=self._org, record=p)


def main():
    ap = argparse.ArgumentParser(description="UVFR telemetry serial->InfluxDB reader")
    ap.add_argument("--port", required=True,
                    help="serial port (e.g. /dev/cu.usbmodem1101, /dev/ttyACM0)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--stdout", action="store_true",
                    help="print parsed frames instead of writing to InfluxDB")
    ap.add_argument("--influx-url", default="http://localhost:8086")
    ap.add_argument("--influx-token", default=None)
    ap.add_argument("--influx-org", default="uvfr")
    ap.add_argument("--influx-bucket", default="telemetry")
    args = ap.parse_args()

    if args.stdout:
        sink = StdoutSink()
    else:
        if not args.influx_token:
            ap.error("--influx-token is required unless --stdout is set")
        sink = InfluxSink(args.influx_url, args.influx_token,
                          args.influx_org, args.influx_bucket)

    ser = open_serial(args.port, args.baud)

    last_seq = None
    good = 0
    while True:
        try:
            raw = ser.readline().decode("utf-8", errors="replace")
        except serial.SerialException as e:
            print(f"[reader] serial dropped ({e}); reconnecting", file=sys.stderr)
            ser = open_serial(args.port, args.baud)
            continue
        if not raw:
            continue

        parsed = parse_line(raw)
        if parsed is None:
            continue
        seq, device_ms, values = parsed

        # host-side drop detection (mirrors the Pico's, but after the radio)
        if last_seq is not None:
            gap = (seq - last_seq - 1) & 0xFFFF
            if gap:
                print(f"[reader] WARN {gap} dropped frame(s) before seq {seq}",
                      file=sys.stderr)
        last_seq = seq

        sink.write(seq, device_ms, values)
        good += 1
        if good % 100 == 0:
            print(f"[reader] {good} frames ok", file=sys.stderr)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
