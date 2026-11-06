# Excel Chart Parser Fixture

Copied from `examples/excel/charts/charts-bubble.md` for C++ parser smoke tests.

```bash
officecli query charts-bubble.xlsx chart
officecli get charts-bubble.xlsx "/1-Bubble Fundamentals/chart[1]"
officecli get charts-bubble.xlsx Sheet1!A1:D10
officecli query charts-bubble.xlsx "Analysis!cell[value>5000 and value<8000]"
officecli query charts-bubble.xlsx "cell[font=Arial][bold=true]"
officecli query charts-bubble.xlsx "chart[2]"
```

Relevant source feature rows:

| Feature | Sheet |
|---------|-------|
| `bubble` chart type | 1, 2, 3, 4 |
| `bubbleScale` (50/80/100/120) | 1, 2, 3, 4 |
| `series1.bubbleSize` (range ref) | 4 |
