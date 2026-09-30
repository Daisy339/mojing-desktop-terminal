# 03 Network Setup and Management Entry

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Create the device networking entry flow for first-time setup and later management. The slice should support AP setup mode when credentials are missing or recovery is requested, saving Wi-Fi settings locally, reconnecting in STA mode on later boots, and exposing a browser-accessible management entry page once the device is online.

## Acceptance criteria

- [ ] On first boot or when saved Wi-Fi is unavailable, the device enters AP setup mode and serves a configuration page reachable from a browser.
- [ ] Saving valid Wi-Fi credentials allows the device to join the target network and persist those credentials for later boots.
- [ ] After joining Wi-Fi, the device exposes a browser-accessible management entry page over the local network.
- [ ] A recovery path exists to re-enter temporary AP setup mode without reflashing the firmware.

## Blocked by

- [01 Board Bring-up and Page Shell](./01-board-bring-up-and-page-shell.md)
