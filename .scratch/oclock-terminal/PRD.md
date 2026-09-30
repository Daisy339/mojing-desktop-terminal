# PRD: O'Clock Desktop Smart Terminal

Status: ready-for-agent

## Problem Statement

The project needs to become a competition-ready desktop smart terminal built on the Waveshare ESP32-S3-RLCD-4.2 board. The device must feel like a complete product instead of a loose demo: it should show time, date, weather, local temperature and humidity, and todos at a glance; support page switching with the hardware button; expose a browser-based management page for todo entry and device configuration; and remain stable enough for live demonstration. The terminal should keep its core value even without a connected computer, while still leaving room for later enhancements such as picture/frame animation upload and PC telemetry display.

## Solution

Build the terminal as an ESP-IDF application with the ESP32-S3 as the system owner. The device will store its own configuration and todos, connect to Wi-Fi, fetch weather from Open-Meteo, read local temperature and humidity from the onboard sensor, maintain time through NTP plus the onboard RTC, and serve its own local web interface for setup and todo management. The display experience will center on a polished clock-first home page, a small set of purpose-built pages, and a controlled auto-rotation model that prioritizes the home, todo, and album pages for ambient viewing. Advanced functions such as picture/frame upload and PC system telemetry will be staged behind the stable P0 path.

## User Stories

1. As a contest presenter, I want the terminal to boot into a polished screen experience, so that judges immediately understand it as a finished product.
2. As a user, I want to see the current time in a large, readable format, so that the device is useful as a real desktop clock.
3. As a user, I want to see the current date and weekday on the home page, so that I can understand the day context at a glance.
4. As a user, I want to see a weather summary on the home page, so that I can quickly understand current outdoor conditions.
5. As a user, I want to see local temperature and humidity on the home page, so that the device reflects my actual desk environment.
6. As a user, I want a dedicated weather page, so that I can view current weather, today's range, and a short future forecast when I need more detail.
7. As a user, I want a dedicated environment page, so that I can view the device's local sensor readings separately from network weather.
8. As a user, I want a dedicated todo page, so that I can use the device as a lightweight productivity display.
9. As a user, I want a dedicated album page, so that the terminal also has a visually engaging display mode.
10. As a user, I want the home, todo, and album pages to auto-rotate during idle use, so that the device remains lively without requiring interaction.
11. As a user, I want to manually switch pages with the hardware button, so that I can browse information on demand.
12. As a user, I want manual navigation to temporarily pause auto-rotation, so that the terminal does not interrupt me while I am viewing a page.
13. As a user, I want a long press action to trigger a manual refresh flow, so that I can recover data freshness without opening the web page.
14. As a user, I want the device to keep working from battery power, so that it behaves like a real standalone terminal.
15. As a user, I want the device to keep showing time, date, and local environment data even when no computer is connected, so that its core functions remain useful.
16. As a user, I want the device to provide its own browser-accessible management page, so that I can configure it from my computer without flashing new firmware.
17. As a first-time user, I want the device to enter AP setup mode when it has no valid Wi-Fi configuration, so that I can connect and configure it easily.
18. As a returning user, I want the device to reconnect automatically to saved Wi-Fi, so that it behaves like an appliance rather than a dev board.
19. As a user, I want to reopen configuration through a fallback AP mode, so that I can recover from network changes or bad settings.
20. As a user, I want to configure the weather location from the browser, so that the terminal can show relevant weather for my city.
21. As a user, I want to enter a city name instead of raw coordinates, so that configuration feels natural.
22. As a user, I want the system to resolve that city name into coordinates and cache them, so that future weather requests remain simple and reliable.
23. As a user, I want the weather data to refresh periodically, so that it stays useful without excessive battery or network cost.
24. As a user, I want the terminal to keep showing the last successful weather result during outages, so that network failure does not collapse the experience.
25. As a user, I want the weather page to show its last update time, so that I can tell whether the data is fresh or cached.
26. As a user, I want the device to synchronize time from the network and maintain it through the RTC, so that the clock remains accurate across reboots and offline periods.
27. As a user, I want to create todos from the browser, so that I can use a normal keyboard instead of device-side input.
28. As a user, I want to edit todos from the browser, so that I can quickly correct titles, dates, and priorities.
29. As a user, I want to delete todos from the browser, so that I can remove irrelevant items cleanly.
30. As a user, I want to mark todos complete from the browser, so that I can keep the device display focused on pending work.
31. As a user, I want the device to store todos locally, so that they remain available without relying on a computer-hosted service.
32. As a user, I want the device page to show only unfinished todos by default, so that the limited display area remains useful.
33. As a user, I want unfinished todos to be sorted by due date and then priority, so that urgent work appears first.
34. As a user, I want overdue todos to be visually distinguishable, so that the device highlights what needs attention.
35. As a user, I want the management page to work over simple local HTTP in P0, so that setup and debugging remain straightforward.
36. As a contest presenter, I want the system to remain understandable and debuggable, so that I can demonstrate both the device and the web flow confidently.
37. As a future user, I want to upload static images or preprocessed frame sequences to the terminal, so that the album page can become more dynamic in P1.
38. As a future user, I want the terminal to show frame-based animation instead of requiring generic GIF decoding, so that the feature remains reliable on the device.
39. As a future user, I want my computer to push CPU and memory data to the terminal, so that the terminal can act as a desk status display in P2.
40. As a developer, I want the system to be organized into testable modules, so that features can be added without destabilizing the contest-critical path.

