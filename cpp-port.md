# C++ Port TODOs: Parsing for `get` and Selectors

Scope: parser plus the C++ edit slice for docx, xlsx, and pptx (`create`, `view`, `get`, `query`, `add`, `set`, `remove`, `move`, `swap`, `batch`, `validate`). PowerPoint pictures, solid backgrounds, transitions, and entrance/exit animations (`fade`, `fly`, `zoom`, `wipe`, and the other filter effects) are properties on those verbs. Charts, pivots, raw XML, watch, marks, HTML preview, morph, and emphasis animations stay on the vendored officecli.

## Edit slice

- [x] Standalone CLI is `pm-office` (`packages/office/cpp`). `npm test` there runs `tests/test-edit.mjs`.
- [x] Create, view (`text` / `outline` / `stats`), add, set, remove, move, swap, batch, and validate for the core types.
- [x] docx: paragraph, run, table, row, cell. xlsx: sheet, row, cell. pptx: slide, shape.
- [x] Host LLM tools `office_*` behind `FEATURE_OFFICE_LLM_TOOLS`, including the Android agent stack.
- [ ] Charts, pivots, raw XML, `--after find:`, docx `@paraId` edits, xlsx column insert, and move/swap outside docx paragraphs and tables.

## Current Boilerplate Decisions

- [x] Use C++20 for the C++ port (`cpp/CMakeLists.txt`).
- [x] Keep office-specific code under `cpp/src/lib/office`.
- [x] Register office CLI commands from `cpp/src/main.cpp` through `office::registerOffice`.
- [x] Compile through the repo build command: `npm run build:cpp`.
- [x] Add pure parser commands: `office parse-path` and `office parse-selector`.
- [x] Build office code as a reusable static library target; keep the CLI as a development-only consumer.
- [x] Add minizip-ng as a static embedded dependency for OOXML ZIP package reads.
- [x] Add pugixml as a static embedded dependency for OOXML XML part parsing.
- [x] Add first XLSX workbook index API and dev CLI command: `office xlsx-info`.
- [x] Replace the placeholder `office get` and `office query` handlers with first XLSX resolver-backed implementations.
- [x] Add first DOCX resolver-backed `office get` and `office query` implementations.

Current CLI shape:

```bash
./cpp/dist/pm-office office parse-path Sheet1!A1:D10 --format xlsx
./cpp/dist/pm-office office parse-selector 'cell[value>5000 and value<8000]'
./cpp/dist/pm-office office package-list example.xlsx
./cpp/dist/pm-office office package-read example.xlsx xl/workbook.xml
./cpp/dist/pm-office office xlsx-info example.xlsx
./cpp/dist/pm-office office get example.xlsx Sheet1!A1 --depth 1
./cpp/dist/pm-office office get example.xlsx Sheet1!A1:D10 --depth 1
./cpp/dist/pm-office office get example.xlsx /sheet[1]/cell[@ref=A1]
./cpp/dist/pm-office office get example.xlsx /meta/core
./cpp/dist/pm-office office query example.xlsx 'cell[value>5000 and value<8000]'
./cpp/dist/pm-office office get example.docx /body/p[1]/r[1] --depth 1
./cpp/dist/pm-office office get example.docx /meta/core
./cpp/dist/pm-office office query example.docx 'paragraph[style=Heading1] > run[bold=true]'
```

## Parser Boundary

Split path parsing and selector parsing hard. The syntaxes look similar, but the semantics differ and should not share AST node types.

```txt
PathParser
  parses only stable locator paths
  rejects selector-ish syntax

SelectorParser
  parses query syntax
  may contain positional narrowing
  does not pretend to be a stable locator
```

Key rule: `/body/p[2]` and `p[2]` are not the same construct. In a path, `[2]` is a child index inside a stable locator. In a selector, `[2]` is nth-match narrowing over query results. Keep those as different AST nodes even if they serialize similarly.

The C++ library should expose pure functions. CLI commands are only consumers:

```cpp
AstResult<PathAst> parsePath(std::string_view input, std::string_view format = "auto");
AstResult<Selector> parseSelector(std::string_view input);
std::string toJson(const PathAst& ast);
std::string toJson(const Selector& selector);
EvalResult evaluateFilter(const FilterExpr& expr, const NodeView& node);
```

