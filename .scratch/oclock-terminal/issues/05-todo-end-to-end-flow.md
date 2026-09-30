# 05 Todo End-to-End Flow

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Implement the first full browser-to-device productivity slice by making the ESP-hosted management page support creating, editing, deleting, and completing todos that are stored locally on the device and rendered on the terminal's todo page. This slice should treat the ESP as the source of truth and should honor the agreed display rules for unfinished-only device output and due-date-first ordering.

## Acceptance criteria

- [ ] The browser management page can create, edit, delete, and mark todos complete through simple HTTP interactions with the device.
- [ ] Todo data is persisted locally on the device and survives reboot.
- [ ] The terminal's todo page shows only unfinished items by default, sorted by due date and then priority, with overdue items visually distinguishable.
- [ ] Completed items remain visible in the browser management page even though they are hidden from the device display by default.

## Blocked by

- [03 Network Setup and Management Entry](./03-network-setup-and-management-entry.md)