## Implementation Decisions

- The project will target ESP-IDF as the primary firmware stack and will reuse the official Waveshare board examples as the baseline for display, sensor, RTC, and board-level integration.
- The ESP32-S3 device is the system owner for P0. It is the source of truth for configuration and todos, and it serves the browser-based management page directly.
- The product will be delivered in three stages:
  - P0: stable contest build with time, calendar, weather, local temperature and humidity, todo display, AP setup, STA mode, and ESP-hosted browser management.
  - P1: album enhancement with browser-assisted upload of static images or preprocessed frame sequences.
  - P2: computer telemetry integration with a PC-side helper pushing CPU and memory data to the device.
- The display information architecture will use five pages:
  - Home page
  - Weather page
  - Environment page
  - Todo page
  - Album page
- The default idle auto-rotation will include only the home, todo, and album pages.
- The weather page and environment page will be manual-only pages and will not participate in idle auto-rotation.
- Button behavior will be intentionally minimal:
  - Short press advances to the next page.
  - Long press triggers a refresh-oriented special action.
- Manual interaction pauses auto-rotation for 30 seconds before idle rotation resumes.
- The home page should prioritize presentation quality over maximum information density. The clock is the visual anchor, while date, weekday, weather summary, local temperature and humidity, and small status indicators are secondary.
- A central application state module should own the device snapshot used by the renderer. This state should combine time status, weather cache, local environment readings, todo summaries, network state, battery/status indicators, page mode, and future album/telemetry state behind a stable interface.
- A page scheduler module should encapsulate all page navigation rules, including auto-rotation, temporary pause after manual input, manual-only pages, and long-press refresh behavior. This is a deep module and should be kept independent from rendering code.
- A board support module should hide vendor-specific details for the RLCD display, button input, sensor reads, battery/status reads, and RTC interaction behind a narrow board-facing interface.
- A display composition module should convert application state into page view models and draw only when needed. The refresh strategy is event-driven first, with low-frequency scheduled refreshes where needed.
- The system should avoid naive high-frequency full-screen redraws. Whole-page redraws should happen on page changes or meaningful state changes. Data-specific refreshes should be limited to the smallest practical scope supported by the board's display stack.
- A network mode manager should handle two primary modes:
  - Normal STA mode using saved Wi-Fi credentials
  - Fallback AP setup mode when credentials are missing or recovery is requested
- The device should continue to expose a normal management page after successful Wi-Fi connection and should support a long-press recovery path into temporary AP setup mode.
- Configuration should be split by volatility:
  - Small settings such as Wi-Fi credentials, city name, cached coordinates, and rotation preferences should live in key-value persistence.
  - Structured data such as todos, cached weather payloads, and browser assets should live in a filesystem layer.
- The todo system should be modeled with the minimum competition-ready schema:
  - title
  - due date
  - priority
  - completion status
- Todo editing will be browser-only in P0. The device UI will not attempt local todo creation or editing through the single hardware button.
- The todo repository should expose list, create, update, delete, and complete operations and should return items in a display-ready sorted order. Sorting rules are:
  - unfinished items before finished items
  - among unfinished items, entries with due dates first
  - earlier due dates first
  - priority as a tie-breaker after due date
