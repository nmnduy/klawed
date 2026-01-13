# Typst Skill

This skill provides quick-start tips and snippets for generating PDFs with Typst.

> Reliability note: Always compile/test the Typst file locally before sending the PDF to the user. Only ship artifacts that actually render without errors.

## What is Typst?
Typst is a modern markup-based typesetting system (similar to LaTeX but faster and simpler) used to create PDFs. Learn more: https://typst.app/

## Installation

### macOS (Homebrew)
```bash
brew install typst
```

### Ubuntu/Debian
```bash
# Official Typst binaries via install script
curl -fsSL https://typst.app/install.sh | sh

# Or via package manager (may be older)
# sudo apt-get install typst
```

### Arch
```bash
sudo pacman -S typst
```

## Usage Basics

Render a Typst file to PDF:
```bash
typst compile doc.typ
```

Watch mode (auto-recompile on change):
```bash
typst watch doc.typ
```

Preview in browser (if installed):
```bash
typst watch --open doc.typ
```

## Minimal Example (`doc.typ`)
```typst
#set page(width: 8.5in, height: 11in, margin: 1in)
#set text(font: "Helvetica", size: 11pt)

= Sample Document

This PDF was generated with Typst.

== Bullet List
- First item
- Second item

== Table
#let data = (
  ("Item", "Qty", "Price"),
  ("Widget", 2, "$10"),
  ("Gadget", 1, "$25"),
)
#table(
  columns: (1fr, auto, auto),
  inset: 6pt,
  data.map(row => row.map(c => c)),
)

== Image
// Uncomment and replace with your image path
//#image("images/example.png", width: 3in)
```

## Invoice/Report Skeleton (`invoice.typ`)
```typst
#import "lib/invoice.typ": invoice

#let items = (
  ("Service", 2, 150),
  ("Hosting", 1, 50),
)
#let tax_rate = 0.07

= Invoice

#invoice(
  "Acme Co.",
  "Example LLC",
  "INV-2024-001",
  datetime.today(),
  items,
  tax_rate,
)
```

## Reusable Library (`lib/invoice.typ`)
```typst
#let money(v) = "$" + v.format(precision: 2)
#let sum_items(items) = items.fold(0, (acc, it) => acc + it.at(1) * it.at(2))

#let invoice(
  company,
  client,
  number,
  date,
  items,
  tax,
) = [
  set text(font: "Helvetica", size: 11pt)
  set page(margin: 1in)

  align(center)[
    text(18pt, strong)[Invoice]
  ]
  v(12pt)

  // Header
  grid(columns: (1fr, 1fr), gutter: 12pt)[
    [
      text(strong)[Company: ] company
      text(strong)[Client: ] client
    ]
    [
      text(strong)[Invoice #: ] number
      text(strong)[Date: ] date.format()
    ]
  ]
  v(12pt)

  // Items table
  #let headers = ("Item", "Qty", "Unit", "Total")
  #let body = items.map(it => (
    it.at(0),
    it.at(1),
    money(it.at(2)),
    money(it.at(1) * it.at(2)),
  ))
  table(
    columns: (1fr, auto, auto, auto),
    inset: 6pt,
    align: (left, right, right, right),
    header: headers,
    body: body,
    stroke: 0.5pt + gray + solid,
  )

  v(12pt)

  // Totals
  #let subtotal = sum_items(items)
  #let tax_amt = subtotal * tax
  #let total = subtotal + tax_amt
  align(right)[
    text(strong)[Subtotal: ] money(subtotal)
    text(strong)[Tax: ] money(tax_amt)
    text(strong)[Total: ] money(total)
  ]
]
```

## Goal Folder
This skill lives under `SKILLS/pdf/` because Typst is used to achieve the PDF-generation goal. Tools (Typst) are documented inside the goal folder.

## How to Use in Agent Workspace
1. Ask the user to upload or describe the desired PDF.
2. Create a `.typ` file in the session workspace (e.g., `documents/report.typ`).
3. Run `typst compile documents/report.typ` to produce `documents/report.pdf`.
4. Share the generated PDF with the user.

## Troubleshooting
- If `typst` command is missing: install via the commands above.
- Fonts: Typst uses system fonts; ensure referenced fonts are installed.
- Images not showing: verify path and that the image exists in workspace.
