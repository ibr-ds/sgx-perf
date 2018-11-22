datadir = dir.'-data/'
plots = dir.'-plots/'

set style fill solid 0.5
set datafile separator ','
set terminal pdf
set grid

set xlabel "Execution time"
set ylabel "# of Executions"

#set format x "%.0fns"
set xtics rotate by -90
set xtics ("10µs" 10000, "15µs" 15000, "20µs" 20000, "25µs" 25000, "30µs" 30000, "35µs" 35000, "40µs" 40000, "45µs" 45000,)
set ytics rotate by 45

set title filename noenhanced
set output plots.filename.'.pdf'

plot datadir.filename.'.dat' using 1:2 smooth freq with boxes lc rgb"gray" notitle