Library consumers should use typed AST structs directly. CLI commands expose the same ASTs as JSON for development and golden smoke tests. No parser function should log, read files, inspect OOXML parts, or print CLI output. Resolution against Word/XLSX documents is a separate layer.

## Packaging Boundary

Production consumers should link the Office static library and call pure library APIs. The CLI is only a development harness for parser and resolver smoke testing.

Current CMake shape:

```txt
office static library
  document_node.cpp
  office_parser.cpp
  ooxml_metadata.cpp
  word_resolver.cpp
  xlsx_package.cpp
  xlsx_resolver.cpp
  xlsx_workbook.cpp
  minizip-ng static dependency
  pugixml static dependency

mono-cpp CLI
  main.cpp
  main_office_cli.cpp
  links office
```

Keep exported headers under `cpp/src/lib/office`. Avoid coupling library headers to CLI11, logger, stdout/stderr, or process exit codes.

## Retrieval And Query Scope

Current priority is retrieval plus selector/query behavior. Style expansion is intentionally stubbed for now: cells expose raw style ids when present, but the resolver does not parse `xl/styles.xml` into font/fill/border/number-format details yet.

OOXML package metadata retrieval is available for Word and XLSX through stable paths:

```txt
/meta
/meta/core
/meta/app
/meta/custom
```

XLSX data selectors currently operate over workbook data nodes:

```txt
sheet / worksheet
row
column / col
cell
```

Unsupported non-data targets such as charts, drawings, tables, pivots, and media should return clean unsupported errors until their retrieval model is explicitly added.

DOCX data selectors currently operate over:

```txt
body
paragraph / p
run / r
```

Word style expansion is also stubbed. The resolver reads direct paragraph style ids and direct run flags (`bold`, `italic`) from `word/document.xml`, but does not resolve inherited styles from `word/styles.xml` yet.

## TODOs

- [x] Start parser tests against AST JSON only; do not require `DocumentNode` for first parser golden tests.
- [x] Expose parser AST through both library and CLI:
  - [x] Library: `parsePath(...)`, `parseSelector(...)`, typed AST structs.
  - [x] CLI: `office parse-path`, `office parse-selector`, JSON AST output.
- [x] Add a minimal OOXML ZIP package reader:
  - [x] `XlsxPackage::listParts()`
  - [x] `XlsxPackage::hasPart(...)`
  - [x] `XlsxPackage::readText(...)`
  - [x] Dev CLI probes: `office package-list`, `office package-read`
- [x] Add a small real `.xlsx` fixture for resolver tests (generated by the Office smoke test as a valid ZIP/OOXML package).
- [x] Add XML parsing over workbook parts:
  - [ ] `[Content_Types].xml`
  - [x] `xl/workbook.xml`
  - [x] `xl/_rels/workbook.xml.rels`
  - [x] `xl/sharedStrings.xml`
  - [x] `xl/worksheets/sheet*.xml`
  - [x] `docProps/core.xml`, `docProps/app.xml`, and `docProps/custom.xml` for metadata retrieval.
- [x] Build the first XLSX workbook index: sheet names, sheet order, relationship targets, shared strings.
- [x] Implement first XLSX cell resolver:
  - [x] Resolve `ExcelReference` sheet name to worksheet part.
  - [x] Parse worksheet cells and rows.
  - [x] Resolve shared string values.
  - [x] Return one cell `DocumentNode` JSON for `office get example.xlsx Sheet1!A1`.
  - [x] Return collection `DocumentNode` JSON for ranges, rows, and columns.
- [x] Define a C++ `DocumentNode` model for resolver output. It matches the CLI JSON contract: `path`, `type`, `text`, `preview`, `style`, `childCount`, `format`, and `children`.
- [x] Implement first XLSX query resolver:
  - [x] Single-step `cell[...]` selectors.
  - [x] Sheet-scoped selectors such as `Sheet1!cell[...]`.
  - [x] Equality, inequality, contains, and numeric comparisons.
  - [x] Positional narrowing through `cell[2]`.
- [x] Broaden XLSX query resolver for data selectors:
  - [x] Comma union alternatives.
  - [x] Chained scopes such as `sheet[name=Sheet1] > row[2] > cell`.
  - [x] Terminal `sheet`, `row`, `column`, and `cell` result nodes.
  - [x] `:contains(...)`.
  - [x] `:empty`.
  - [x] `:has(formula)`.
