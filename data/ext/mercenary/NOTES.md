# Interesting addresses

4185 - calls the actual line routine


5230 - line, X right major, Y down minor, ADC delta
5222 - loop
525d - line, Y down major, X rigth minor, ADC delta
524f - loop
528A - line, Y down major, X left minor, ADC delta
527C - loop for above
52b7 - line, X left major, Y down minor, SBC delta
52a9 - loop
52E4 - line, X left major, Y up minor, SBC  delta
52d6 - loop for above
5311 - line, Y up major, X left minor, ADC delta
5303 - loop for above
533e - line, Y up major, X right minor, SBC delta
5330 - loop for above
536b - line, X right major, Y up minor, ADC delta
535d - loop
538a - pixel

53b7 - code that sets either ORA or AND inside the line code


570e - two-color fill-the-line
A/07 - switch point
80 - color left
81 - color right
18 - #lines
7b - positive/negative between-line increment
06,07 - X switch point lo/hi
79,7a - X switch delta lo/hi
5823 - end of loop


586f - fill-the-line routine


line routines: X, Y input X, Y resp.
2600 - screen lines LO
2000 - screen lines HI
26a0 - div-by-4 (0,0,0,0,1,1,1,1,2,2,2,2) - X pos to byte
23 - screen memory HI (0x80 or 0xE0)
6a - max X ?
6b - max Y ?
64 - X/Y delta
06 - X/Y lo
04 - current Y

8010 - screen memory 1
e010 - screen memory 2
