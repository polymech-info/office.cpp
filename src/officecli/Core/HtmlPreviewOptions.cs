// Copyright 2025 OfficeCLI (officecli.ai)
// SPDX-License-Identifier: Apache-2.0

namespace OfficeCli.Core;

public sealed record HtmlPreviewOptions(string Theme = HtmlPreviewTheme.Auto, string? CssPath = null)
{
    public static HtmlPreviewOptions Default { get; } = new();

    public static HtmlPreviewOptions Create(string? theme, string? cssPath = null)
        => new(HtmlPreviewTheme.Normalize(theme), NormalizeCssPath(cssPath));

    private static string? NormalizeCssPath(string? cssPath)
    {
        if (string.IsNullOrWhiteSpace(cssPath)) return null;
        var trimmed = cssPath.Trim();
        if (trimmed.StartsWith("http://", StringComparison.OrdinalIgnoreCase) ||
            trimmed.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
        {
            throw new CliException($"Remote CSS URLs are disabled for offline previews: {trimmed}. Use a local CSS file.")
            {
                Code = "invalid_css"
            };
        }
        var fullPath = Path.GetFullPath(trimmed);
        if (!File.Exists(fullPath))
        {
            throw new CliException($"CSS file not found: {fullPath}")
            {
                Code = "invalid_css"
            };
        }
        return fullPath;
    }

    public string? LoadCustomCss()
        => CssPath == null ? null : File.ReadAllText(CssPath);
}

public static class HtmlPreviewTheme
{
    public const string Auto = "auto";
    public const string Light = "light";
    public const string Dark = "dark";

    public static string Normalize(string? theme)
    {
        var normalized = string.IsNullOrWhiteSpace(theme) ? Auto : theme.Trim().ToLowerInvariant();
        return normalized is Auto or Light or Dark
            ? normalized
            : throw new CliException($"Invalid --theme value: {theme}. Valid: auto, light, dark")
            {
                Code = "invalid_theme",
                ValidValues = [Auto, Light, Dark]
            };
    }

    public static string HtmlDataAttribute(HtmlPreviewOptions? options)
        => $" data-theme=\"{HtmlPreviewHelper.HtmlEncode((options ?? HtmlPreviewOptions.Default).Theme)}\"";

    public static string Css()
        => """
           :root,
           [data-theme="light"] {
             --office-preview-bg: #ffffff;
             --office-preview-text: #111827;
             --office-preview-muted: #6b7280;
             --office-preview-surface: #f8fafc;
             --office-preview-surface-2: #f3f4f6;
             --office-preview-border: #d0d7de;
             --office-preview-selection: rgba(37, 99, 235, 0.16);
           }

           [data-theme="dark"] {
             --office-preview-bg: #0f172a;
             --office-preview-text: #e5e7eb;
             --office-preview-muted: #94a3b8;
             --office-preview-surface: #111827;
             --office-preview-surface-2: #1e293b;
             --office-preview-border: #334155;
             --office-preview-selection: rgba(96, 165, 250, 0.22);
           }

           @media (prefers-color-scheme: dark) {
             [data-theme="auto"] {
               --office-preview-bg: #0f172a;
               --office-preview-text: #e5e7eb;
               --office-preview-muted: #94a3b8;
               --office-preview-surface: #111827;
               --office-preview-surface-2: #1e293b;
               --office-preview-border: #334155;
               --office-preview-selection: rgba(96, 165, 250, 0.22);
             }
           }

           body {
             background: var(--office-preview-bg);
             color: var(--office-preview-text);
           }
           """;
}
