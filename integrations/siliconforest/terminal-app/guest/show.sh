#!/bin/sh
# scratch: out.log without the image list, repeated lines counted
cd /private/tmp/sfy
grep -E -v "SFCATCH image|SFCATCH x[0-9]|^SFSURVEY on" out.log | sed "s/^[0-9-]* [0-9:.]* Terminal[a-z.]*\[[0-9]*\] //; s/0x[0-9a-f]\{9,\}/0xADDR/g" | awk "{ if (!(\$0 in c)) order[n++]=\$0; c[\$0]++ } END { for (i=0;i<n;i++) printf \"%s%s\n\", (c[order[i]]>1 ? c[order[i]] \"x \" : \"\"), order[i] }" | cut -c1-${COLS:-230}
