# 02 Time System and Home Clock

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Add the first live data slice for the terminal by making the home page show a real clock, date, and weekday backed by NTP plus the onboard RTC. This slice should make time feel appliance-like: the device should synchronize when network time is available, keep useful time locally when offline, and continue showing a polished clock-first home page after reboot.

## Acceptance criteria

- [ ] When network time is available, the device synchronizes through NTP and updates its local RTC source of truth.
- [ ] The home page shows the current time, date, and weekday using the maintained local time source.
- [ ] After a reboot without internet access, the device still shows plausible carried-forward time from the RTC instead of resetting to an invalid default.
- [ ] Time-sync success or stale-time fallback is handled gracefully enough for a live demo.

## Blocked by

- [01 Board Bring-up and Page Shell](./01-board-bring-up-and-page-shell.md)
