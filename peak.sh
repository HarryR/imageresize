#!/usr/bin/env bash
"$@" & # Run the given command line in the background.
pid=$! peak=0
while true; do
  sample="$(ps -o rss= $pid 2> /dev/null)" || break
  echo $sample
  let peak='sample > peak ? sample : peak'
done
echo "Peak: $peak" 1>&2

