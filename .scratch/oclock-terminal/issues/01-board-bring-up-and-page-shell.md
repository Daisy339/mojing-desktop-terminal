# 01 Board Bring-up and Page Shell

Status: ready-for-agent

## Parent

[PRD: O'Clock Desktop Smart Terminal](../PRD.md)

## What to build

Create the initial ESP-IDF firmware baseline for the Waveshare ESP32-S3-RLCD-4.2 board by starting from the official example approach and turning it into the O'Clock application shell. This slice is complete when the board can boot into the project firmware, render placeholder versions of all five pages, and respond to the single hardware button with basic short-press page switching and visible long-press feedback. The goal is a product-shaped shell without yet requiring live time, weather, or todo data.

## Acceptance criteria

- [ ] The firmware boots on the target board and renders placeholder versions of the home, weather, environment, todo, and album pages.
- [ ] A short press advances through the five pages in a predictable order, and a long press produces a visible placeholder refresh or special-action response.
- [ ] The project structure is recognizable as the O'Clock application rather than a raw vendor demo, while still reusing the official board bring-up path.
- [ ] The slice is demoable on hardware as a navigable device shell with no missing-screen failures.

## Blocked by

None - can start immediately
