# CI troubleshooting

To see latest CI runs

```sh
gh run list --event push
```

To see failed logs. For example, if run ID 18926779252 failed. Use this to find out what failed

```sh
gh run view 18926779252 --json jobs \
  --jq '.jobs[] | select(.conclusion=="failure") | .databaseId' | \
  head -n1 | xargs -I{} gh run view --job {} --log
```
