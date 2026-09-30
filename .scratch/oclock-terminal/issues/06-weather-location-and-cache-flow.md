# 06 Weather Location and Cache Flow

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Implement the weather slice end to end using a city-name-driven configuration flow and Open-Meteo as the forecast source. The browser management page should accept a city name, the device should resolve and cache coordinates, and the terminal should display a useful weather summary on the home page plus a richer manual-only weather page. Cached weather must remain usable during outages.

## Acceptance criteria

- [ ] The browser management page accepts a city name, and the backend resolves it to coordinates and caches the result for future weather requests.
- [ ] The device fetches weather from Open-Meteo on the agreed schedule, including retry backoff behavior after failures.
- [ ] The home page shows a concise weather summary, and the weather page shows current conditions, daily range, short forecast, and last update time.
- [ ] When weather refresh fails, the terminal continues showing the last successful cached weather with a clear freshness indicator instead of going blank.

## Blocked by

- [03 Network Setup and Management Entry](./03-network-setup-and-management-entry.md)
