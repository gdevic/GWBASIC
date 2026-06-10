#!/usr/bin/env python3
"""Render GUIDE.md to GUIDE.pdf with a clean print layout."""
import sys
import markdown
from weasyprint import HTML

src, dst = sys.argv[1], sys.argv[2]

with open(src) as f:
    body = markdown.markdown(
        f.read(),
        extensions=["tables", "fenced_code", "toc", "sane_lists"],
        extension_configs={"toc": {"anchorlink": False}},
    )

css = """
@page {
    size: A4;
    margin: 22mm 18mm 20mm 18mm;
    @bottom-center {
        content: counter(page);
        font-family: 'DejaVu Sans', sans-serif;
        font-size: 8.5pt;
        color: #666;
    }
    @top-right {
        content: 'The BASIC User\\2019s Guide';
        font-family: 'DejaVu Sans', sans-serif;
        font-size: 8pt;
        color: #999;
    }
}
@page :first { @top-right { content: ''; } }

body {
    font-family: 'DejaVu Serif', serif;
    font-size: 10pt;
    line-height: 1.45;
    color: #1a1a1a;
}
h1 {
    font-family: 'DejaVu Sans', sans-serif;
    font-size: 21pt;
    border-bottom: 2.5pt solid #333;
    padding-bottom: 6pt;
    margin-bottom: 4pt;
}
h2 {
    font-family: 'DejaVu Sans', sans-serif;
    font-size: 14.5pt;
    color: #15406a;
    border-bottom: 0.7pt solid #b8c4d0;
    padding-bottom: 3pt;
    margin-top: 0;
    page-break-after: avoid;
}
/* every chapter starts on a fresh page */
h2:not(:first-of-type) { page-break-before: always; padding-top: 4pt; }
h3 {
    font-family: 'DejaVu Sans', sans-serif;
    font-size: 11.5pt;
    color: #15406a;
    margin-top: 14pt;
    page-break-after: avoid;
}
p { margin: 6pt 0; }
em { color: #333; }
hr { border: none; border-top: 0.7pt solid #ccc; margin: 10pt 0; }

code {
    font-family: 'DejaVu Sans Mono', monospace;
    font-size: 8.6pt;
    background: #f2f4f6;
    padding: 0.5pt 2.5pt;
    border-radius: 2pt;
}
pre {
    background: #f5f7f9;
    border: 0.6pt solid #d4dade;
    border-left: 2.5pt solid #5b7da0;
    padding: 6pt 9pt;
    margin: 7pt 0;
    page-break-inside: avoid;
    white-space: pre-wrap;
}
pre code {
    background: none;
    padding: 0;
    font-size: 8.6pt;
    line-height: 1.35;
}

table {
    border-collapse: collapse;
    margin: 8pt 0;
    width: 100%;
    page-break-inside: avoid;
    font-size: 9.2pt;
}
th, td {
    border: 0.6pt solid #b9c2ca;
    padding: 3pt 6pt;
    text-align: left;
    vertical-align: top;
}
th {
    background: #e6ecf1;
    font-family: 'DejaVu Sans', sans-serif;
    font-size: 8.8pt;
}
td code, th code { font-size: 8.2pt; }

ul, ol { margin: 5pt 0; padding-left: 18pt; }
li { margin: 2.5pt 0; }
blockquote {
    margin: 7pt 0 7pt 10pt;
    padding-left: 9pt;
    border-left: 2.5pt solid #aab8c5;
    color: #444;
    font-style: italic;
}
a { color: #15406a; text-decoration: none; }
"""

html = f"<html><head><meta charset='utf-8'><style>{css}</style></head><body>{body}</body></html>"
HTML(string=html).write_pdf(dst)
print("wrote", dst)
