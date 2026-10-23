# UVFR telemetry dashboard (laptop side)

Reads the base-pico over USB serial → parses the `DATA,` frames → InfluxDB → Grafana.

## Setup
```bash
cd dashboard
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt
```

## 1. Confirm the link (no database needed)
Find the Pico's serial port:
- macOS: `ls /dev/cu.usbmodem*`
- Linux: `ls /dev/ttyACM*`

Then watch parsed frames:
```bash
python3 telemetry_reader.py --port /dev/cu.usbmodem1101 --stdout
```
You should see one line per frame with real units (volts, amps, rpm, ...).

## 2. Stand up InfluxDB + Grafana (Docker is easiest)
```bash
docker run -d --name influxdb -p 8086:8086 influxdb:2.7
docker run -d --name grafana  -p 3000:3000 grafana/grafana:latest
```
- Open InfluxDB at http://localhost:8086, do first-time setup, create org `uvfr`
  and bucket `telemetry`, and generate an API token.
- Open Grafana at http://localhost:3000 (admin/admin), add InfluxDB as a data
  source (Flux query language, URL `http://localhost:8086`, your org + token),
  then build panels off the `telemetry` measurement.

## 3. Stream into InfluxDB
```bash
export INFLUX_TOKEN=... # the token from step 2
python3 telemetry_reader.py --port /dev/cu.usbmodem1101 \
    --influx-url http://localhost:8086 --influx-token $INFLUX_TOKEN \
    --influx-org uvfr --influx-bucket telemetry
```

## Keeping the frame layout in sync
`FIELDS` in `telemetry_reader.py` mirrors the channel block in
`../protocol/telemetry.h`. If you change the struct there (and bump
`TELEM_VERSION`), update `FIELDS` to match.
