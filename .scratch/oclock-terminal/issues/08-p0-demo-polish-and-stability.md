# 08 P0 Demo Polish and Stability

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Polish the completed P0 slices into a contest-ready end-to-end experience. This slice should tighten the visual hierarchy of the home page, smooth loading and stale-data states, make network and refresh status understandable, and ensure the device can power on and walk through a stable demo flow without exposing obvious prototype rough edges.

## Acceptance criteria

- [ ] The home page clearly presents a clock-first layout with secondary date, weather summary, local temperature and humidity, and compact status information.
- [ ] Loading, offline, stale-cache, and refresh-in-progress states are surfaced in a calm and understandable way across the relevant pages.
- [ ] Boot, reconnect, and data-refresh transitions feel stable enough for repeated live demonstrations.
- [ ] The device can be demonstrated end to end as the agreed P0 product without depending on unfinished P1 or P2 features.

## Blocked by

- [02 Time System and Home Clock](./02-time-system-and-home-clock.md)
- [03 Network Setup and Management Entry](./03-network-setup-and-management-entry.md)
- [04 Local Environment Sensing](./04-local-environment-sensing.md)
- [05 Todo End-to-End Flow](./05-todo-end-to-end-flow.md)
- [06 Weather Location and Cache Flow](./06-weather-location-and-cache-flow.md)
- [07 Rotation and Refresh Orchestration](./07-rotation-and-refresh-orchestration.md)
