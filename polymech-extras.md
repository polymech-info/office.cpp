# Polymech Extras

Local customization tracker for OfficeCLI changes that should survive upstream updates.

## HTML Preview Theme

- [x] Add shared HTML preview options and theme helpers.
- [x] Add `officecli view <file> html --theme auto|light|dark`.
- [x] Apply theme marker/CSS to Excel HTML preview.
- [x] Apply theme marker/CSS to Word HTML preview.
- [x] Add local custom CSS support through `--css <file>`.
- [x] Keep PowerPoint/plugin preview behavior unchanged for this slice.
- [x] Extend current test harness with focused HTML theme checks.
- [x] Build and run focused verification.

CLI examples:

```bash
# Default: follows browser/system preference through data-theme="auto"
officecli view workbook.xlsx html
officecli view document.docx html

# Force a theme for generated Excel/Word HTML
officecli view workbook.xlsx html --theme dark
officecli view workbook.xlsx html --theme light
officecli view document.docx html --theme dark
officecli view document.docx html --theme light

# Append a local CSS file after built-in/theme CSS
officecli view workbook.xlsx html --theme dark --css ./preview-overrides.css
officecli view document.docx html --css ./preview-overrides.css

# Browser output keeps the same theme marker/CSS
officecli view workbook.xlsx html --theme dark --css ./preview-overrides.css --browser

# Screenshot fallback HTML also accepts the option
officecli view document.docx screenshot --render html --theme dark --out preview.png

# Focused verification
npm run test:html-theme
```

Notes:

- Keep the patch layer small: one options/helper file, one CLI option, and tiny renderer insertions.
- Preserve authored document colors by default. Theme affects preview chrome/background/headers/toolbars first.
- `--css` accepts local files only. Remote CSS URLs are rejected to preserve offline defaults.
- Avoid moving the existing renderers to templates in this slice.

## Offline / No Updates

- [x] Default OfficeCLI runtime to offline mode.
- [x] Disable normal CLI update checks unless explicitly opted in.
- [x] Disable MCP periodic update checks unless explicitly opted in.
- [x] Default `autoUpdate` config to `false` for new configs.
- [x] Suppress external HTML preview assets by default:
  - Google Fonts.
  - KaTeX CDN.
  - Three.js CDN/import map.
- [x] Block remote `http://` and `https://` file/image sources by default.
- [x] Set offline/no-update env vars in CMake and npm build/test/publish routes.

Default offline environment used by build routes:

```bash
OFFICECLI_NO_NETWORK=1
OFFICECLI_SKIP_UPDATE=1
OFFICECLI_NO_AUTO_INSTALL=1
```

Explicit opt-in for network behavior:

```bash
OFFICECLI_ALLOW_NETWORK=1 officecli view document.docx html
OFFICECLI_ALLOW_NETWORK=1 officecli config autoUpdate true
```

Notes:

- `OFFICECLI_ALLOW_NETWORK=1` permits update checks and external preview assets unless `OFFICECLI_NO_NETWORK=1` or `OFFICECLI_SKIP_UPDATE=1` is also set.
- Local files and `data:` URIs still work in offline mode.
- Hyperlinks inside generated documents are preserved as document content; they are not fetched by OfficeCLI.
