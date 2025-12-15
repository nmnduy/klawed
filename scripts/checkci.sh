runid=$1
set -x
gh run view $runid --json jobs \
  --jq '.jobs[] | select(.conclusion=="failure") | .databaseId' | \
  head -n1 | xargs -I{} gh run view --job {} --log
