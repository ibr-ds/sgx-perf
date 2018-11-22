datadir = dir.'-data/'
plots = dir.'-plots/'

set datafile separator ','
set terminal pdf
set grid

set xlabel "Time since application start (ns)"
set ylabel "Execution time (ns)"

#set format x "%.0fns"
set xtics rotate by -90
#set xtics ("10µs" 10000, "15µs" 15000, "20µs" 20000, "25µs" 25000, "30µs" 30000, "35µs" 35000, "40µs" 40000, "45µs" 45000,)
set ytics rotate by 45

set title filename noenhanced
set output plots.filename.'.pdf'

set style fill  transparent solid 0.35 noborder
#set style circle radius 0.1

plot datadir.filename.'.dat.sorted' using 1:2 with circles notitle