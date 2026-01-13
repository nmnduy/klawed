#let money(v) = "$" + str(v)
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

  align(center)[ text(18pt, strong)[Invoice] ]
  v(12pt)

  grid(columns: (1fr, 1fr), gutter: 12pt)[
    [
      text(strong)[Company: ] company
      text(strong)[Client: ] client
    ]
    [
      text(strong)[Invoice \#: ] number
      text(strong)[Date: ] date.format()
    ]
  ]
  v(12pt)

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
  #let subtotal = sum_items(items)
  #let tax_amt = subtotal * tax
  #let total = subtotal + tax_amt
  align(right)[
    text(strong)[Subtotal: ] money(subtotal)
    text(strong)[Tax: ] money(tax_amt)
    text(strong)[Total: ] money(total)
  ]
]
