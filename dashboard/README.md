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

## 2. Stand up InfluxDB + Grafana (Docker Compose — recommended)
One command brings up InfluxDB 2.x (pre-initialized with org `uvfr`, bucket
`telemetry`, and a token) and Grafana (datasource + dashboard auto-provisioned).

Needs Docker. On macOS: `brew install --cask docker`, then launch Docker Desktop once.
```bash
cd dashboard
docker compose up -d
```
- Grafana: http://localhost:3000 — the **UVFR Telemetry** dashboard is already
  there (anonymous viewing is on; admin/admin to edit).
- InfluxDB: http://localhost:8086 — login uvfr / uvfrpassword.

The dev token is `uvfr-dev-token-change-me` (set in docker-compose.yml and the
grafana datasource). Change it in both places before this leaves your laptop.

> Not using Docker? You can instead `brew install influxdb@2 grafana` (note:
> plain `brew install influxdb` gives InfluxDB **3**, which uses SQL on :8181 and
> is NOT compatible with this Flux dashboard). Then create the org/bucket/token
> by hand and import `grafana/dashboards/uvfr-telemetry.json`.

## 3. Stream into InfluxDB
```bash
source venv/bin/activate
python3 telemetry_reader.py --port /dev/cu.usbmodem1101 \
    --influx-url http://localhost:8086 --influx-token uvfr-dev-token-change-me \
    --influx-org uvfr --influx-bucket telemetry
```
Frames should appear on the Grafana dashboard within a second or two.

## Keeping the frame layout in sync
`FIELDS` in `telemetry_reader.py` mirrors the channel block in
`../protocol/telemetry.h`. If you change the struct there (and bump
`TELEM_VERSION`), update `FIELDS` to match.
