# 10 PC Telemetry Push Path

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Add the P2 companion path for computer telemetry by exposing a device endpoint that accepts pushed PC status data and making the terminal display the latest pushed snapshot. The design should assume a PC-side helper process actively sends CPU and memory information to the ESP instead of asking the ESP to inspect the computer directly.

## Acceptance criteria

- [ ] A documented local endpoint exists for a PC-side helper to push CPU, memory, and related desktop status data to the device.
- [ ] The device persists or caches the latest successful telemetry snapshot with its update time.
- [ ] The terminal shows the latest PC telemetry summary on a manual diagnostics surface without changing the agreed five-page P0 structure.
- [ ] If telemetry stops arriving, the device clearly indicates that the shown PC data is stale instead of pretending it is current.

## Blocked by

- [03 Network Setup and Management Entry](./03-network-setup-and-management-entry.md)
