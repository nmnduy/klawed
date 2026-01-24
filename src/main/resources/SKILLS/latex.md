# LaTeX Skills

This skill provides quick-start tips, recipes, and best practices for generating PDFs with LaTeX.

> **Reliability note**: Always compile/test the LaTeX file locally before sending the PDF to the user. Only ship artifacts that actually render without errors.

## What is LaTeX?
LaTeX is a document preparation system widely used for technical and scientific documents. It provides high-quality typesetting for complex documents including mathematical formulas, tables, and multilingual content.

## Installation

### macOS (Homebrew)
```bash
# Full TeX Live distribution
brew install --cask mactex

# Smaller basic distribution
brew install --cask basictex
```

### Ubuntu/Debian
```bash
# Full TeX Live
sudo apt-get install texlive-full

# Smaller installation
sudo apt-get install texlive-latex-base texlive-latex-extra
```

### Arch
```bash
sudo pacman -S texlive-core texlive-latexextra
```

## Compilation Basics

### Default: pdflatex
Use pdflatex unless custom fonts/RTL/non-Latin are needed:
```bash
pdflatex -interaction=nonstopmode -halt-on-error document.tex
```

### For Unicode/Custom Fonts: xelatex
For Unicode/non-ASCII characters (Vietnamese, Chinese, Arabic, etc.): **ALWAYS use xelatex**:
```bash
xelatex -interaction=nonstopmode -halt-on-error document.tex
```

The frontend now defaults to xelatex for better Unicode support.

## Minimal Invoice Skeleton

### Preamble (pdflatex-friendly)
```latex
\documentclass[11pt]{article}
\usepackage[margin=1in]{geometry}
\usepackage{tabularx,booktabs,array,xcolor}
\usepackage{fancyhdr,lastpage}
% Optional: \usepackage{hyperref} (only if links are desired)

% Brand colors (customize)
\definecolor{Primary}{HTML}{1F4B99}

\begin{document}
```

