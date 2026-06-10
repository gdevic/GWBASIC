10 OPEN "names.txt" FOR OUTPUT AS #1
20 WRITE #1, "Smith, John", 42, "x"
30 PRINT #1, "raw line here"
40 CLOSE #1
50 OPEN "names.txt" FOR INPUT AS #1
60 INPUT #1, A$, N, B$
70 PRINT "["; A$; "]"; N; "["; B$; "]"
80 LINE INPUT #1, L$
90 PRINT "L=["; L$; "]"
100 PRINT EOF(1)
110 CLOSE: KILL "names.txt"
