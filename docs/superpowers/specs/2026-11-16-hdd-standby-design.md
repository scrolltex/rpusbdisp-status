# Do not wake sleeping HDDs — design

Date: 2026-11-16 · Status: approved by the user · Size: small change

## Why
The user does not want the status display to spin up sleeping SATA HDDs.

## Changes
- `smart-collect.sh` runs `smartctl -n standby`. A sleeping disk is skipped:
  its last JSON stays, and `<name>.standby` is touched. A normal run removes
  the marker.
- Rotational disks (`/sys/class/block/<disk>/queue/rotational == 1`) never read
  hwmon (`drivetemp` issues a disk command on every read); their temperature
  comes from the SMART JSON (`temperature.current`), i.e. at most every 10 min
  and only while awake. SSD/NVMe keep the 1 s hwmon reading.
- `read_smart`: a `.standby` marker newer than the JSON → `asleep`; freshness
  is judged by the newer of the two files, so a sleeping disk is not `?`.
- UI: asleep → badge dim followed by `zz`; temperature shows the last value.
- Unchanged: `statvfs` and `/proc/diskstats` are served from kernel memory and
  do not wake disks.

## Approaches
One sensible way; alternatives (hdparm -C polling, smartd) add tools without
benefit.