### Layout Structure
1. **Header** (from/to/meta): Two-column tabularx block
2. **Invoice meta**: Small tabular (Invoice #, Issue date, Due date, PO, Currency)
3. **Line items**: tabularx with columns: Description | Qty | Unit | Line Total
4. **Totals**: Small tabular (Subtotal, Tax, Discount, Shipping, Total Due in bold)
5. **Terms/Notes**: Simple itemize or paragraph text

### Example Template
```latex
\documentclass[11pt]{article}
\usepackage[margin=1in]{geometry}
\usepackage{tabularx,booktabs,array,xcolor,fancyhdr,lastpage}

\definecolor{Primary}{HTML}{1F4B99}

% Header/Footer
\pagestyle{fancy}
\fancyhf{}
\lhead{\textbf{{{COMPANY_NAME}}} \\ {{COMPANY_ADDRESS}}}
\rhead{Invoice \#{{INVOICE_NUMBER}}}
\cfoot{Page \thepage\ / \pageref{LastPage}}

\begin{document}

% Invoice Header
\noindent
\begin{tabularx}{\textwidth}{@{} X r @{}}
\textbf{Bill To:} & \textbf{Invoice Date:} {{ISSUE_DATE}} \\
{{CUSTOMER_NAME}} & \textbf{Due Date:} {{DUE_DATE}} \\
{{CUSTOMER_ADDRESS}} & \textbf{Currency:} {{CURRENCY}} \\
\end{tabularx}

\vspace{1em}
\noindent\color{Primary}{\rule{\textwidth}{0.8pt}}
\vspace{1em}

% Line Items
\rowcolors{2}{gray!6}{white}
\begin{tabularx}{\textwidth}{@{} X r r r @{}}
\toprule
\textbf{Description} & \textbf{Qty} & \textbf{Unit Price} & \textbf{Total} \\
\midrule
{{LINE_1_DESCRIPTION}} & {{LINE_1_QTY}} & {{LINE_1_UNIT}} & {{LINE_1_TOTAL}} \\
{{LINE_2_DESCRIPTION}} & {{LINE_2_QTY}} & {{LINE_2_UNIT}} & {{LINE_2_TOTAL}} \\
\bottomrule
\end{tabularx}

\vspace{1em}

% Totals
\begin{flushright}
\begin{tabular}{@{} l r @{}}
\textbf{Subtotal:} & {{SUBTOTAL}} \\
\textbf{Tax:} & {{TAX_AMOUNT}} \\
\midrule
\textbf{Total Due:} & \textbf{{{TOTAL_DUE}}} \\
\end{tabular}
\end{flushright}

\vspace{2em}

% Terms
\section*{Terms \& Notes}
{{TERMS_AND_CONDITIONS}}

\end{document}
```

## Recipes & Best Practices

### 1. Branded Header/Footer

**Recommended packages:**
- `fancyhdr` for header/footer
- `xcolor` for brand colors
- `lastpage` for page X / Y
- `graphicx` only if user provides a logo

**Header options:**
- Left: Business name (bold), Address/email/phone in smaller text
- Right: Invoice label + number
- Add a thin accent rule: `\color{Primary}{\rule{\textwidth}{0.6pt}}` below header band

**Footer options:**
- Center: `\thepage / \pageref{LastPage}`
- Optional: short support message or payment reminder

**Logo placeholder (if provided):**
```latex
\includegraphics[height=1.1cm]{logo.png}
```
Keep optional; avoid compile failure if logo missing.

### 2. Line Items and Totals Layout

**Packages:** tabularx, booktabs, array; optional: xcolor for row shading

**Line items table pattern:**
```latex
\rowcolors{2}{gray!6}{white}
\begin{tabularx}{\textwidth}{@{} X r r r @{}}
\toprule
\textbf{Description} & \textbf{Qty} & \textbf{Unit Price} & \textbf{Total} \\
\midrule
Item 1 & 2 & \$10.00 & \$20.00 \\
Item 2 & 1 & \$25.00 & \$25.00 \\
\bottomrule
\end{tabularx}
```

**Totals block:**
```latex
\begin{flushright}
\begin{tabular}{@{} l r @{}}
\textbf{Subtotal:} & \$45.00 \\
\textbf{Tax (7\%):} & \$3.15 \\
\midrule
\textbf{Total Due:} & \textbf{\$48.15} \\
\end{tabular}
\end{flushright}
```

### 3. Layout Variants

**Accent band:**
```latex
\noindent\color{Primary}{\rule{\textwidth}{0.8pt}}
```

**Shaded header row:**
```latex
\rowcolor{Primary!12}
```

**Multi-column header block:**
```latex
\begin{tabularx}{\textwidth}{@{} X r @{}}
\textbf{From:} & \textbf{Invoice \#:} INV-001 \\
Company Name & \textbf{Date:} 2026-01-24 \\
\end{tabularx}
```

### 4. Localization & Currency

**Dates:**
```latex
\usepackage{datetime2}
\DTMsetstyle{en-GB}  % or locale-specific styles
```

**Currency/number formatting:**
Keep placeholders literal unless user requests formatting. For locale-specific decimal formatting:
```latex
\usepackage{siunitx}
\sisetup{locale=DE, detect-weight=true}
```

**RTL/non-Latin:**
- Switch to xelatex (or lualatex)
- Use fontspec + appropriate fonts
- For RTL scripts, consider bidi/polyglossia

### 5. Typography & Fonts

**Engines:**
- **pdflatex**: Fast, stable, good for Latin scripts, no custom system fonts
- **xelatex**: Custom brand fonts, non-Latin scripts, RTL, advanced font features

**Font choices:**
- **pdflatex**: Stick to defaults or TeX Live fonts (e.g., mathpazo, lmodern, newtxtext)
- **xelatex**: Use fontspec:
  ```latex
  \usepackage{fontspec}
  \setmainfont{Arial}
  ```

**Micro-typography:**
```latex
\usepackage{microtype}  % Better spacing (pdflatex-friendly)
```

## Package Discipline

**Prefer core packages:**
- geometry, tabularx, booktabs, array, xcolor, fancyhdr, lastpage

**Add hyperref only if links are desired:**
```latex
\usepackage{hyperref}  % Place near end of preamble
```

**Avoid heavy packages:**
- Full TikZ unless user requests graphics
- Uncommon fonts or specialized packages

## Pre-flight Checks & Validation

### CRITICAL: Test Compilation Immediately

After creating template:

1. Save as .tex file in session directory
2. Test compilation:
   ```bash
   pdflatex -interaction=nonstopmode -halt-on-error template.tex
   ```
3. If Unicode/non-ASCII needed:
   ```bash
   xelatex -interaction=nonstopmode -halt-on-error template.tex
   ```
4. Check for common errors (see below)

**DO NOT proceed without successful compilation test.**

### Common LaTeX Syntax Errors

1. **Tabular environments**: Check for broken column specifications
   - Common error: `@{\\extracolsep{\\fill}}` should be `@{\extracolsep{\fill}}` (single backslash)
   - Never split column specs across lines

2. **Mismatched braces**: Count opening `{` and closing `}` - must be equal

3. **Unicode with pdflatex**: If using pdflatex and file has non-ASCII:
   - Switch to xelatex (recommended)
   - OR add `\usepackage[utf8]{inputenc}` and `\usepackage[T1]{fontenc}`

4. **Broken commands**: Check for double backslashes in wrong places

### Pre-compilation Validation

The LatexCompilerService now implements:
- Syntax validation runs before compilation
- Checks for mismatched braces, broken tabular specs, Unicode issues
- Returns clear error messages before attempting compilation

### Troubleshooting Tabular Errors

**"Misplaced \cr" error:**
- Check column specifications: `@{\extracolsep{\fill}}` (SINGLE backslash)
- Never split column specs across lines
- Ensure matching `&` and `\\` in each row

**Column alignment issues:**
- `l` = left, `c` = center, `r` = right, `X` = flexible width (tabularx)
- `@{}` removes column spacing, `@{ }` adds space

**Row coloring errors:**
```latex
\rowcolors{2}{gray!6}{white}  % start row, odd color, even color
```
Must be placed BEFORE `\begin{tabular}`

### Testing for Unicode/RTL Templates

For any non-Latin/Unicode/RTL content:

1. **ALWAYS use xelatex** (NOT pdflatex)
2. **Common Unicode errors:**
   - pdflatex: "Unicode character X not set up for use with LaTeX"
   - Solution: Switch to xelatex immediately
3. **Test with actual Unicode content:**
   - Don't assume it works - test with sample Vietnamese/Chinese/Arabic text
   - Verify characters display correctly in generated PDF
4. **Font availability:**
   - xelatex uses system fonts
   - Test with common fonts (Arial, Times New Roman) first
   - Warn user if custom fonts might not be available
5. **Validation before delivery:**
   - [ ] Template compiles with xelatex
   - [ ] All Unicode characters display correctly
   - [ ] RTL text flows right-to-left if applicable
   - [ ] Fonts render as expected

**NEVER deliver international template without xelatex compilation test.**

## Debugging Steps for AI Agent

1. Test compilation manually:
   ```bash
   pdflatex -interaction=nonstopmode -halt-on-error file.tex
   ```
2. Check log file for specific error lines
3. Common fixes:
   - Tabular errors: Fix column specifications
   - Unicode errors: Switch to xelatex
   - Missing packages: Add required `\usepackage` statements
4. Always validate template before giving to customer

## Placeholders

Use double braces for placeholders:
- `{{INVOICE_NUMBER}}`
- `{{CUSTOMER_NAME}}`
- `{{LINE_1_DESCRIPTION}}`
- `{{CURRENCY}}`

Duplicate line rows as needed. Keep placeholders literal unless user instructs specific formatting.

## Error Handling

**When errors occur:**
- If font not found under xelatex, fall back to a TeX Live bundled font or pdflatex-safe default and warn the user
- If a package is missing on minimal TeX, suggest installing latexextra/required collections
- Keep templates slim to avoid dependency issues

## Multi-page Tables

For many items, consider `longtable`:
```latex
\usepackage{longtable}
```
Otherwise keep simple tabularx for speed.

## Resources
- Official docs: https://www.latex-project.org/help/documentation/
- CTAN (packages): https://ctan.org/
- Overleaf tutorials: https://www.overleaf.com/learn