- [x] Implement first DOCX resolver:
  - [x] Parse `word/document.xml`.
  - [x] Resolve `/body`, `/body/p[...]`, and `/body/p[...]/r[...]`.
  - [x] Support paragraph aliases `paragraph`/`p`.
  - [x] Support run aliases `run`/`r`.
  - [x] Return `DocumentNode` JSON for body, paragraph, and run retrieval.
- [x] Implement first DOCX query resolver:
  - [x] Paragraph selectors.
  - [x] Run selectors.
  - [x] `paragraph > run` chains.
  - [x] Comma union alternatives.
  - [x] `:contains(...)` and `:empty`.
  - [x] Direct paragraph `style` and direct run `bold`/`italic` filters.
- [ ] Broaden XLSX query resolver:
  - [ ] More pseudo-classes used by OfficeCLI outside XLSX cells.
  - [ ] Style expansion from `styles.xml` (deferred/stubbed).
  - [x] Missing-key warnings.
- [x] Define structured parser outputs instead of passing raw strings through the codebase:
  - [x] `PathSegment { name, predicate }`
  - [x] `PathPredicate = none | index | last | attribute`
  - [x] `ExcelReference = cell | range | row | column | sheet-scoped-element`
  - [x] `Selector { alternatives }`
  - [x] `SelectorAlternative { steps }`
  - [x] `SelectorStep { subject, filters, pseudos, nth }`
  - [x] `SelectorCombinator = descendant | child | union`
  - [x] `FilterExpr = predicate | and | or`
- [x] Implement a shared `PathParser` for the stable `get` grammar:
  - [x] `/`
  - [x] `/name`
  - [x] `/name[index]`
  - [x] `/name[last()]`
  - [x] `/name[@attr=value]`
  - [x] Reject `//`, trailing `/`, empty segments, empty predicates, multiple predicates, non-positive indexes, malformed attribute predicates.
- [x] Make `PathParser` reject selector-ish syntax:
  - [x] Bare selectors such as `p[2]`.
  - [x] Content filters such as `/body/p[style=Heading1]`.
  - [x] Pseudo-classes such as `/body/p:contains(Title)`.
  - [x] Comma unions and combinators.
- [x] Implement native Excel reference parsing before normal path parsing:
  - [x] `Sheet1!A1`
  - [x] `Sheet1!A1:D10`
  - [x] `Sheet1!1:1`
  - [x] `Sheet1!A:A`
  - [x] `Sheet1!table[1]`
  - [x] Keep this as its own grammar instead of treating it as a string normalization trick.
- [ ] Implement per-format path handling before resolution:
  - [ ] Word: apply path aliases and strict slash/predicate validation.
  - [x] Excel: native references first, plus slash aliases such as `/sheet[1]/cell[@ref=A1]` and `/sheet[@name=Sheet1]/cell[@ref=A1]`.
  - [ ] PowerPoint: defer until Word/XLSX are in place; when enabled, lowercase segment names, normalize table aliases, rewrite `cell[R,C]` to `tr[R]/tc[C]`, preserve quoted attribute values.
- [x] Keep document-dependent resolution outside the parser:
  - [x] `last()` parser output is only `Predicate::Last`; resolver counts siblings in the current document scope.
  - [x] Excel `sheet[N]` requires workbook sheet order.
  - [ ] PowerPoint `[@id=...]` and `[@name=...]` require searching the current slide/master/layout/group once PPTX is in scope.
- [x] Implement a tokenizer for selector strings:
  - [x] Identifiers and optional `@` prefix.
  - [x] Operators: `=`, `!=`, `~=`, `>=`, `<=`, `>`, `<`.
  - [x] Brackets, parentheses, commas, child combinator `>`.
  - [x] Quoted values and raw regex values like `r"..."`.
  - [x] Tokenize `r"..."` as one literal token and preserve escapes; do not parse regex values with regex.
  - [x] Reserved words: `and`, `or`.
- [x] Implement selector filter parsing with precedence:
  - [x] `and` binds tighter than `or`.
  - [x] Parentheses override precedence.
  - [x] Stacked brackets are implicit `and`.
  - [x] Bare `[key]` means existence.
  - [x] Numeric `[N]` is positional, not an attribute filter.
