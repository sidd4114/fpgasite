# Web Dashboard

This folder contains the web interface files for the ESP32 FPGA Monitor.

## Files

- **index.html** - The main web dashboard (upload this to SPIFFS)

## How to Use

1. Ensure `index.html` is in this folder
2. In Arduino IDE, select: `Tools` → `ESP32 Sketch Data Upload`
3. The dashboard will be served at: `http://192.168.1.XXX`

## Features

- Real-time voltage display
- Live Chart.js graph
- 2-minute history tracking
- Failure detection alerts
- Mobile-responsive design
- Professional UI with animations

## Customization

The dashboard JavaScript in `index.html` includes:
- API endpoint configuration
- Chart update frequency
- Voltage threshold for alerts
- UI styling and animations

You can modify the JavaScript section to customize:
- Colors and themes
- Update intervals
- Alert behavior
- Data display format

All changes are in the `<script>` section at the end of the file.
