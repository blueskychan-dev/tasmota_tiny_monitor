# Tasmota Tiny-Monitor

A tiny single-file C HTTP server that listens on **port 7270**, fetches a Tasmota device status page (`/?m=1`), parses the HTML table, converts it to JSON, and returns it to the client.

> **⚠️ Big Warning / Disclaimer**
>
> - This code was written with **vibe coding energy** — it’s a hack, not a polished product.  
> - It exists mainly for **my own project**: [status.mindhas403.dev](https://status.mindhas403.dev).  
> - It may break at any time (Tasmota’s HTML isn’t stable).  
> - If you use it, **you’re on your own**.  
>
> TL;DR: fun hack → not serious → vibe responsibly ✨

---

## Features

- 🔌 Minimal dependencies: C + `libcurl`.
- 🌐 Serves JSON instead of scraping HTML yourself.
- ⏱️ Short timeouts (doesn’t hang forever).
- 🧩 Very easy to tweak (port, URL, labels).
- 🛠️ Lightweight: no frameworks, no magic.

---

## Quick Start

### Build

```bash
# Fedora
sudo dnf install -y gcc libcurl-devel

# Debian/Ubuntu
# sudo apt-get install -y build-essential libcurl4-openssl-dev

gcc -O2 -Wall -o tasmota_tiny_monitor tasmota_tiny_monitor.c -lcurl
```

### Run

```bash
./tasmota_tiny_monitor
curl http://127.0.0.1:7270/
```

---

## Example JSON Output

```json
{
  "name": "Tasmota Tiny-Monitor",
  "voltage": 233.000,
  "current": 0.170,
  "active_power": 25.000,
  "apparent_power": 39.000,
  "reactive_power": 31.000,
  "power_factor": 0.620,
  "energy_today_kwh": 0.233,
  "energy_yesterday_kwh": 0.570,
  "energy_total_kwh": 4.770,
  "state": "ON",
  "source": "http://192.168.1.124/?m=1"
}
```

---

## Configuration

Change these at the top of `tasmota_tiny_monitor.c`:

```c
#define LISTEN_PORT 7270
#define UPSTREAM_URL "http://192.168.1.124/?m=1"
#define NAME_STR "Tasmota Tiny-Monitor"
```

- **LISTEN_PORT** → server port (default: 7270).  
- **UPSTREAM_URL** → your Tasmota device URL.  
- **NAME_STR** → just the JSON “name” field.

---

## Endpoints

- `GET /` → Fetch Tasmota, parse HTML, return JSON.
  - `200 OK` → JSON payload
  - `502 Bad Gateway` → Tasmota unreachable
  - `500 Internal Server Error` → parse failed
  - `405 Method Not Allowed` → non-GET requests

---

## Use Cases / Why Bother?

- 🖥️ Feed for a **status dashboard**.  
- 📊 JSON metrics for Telegraf, Node-RED, or Grafana.  
- 🧪 Local monitoring / tinkering.  
- 🔒 Safer: exposes numbers only, not the whole Tasmota UI.  

---

## Not Serious Things™ (Limitations)

- **Fragile** → depends on Tasmota’s HTML structure.  
- **Blocking** → handles one request at a time.  
- **No TLS** → use behind nginx/caddy.  
- **Unofficial** → Tasmota might break it anytime.  

---

## Deployment Ideas

- Run with **systemd** (see sample unit in repo).  
- Build a tiny **Docker image**.  
- Put it behind a reverse proxy.  
- Add caching if you query it too often.  

---

## Related Projects

These tools together power [**status.mindhas403.dev**](https://status.mindhas403.dev):

- **🌸 MindTheNerd Monitor (Frontend / Dashboard)**  
  <https://github.com/blueskychan-dev/MindTheNerd-Monitor>  
  → The pretty web frontend for status and charts.

- **🔌 tasmota_tiny_monitor (This project)**  
  <https://github.com/blueskychan-dev/tasmota_tiny_monitor>  
  → Scrapes Tasmota’s `/?m=1` HTML, converts to JSON for the dashboard.

- **🩺 Mind HealthCheck (Backend)**  
  <https://github.com/blueskychan-dev/mind_healthcheck>  
  → Runs periodic checks on my servers and feeds results to the monitor.

---

## License

MIT

---

## Author’s Note

This was hacked together for **fun + utility**.  
It’s the “tiny JSON bridge” between my Tasmota device and my public status page.  
If you find it useful — cool. If it breaks — also cool. Just remember: **vibe coding only**.
