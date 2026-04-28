# Web Dashboard

This folder contains the web interface files for the Teensy UDP dashboard.

## Files

- **index.html** - The main web dashboard (served by the local Python server)

## How to Use

1. Ensure `index.html` is in this folder
2. From the project root, run: `python udp_web_server.py`
3. Open the dashboard at: `http://localhost:8000`

## Features

- Real-time 4-channel display
- Live Chart.js graphs
- Rolling history tracking
- Mobile-responsive design
- Professional UI with animations

## Customization

The dashboard JavaScript in `index.html` includes:
- API endpoint configuration
- Chart update frequency
- UI styling and animations

You can modify the JavaScript section to customize:
- Colors and themes
- Update intervals
- Alert behavior
- Data display format

All changes are in the `<script>` section at the end of the file.
