# Test report: media-img integration

## Summary

| Key | Value |
| --- | --- |
| Result | PASS |
| Assertions passed | 98 |
| Assertions failed | 0 |
| CWD | `C:\Users\zx\Desktop\pixlwiz\pixlwiz\packages\office\cpp` |
| Test binary | `C:\Users\zx\Desktop\pixlwiz\pixlwiz\packages\office\cpp\dist\pm-office.exe` |
| Assets dir | `C:\Users\zx\Desktop\pixlwiz\pixlwiz\packages\office\cpp\tests\orchestrator\tests\assets` |
| CLI args | `--office-smoke-only` |
| Wall clock (total script) | **1208.3 ms** |

## Timing

| Metric | Value |
| --- | --- |
| Started (ISO) | 2026-11-11T13:04:50.762Z |
| Finished (ISO) | 2026-11-11T13:04:51.970Z |
| Wall time (perf) | 1212.247 ms |
| Wall time (clock) | 1212 ms |

## Process (Node)

| Metric | Value |
| --- | --- |
| PID | 38916 |
| Node | v24.13.0 |
| process.uptime() | 1.262 s |
| CPU user (process.cpuUsage Δ) | 109.000 ms (109000 µs) |
| CPU system (process.cpuUsage Δ) | 140.000 ms (140000 µs) |
| CPU user (resourceUsage) | 140.000 ms |
| CPU system (resourceUsage) | 171.000 ms |
| Max RSS (resourceUsage) | 55 MB |
| RSS | 55 MB (57995264 B) |
| Heap used | 8.1 MB |
| Heap total | 12 MB |
| External | 2.2 MB |
| Array buffers | 27 KB |

## Host

| Metric | Value |
| --- | --- |
| Hostname | DESKTOP-QH14L5F |
| OS | win32 10.0.26200 |
| Arch | x64 |
| CPUs | 16 |
| CPU model | AMD Ryzen 7 3700X 8-Core Processor |
| RAM total | 64 GB |
| RAM free | 32 GB |
| RAM used | 32 GB |
| Load avg (1/5/15) | 0.00 / 0.00 / 0.00 |
| OS uptime | 196.33 h |

## Integration: performance by suite

### CLI: office parser smoke

- **Suite wall time:** 1206.73 ms

| Step | ms | Detail |
| --- | ---: | --- |
| spawnSync --help | 23.61 | exit 0 |
| spawnSync office --help | 22.23 | exit 0 |
| spawnSync office package-list C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx | 39.4 | exit 0 |
| spawnSync office package-read C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx xl/workbook.xml | 25.36 | exit 0 |
| spawnSync office xlsx-info C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx | 27.78 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx Sheet1!A1 | 24.84 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx /sheet[1]/cell[@ref=A1] | 25.53 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx /meta/core | 22.86 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx Sheet1!A1:B2 | 26.12 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx Sheet1!1:1 | 25.27 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx Sheet1!B:B | 36.88 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx cell[value>5000] | 29.27 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx Sheet1!cell[value~=Revenue] | 25.3 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx Sheet1!cell[value=6000], Sheet1!cell[value=42] | 24.96 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx cell[2] | 25.1 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx sheet[name=Sheet1] > row[2] > cell | 24.76 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx cell:has(formula) | 24.72 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx cell:contains("Hello") | 24.11 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx cell:empty | 24.84 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx row[2] | 24.55 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx column[name=B] | 34.32 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx sheet[2] | 24.83 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-xlsx-LfoLjT\minimal.xlsx cell[unknownField=1] | 24.08 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-docx-XDEYei\minimal.docx /body --depth 1 | 33.26 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-docx-XDEYei\minimal.docx /meta/core | 22.33 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-docx-XDEYei\minimal.docx /body/p[1] --depth 1 | 22.64 | exit 0 |
| spawnSync office get C:\Users\zx\AppData\Local\Temp\office-docx-XDEYei\minimal.docx /body/p[2]/r[2] | 22.33 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-docx-XDEYei\minimal.docx paragraph[style=Heading1] | 22.65 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-docx-XDEYei\minimal.docx paragraph[style=Heading1] > run[bold=true] | 22.07 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-docx-XDEYei\minimal.docx paragraph:contains("Revenue") > run[italic=true] | 22.04 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-docx-XDEYei\minimal.docx run[bold=true], run[italic=true] | 32.14 | exit 0 |
| spawnSync office query C:\Users\zx\AppData\Local\Temp\office-docx-XDEYei\minimal.docx paragraph[unknownField=1] | 22.81 | exit 0 |
| spawnSync office parse-path Sheet1!A1:D10 --format xlsx | 20.47 | exit 0 |
| spawnSync office parse-path /body/p[1]/r[1] --format docx | 20.37 | exit 0 |
| spawnSync office parse-selector cell[value>5000 and value<8000] | 20.47 | exit 0 |
| spawnSync office parse-selector paragraph[style=Heading1] > run[bold=true] | 20.63 | exit 0 |
| spawnSync office parse-selector chart[2] | 21.33 | exit 0 |
| spawnSync office parse-selector Analysis!cell[value~=Revenue] | 20.47 | exit 0 |

---
*Written 2026-11-11T13:04:51.970Z*

---

*Artifact: `tests/test-report-last.md` — overwritten on each test run.*
