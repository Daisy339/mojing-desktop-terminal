# 04 Local Environment Sensing

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Wire the onboard temperature and humidity sensor into the terminal experience so the device can show its local environment as a real hardware capability. This slice should periodically sample sensor data, derive a simple comfort-style status string, and present the information both on the home page and on the dedicated environment page.

## Acceptance criteria

- [ ] The device periodically reads local temperature and humidity from the onboard sensor and stores the latest successful sample with its timestamp.
- [ ] The home page shows local temperature and humidity as supporting information alongside the clock-first layout.
- [ ] The environment page shows temperature, humidity, sample time, and a simple textual comfort-style status.
- [ ] Sensor read failures degrade gracefully without crashing the device or leaving obviously broken UI.

## Blocked by

- [01 Board Bring-up and Page Shell](./01-board-bring-up-and-page-shell.md)
