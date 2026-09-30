# 09 Album Upload and Frame Playback

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Add the P1 visual enhancement path by letting the browser management page upload static images or preprocessed frame sequences to the device and making the album page display them. This slice should avoid generic GIF decoding and should instead focus on a reliable upload, storage, and low-rate playback path that suits the board and screen.

## Acceptance criteria

- [ ] The browser management page can upload at least one static image or a preprocessed frame sequence to the device.
- [ ] Uploaded album assets are persisted locally and are available again after reboot.
- [ ] The album page can display the uploaded content, including low-rate frame playback for preprocessed animation sequences.
- [ ] Unsupported, oversized, or invalid uploads are rejected with clear feedback instead of failing silently.

## Blocked by

- [03 Network Setup and Management Entry](./03-network-setup-and-management-entry.md)
- [07 Rotation and Refresh Orchestration](./07-rotation-and-refresh-orchestration.md)
