# Test report: media-img integration

## Summary

| Key | Value |
| --- | --- |
| Result | PASS |
| Assertions passed | 16 |
| Assertions failed | 0 |
| CWD | `C:\Users\zx\Desktop\pixlwiz\pixlwiz\packages\office` |
| Test binary | `C:\Users\zx\Desktop\pixlwiz\pixlwiz\packages\office\cpp\dist\pm-image.exe` |
| Assets dir | `C:\Users\zx\Desktop\pixlwiz\pixlwiz\packages\office\cpp\tests\tests\assets` |
| CLI args | `--officecli-html-theme-only` |
| Wall clock (total script) | **3278.86 ms** |

## Timing

| Metric | Value |
| --- | --- |
| Started (ISO) | 2026-06-23T08:22:08.761Z |
| Finished (ISO) | 2026-06-23T08:22:12.040Z |
| Wall time (perf) | 3282.054 ms |
| Wall time (clock) | 3282 ms |

## Process (Node)

| Metric | Value |
| --- | --- |
| PID | 18096 |
| Node | v22.22.0 |
| process.uptime() | 3.327 s |
| CPU user (process.cpuUsage Δ) | 0.000 ms (0 µs) |
| CPU system (process.cpuUsage Δ) | 16.000 ms (16000 µs) |
| CPU user (resourceUsage) | 31.000 ms |
| CPU system (resourceUsage) | 31.000 ms |
| Max RSS (resourceUsage) | 41 MB |
| RSS | 41 MB (43327488 B) |
| Heap used | 6.9 MB |
| Heap total | 11 MB |
| External | 2.2 MB |
| Array buffers | 81 KB |

## Host

| Metric | Value |
| --- | --- |
| Hostname | DESKTOP-QH14L5F |
| OS | win32 10.0.19045 |
| Arch | x64 |
| CPUs | 16 |
| CPU model | AMD Ryzen 7 3700X 8-Core Processor |
| RAM total | 64 GB |
| RAM free | 43 GB |
| RAM used | 21 GB |
| Load avg (1/5/15) | 0.00 / 0.00 / 0.00 |
| OS uptime | 2.52 h |

## Integration: performance by suite

### CLI: OfficeCLI HTML theme

- **Suite wall time:** 3276.88 ms

| Step | ms | Detail |
| --- | ---: | --- |
| xlsx html emits dark theme marker | 738.78 | exit 0 |
| xlsx html appends custom local CSS | 765.36 | exit 0 |
| docx html emits light theme marker | 795.28 | exit 0 |
| xlsx html defaults to auto theme | 729.88 | exit 0 |
| remote html css URL is rejected | 122.16 | exit 1 |
| invalid html theme is rejected | 119.92 | exit 1 |

---
*Written 2026-06-23T08:22:12.040Z*

---

*Artifact: `tests/test-report-last.md` — overwritten on each test run.*
