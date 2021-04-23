#

3582 start
3584 - looks like drawing to screen, byte by byte, also saves the previous data (to restore)
3e - data to draw
3a - screen
3c - screen backup ?
43 - num lines
351f - calling it - always 10 bytes high
always 3 bytes wide (?) and 10 bytes high (?)
4300 - and masks for any set bit in what is being drawn

3635 - start
3652 - restore screen content

# Time profile for a short run
The data below is in the format:
```
<number_of_executions> <memory_start>..<memory_end>

```

Here's the data for a quick Zybex run:
```
...
2955 24BF..24C9
3168 330B..3417
3226 3534..3581
3300 3843..386D
3696 33FF..33FF
4400 2A7B..39AF
4890 2F5B..2F61
4975 2F57..2F59
5400 3BA9..3BB0
5440 3B9B..3BA2
5962 2ACF..2B0C
5979 25BE..25C1
5980 25B6..25BD
5984 3A67..3B67
6016 2F53..2F55
7072 36A4..37A0
8996 376E..377D
9048 36DE..36FD
9610 3650..3673
9640 3582..35B0
10190 2F4F..2F51
10308 2F49..2F90
11968 3B70..3B8E
22000 2A84..2A92
26272 3B91..3B93
28830 3652..3659 - restore screen
28920 3584..3593 - drawing to screen
564318 3BB6..3BB6 - VBLANK wait
564319 3BB8..3BB8 - VBLANK wait
```