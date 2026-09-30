# 07 Rotation and Refresh Orchestration

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Turn page navigation into the intended product behavior by implementing the agreed scheduler rules for idle rotation, manual navigation, and refresh actions. This slice should make the device feel intentional in daily use: only the home, todo, and album pages rotate automatically; manual navigation can reach all pages; manual input pauses rotation temporarily; and long press runs the refresh flow without throwing the user into a confusing state.

## Acceptance criteria

- [ ] In idle use, the terminal auto-rotates only the home, todo, and album pages with a stable dwell time per page.
- [ ] Manual short-press navigation can reach the weather and environment pages even though they do not participate in idle rotation.
- [ ] Manual interaction pauses auto-rotation for the agreed interval before idle behavior resumes automatically.
- [ ] Long press triggers the refresh-oriented special action and returns the device to a predictable post-refresh state.

## Blocked by

- [02 Time System and Home Clock](./02-time-system-and-home-clock.md)
- [04 Local Environment Sensing](./04-local-environment-sensing.md)
- [05 Todo End-to-End Flow](./05-todo-end-to-end-flow.md)
- [06 Weather Location and Cache Flow](./06-weather-location-and-cache-flow.md)
