#!/bin/sh
# SPDX-License-Identifier: MIT
# FXNET curl timing: dns, tcp connect, tls, ttfb, total for 10 sites, N rounds; prints one line per fetch.
TAG=${1:-x}; ROUNDS=${2:-3}; IPV=${3:-}
SITES="https://www.wikipedia.org/ https://www.bbc.co.uk/ https://www.cnn.com/ https://www.nytimes.com/ https://github.com/ https://www.reddit.com/ https://www.mozilla.org/en-US/ https://news.ycombinator.com/ https://www.theguardian.com/international https://www.apple.com/"
r=0
while [ $r -lt $ROUNDS ]; do
  for u in $SITES; do
    curl $IPV -s -o /dev/null --compressed -A 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:140.0) Gecko/20100101 Firefox/140.0' \
      -w "$TAG r$r %{time_namelookup} %{time_connect} %{time_appconnect} %{time_starttransfer} %{time_total} %{http_code} %{size_download} %{remote_ip} $u\n" \
      --max-time 20 "$u" || echo "$TAG r$r FAIL $u"
  done
  r=$((r+1))
done