- The device display should default to showing only a small list of unfinished todos. Completed items remain stored and browser-visible but are hidden from the device page by default.
- The weather subsystem will use Open-Meteo for forecast data and a city-name-driven location flow. The browser accepts a city name, the backend resolves it to coordinates, and future forecast requests use cached coordinates.
- Weather refresh behavior will be:
  - normal refresh every 30 minutes
  - backoff retries after failures
  - explicit manual refresh from long press
  - continued display of the last successful weather cache during outages
- The weather page should show a concise but complete view:
  - current condition
  - current temperature
  - optional feels-like value if easy to support
  - humidity if available
  - daily high/low
  - two-day summary forecast
  - last successful update time
- The environment page should show local sensor-driven data, distinct from remote weather:
  - current temperature
  - current humidity
  - sampling timestamp
  - a simple comfort-style textual status
- The time subsystem will use NTP as the primary synchronization source and the onboard RTC as the persistent local timekeeper between syncs and across offline periods.
- The browser interface in P0 should use simple HTTP routes rather than real-time sockets. The page fetches state, submits changes through HTTP actions, and refreshes its own view after successful updates.
- P0 will not include account systems or full authentication. The management page is intended for local use on trusted LAN or temporary AP setup networks.
- The album feature in P1 should avoid generic on-device GIF decoding. The preferred model is static image display plus low-rate playback of uploaded, preprocessed frame sequences.
- P2 computer telemetry should be push-based from a PC helper process to an ESP endpoint. The ESP should not attempt to directly inspect the computer system.
- Power management in P0 should favor demonstration stability over aggressive sleep behavior. Battery operation matters, but complex power-saving modes are deferred until the core experience is reliable.
- The recommended build order is:
  - board baseline and display bring-up
  - time and page framework
  - AP setup and STA network flow
  - todo storage plus browser management
  - weather integration and caching
  - environment page integration
  - final rendering polish
  - P1/P2 extensions

## Testing Decisions

- Good tests for this project should validate external behavior and stable contracts, not vendor internals or display implementation details.
- The project should prefer deep-module testing where logic can be exercised off-device with deterministic inputs.
- The first modules that should receive tests are:
  - page scheduler state machine
  - todo repository and sorting behavior
  - configuration validation and persistence mapping
  - weather refresh policy, cache fallback, and location-resolution flow
  - browser API request and response behavior for todo and configuration operations
  - time service fallback behavior around NTP sync state and RTC-backed continuity
- Hardware-facing code should rely on smoke tests and focused integration tests instead of heavy mocking of vendor libraries.
- Display tests should verify page selection and view-model behavior, not pixel-perfect frame buffers, unless the rendering stack later becomes abstract enough to support that cheaply.
- Browser/API tests should focus on visible outcomes:
  - creating a todo stores it and returns the updated list
  - editing and completing a todo changes subsequent reads
  - invalid config input is rejected clearly
  - recovery/AP mode exposes the expected configuration flow
- Weather tests should cover stale-cache and failure-retry behavior so that outage handling is intentional rather than accidental.
- Because the current codebase does not yet contain an established automated test suite, this project should introduce testing conventions together with the first deep modules rather than imitate nonexistent prior art.

## Out of Scope

- Touch interaction or any touch-first UI design
- Rich local editing of todos from the device itself
- Multi-button menu systems
- Generic GIF decoding on the ESP in P1
- Full user accounts, cloud auth, or internet-facing administration
- MQTT-first architecture for P0
- Hourly weather graphs or dense meteorological dashboards
- Long-term environment history charts
- Todo reminders, alarms, or notification scheduling
- Multi-user todo collaboration
- Advanced battery optimization such as deep sleep orchestration in P0
- Direct ESP-side collection of PC CPU or memory metrics
- Any requirement that all features work only through a connected computer

## Further Notes

- The project should feel like a real desktop terminal first and a feature list second. That means the page system, visual rhythm, and data freshness behavior are product decisions, not just implementation details.
- The official Waveshare ESP32-S3-RLCD-4.2 documentation and ESP-IDF examples should be treated as the board bring-up reference source before custom application logic is layered on top.
- The browser experience is part of the product, but in P0 it should stay intentionally simple: setup, configuration, and todo management over local HTTP.
- The local markdown issue tracker for this repo should use this PRD as the feature anchor for subsequent implementation issues under the same feature slug.