- [ ] Implement selector-level features currently used by the CLI:
  - [ ] Comma union for PowerPoint selectors.
  - [x] Child combinator `>` for XLSX/DOCX query chains.
  - [x] `:contains(...)` with quote-safe and nested-parenthesis-safe parsing.
  - [x] `:empty`.
  - [ ] `:no-alt`.
  - [x] Excel `:has(formula)`.
  - [ ] Shorthand `element:text`.
  - [x] Trailing positional narrowing for XLSX/DOCX selectors such as `cell[2]`, `row[2]`, and `p[2]`.
- [ ] Implement `AttributeFilter` evaluation against `DocumentNode`:
  - [ ] Case-insensitive key lookup in `format`.
  - [x] Fallback keys used by current resolvers: `text`, `type`, `style`, and Excel cell `value`.
  - [x] Equality and inequality.
  - [x] Contains with literal support.
  - [x] Numeric comparisons.
  - [ ] Color normalization so `#FF0000` equals `FF0000`.
  - [ ] Defer unit/dimension comparison through `EmuConverter` equivalent until after the first Word/XLSX parser slice.
  - [x] Missing-key warnings for query output.
- [ ] Add alias normalization:
  - [ ] Excel cell selector aliases: `bold -> font.bold`, `italic -> font.italic`, `font -> font.name`, `size -> font.size`, `color -> font.color`.
  - [ ] Word element aliases such as `p/paragraph`, `r/run`.
  - [ ] PPT aliases such as `pic/picture`, `row/tr`, `cell/tc` are deferred.
- [x] Add golden parser tests before wiring document traversal. First assert AST JSON only:
  - [x] `get report.docx /body/p[@paraId=1A2B3C4D]/r[1]`
  - [x] `get budget.xlsx Sheet1!A1:D10`
  - [x] `query "paragraph[style=Heading1] > run[bold=true]"`
  - [x] `query "cell[value>5000 and value<8000]"`
  - [ ] Defer PPTX examples: `get deck.pptx /slide[1]/shape[1]`, `get deck.pptx /slide[last()]/shape[@id=5]`, `query "shape[text~=Revenue]"`, `query "shape, chart, table"`.
- [x] Add invalid parser tests:
  - [x] `/body//p`
  - [x] `/body/p/`
  - [x] `/body/p[]`
  - [x] `/body/p[0]`
  - [x] `/body/p[-1]`
  - [x] `/body/p[1][2]`
  - [x] `/body/p[@]`
  - [x] `/body/p[@id=]`
  - [ ] `cell[value>]`
  - [ ] `paragraph[style=Heading1`
  - [ ] `paragraph[style=Heading1]]`
  - [ ] `shape[text~=r"(bad"]`

## Regex Problem

The current C# implementation uses many local regexes spread across handlers. That works for incremental CLI fixes, but it is a poor shape to port directly to C++.

Problems to avoid:

- Regexes are duplicated across Word, Excel, PowerPoint, generic XML, and the shared attribute filter. Small grammar changes require several edits.
- Regex matching is mostly unaware of nesting. Many helpers manually track bracket, parenthesis, quote, and comma depth before applying regexes.
- Regexes blur structural paths and content filters. For example, `[2]` can mean positional index, while `[value>2]` is a filter and `[@id=2]` is a stable locator.
- Regex fallthrough can silently change meaning. A malformed selector may skip one parser and become a generic XML query or broad element query.
- Handler-specific patches have accumulated around edge cases: `!=` shell escaping, `r"..."` regex values, `last()`, Excel `!` sheet prefixes, PPT `>` scoping, comma unions, and path-style selector rejection.
- Porting the regexes verbatim would preserve accidental behavior and make future parser changes harder, especially because C++ regex ergonomics and performance differ from .NET regex.

Preferred C++ approach:

- Use a small hand-written tokenizer and recursive-descent parser.
- Produce typed AST objects for paths and selector filters.
- Keep normalization and document-dependent resolution as explicit phases.
- Keep per-format traversal separate from parsing.
- Use regex only for narrow leaf validations, such as `A1` cell references or simple identifier checks.
