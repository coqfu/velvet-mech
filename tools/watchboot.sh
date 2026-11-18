#!/usr/bin/env bash
# Watch for the WCH ISP bootloader appearing/disappearing, with timestamps.
end=$((SECONDS + ${1:-60}))
state=absent
while [ $SECONDS -lt $end ]; do
    if lsusb -d 4348:55e0 >/dev/null 2>&1; then cur=present; else cur=absent; fi
    if [ "$cur" != "$state" ]; then
        printf '%s  %s\n' "$(date +%H:%M:%S.%3N)" "$cur"
        state=$cur
    fi
    sleep 0.05
done
