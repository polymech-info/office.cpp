# mono-cpp

Minimal C++ CLI boilerplate built with CMake.

Cross-platform builds for Windows, macOS, and Linux using CLI11, tomlplusplus, and spdlog.
The Office port is built as a reusable static library; the `mono-cpp office ...` CLI is a development harness only.

## Prerequisites

| Tool | Version |
| ---- | ------- |
| CMake | ≥ 3.20 |
| C++ compiler | C++20 (MSVC, GCC, or Clang) |
| Ninja | (optional, for macOS/Linux) |

## Quick Start

```bash
# Windows
cmake --preset dev
cmake --build --preset dev

# macOS/Linux with Ninja
cmake --preset release-macos -G Ninja    # macOS
cmake --preset release-linux -G Ninja     # Linux
cmake --build --preset release-macos
```

## Build Scripts

```bash
# Windows
npm run build              # Debug build
npm run build:release      # Release build

# macOS
npm run install:macos      # Install deps (cmake, ninja)
npm run build:macos        # Build
npm run rebuild:macos      # Clean + build

# Linux
npm run install:linux      # Install deps (cmake, ninja, clang)
npm run build:linux        # Build
npm run rebuild:linux      # Clean + build
```

## Usage

```bash
./dist/mono-cpp --help
./dist/mono-cpp --version
./dist/mono-cpp hello
./dist/mono-cpp hello --name Developer
./dist/mono-cpp config config.toml
./dist/mono-cpp office parse-path Sheet1!A1:D10 --format xlsx
./dist/mono-cpp office parse-selector 'cell[value>5000 and value<8000]'
./dist/mono-cpp office package-list example.xlsx
./dist/mono-cpp office package-read example.xlsx xl/workbook.xml
./dist/mono-cpp office xlsx-info example.xlsx
./dist/mono-cpp office get example.xlsx Sheet1!A1 --depth 1
./dist/mono-cpp office get example.xlsx Sheet1!A1:D10 --depth 1
./dist/mono-cpp office get example.xlsx /sheet[1]/cell[@ref=A1]
./dist/mono-cpp office get example.xlsx /meta/core
./dist/mono-cpp office query example.xlsx 'cell[value>5000]'
./dist/mono-cpp office query example.xlsx 'sheet[name=Sheet1] > row[2] > cell'
./dist/mono-cpp office query example.xlsx 'cell:has(formula)'
./dist/mono-cpp office get example.docx /body/p[1]/r[1] --depth 1
./dist/mono-cpp office get example.docx /meta/core
./dist/mono-cpp office query example.docx 'paragraph[style=Heading1] > run[bold=true]'
```

## Office Library API

Production consumers should link the static `office` library and use typed ASTs directly:

```cpp
#include "office_parser.h"
#include "word_resolver.h"
#include "xlsx_package.h"
#include "xlsx_resolver.h"
#include "xlsx_workbook.h"

auto path = office::parser::parsePath("Sheet1!A1:D10", "xlsx");
auto selector = office::parser::parseSelector("cell[value>5000 and value<8000]");
auto package = office::XlsxPackage("example.xlsx");
auto workbook = office::loadWorkbook(package);
auto range = office::resolveXlsxGet(package, path.ast);
auto hits = office::resolveXlsxQuery(package, selector.ast);
```

The dev CLI exposes parser ASTs and resolver `DocumentNode`s as JSON.

XLSX retrieval/query support currently focuses on data nodes (`sheet`, `row`, `column`, `cell`). Native refs such as `Sheet1!A1` and slash aliases such as `/sheet[1]/cell[@ref=A1]` are supported. Style expansion is stubbed: raw style ids are surfaced when present, but `xl/styles.xml` is not expanded yet.

DOCX retrieval/query support currently focuses on `body`, `paragraph`/`p`, and `run`/`r`. Inherited Word styles are stubbed; direct paragraph style ids and direct run `bold`/`italic` flags are surfaced.

OOXML metadata retrieval works for XLSX and DOCX through `/meta`, `/meta/core`, `/meta/app`, and `/meta/custom`. Query results include `format.warnings` entries for unsupported filter keys.

## Project Structure

```text
mono-cpp/
├── CMakeLists.txt          # Main build config
├── CMakePresets.json       # Dev/Release presets (Windows/macOS/Linux)
├── package.json            # npm build scripts
├── config.toml             # Example TOML config
├── src/
│   ├── main.cpp            # CLI entry point
│   └── lib/
│       └── office/         # Static Office parser/resolver library
└── packages/
    └── logger/             # Example package (spdlog wrapper)
        ├── CMakeLists.txt
        ├── include/logger/
        └── src/
```

## Dependencies

- [CLI11](https://github.com/CLIUtils/CLI11) - Command line parser
- [tomlplusplus](https://github.com/marzer/tomlplusplus) - TOML config parser
- [spdlog](https://github.com/gabime/spdlog) - Logging library
- [minizip-ng](https://github.com/zlib-ng/minizip-ng) - Static ZIP reader for OOXML packages
- [pugixml](https://github.com/zeux/pugixml) - Static XML parser for OOXML parts

## License

MIT
