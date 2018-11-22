#!/bin/bash

DIR=$1

mkdir -p $DIR-plots
find $DIR-data -iname "*.dat" -printf "%f\n" | sed 's/\.[^.]*$//' | xargs -i gnuplot -e "dir='$DIR';filename='{}'" hist.plot
find $DIR-data -iname "*.dat" -printf "%f\n" | sed 's/\.[^.]*$//' | xargs -i bash -c "sort $DIR-data/{}.dat > $DIR-data/{}.dat.sorted"
find $DIR-data -iname "*.dat" -printf "%f\n" | sed 's/\.[^.]*$//' | xargs -i gnuplot -e "dir='$DIR';filename='{}'" scatter.plot
