#!/bin/bash

program="APSP_MPI_async"
p='-p batch'
Nd=('1' '2' '3' '4' '4')
nd=('1' '2' '3' '4' '8')
c=('12' '12' '12' '12' '6')
testcase=("case00" "case01" "case02" "case03" "case04" "case05" "case06")
root="testcase/"
log="mpi_async_final_test.log"


if [ -f "$log" ] ; then
    echo -e "$log existed!! -> \e[1;31mremove\e[0m"
    rm $log
fi

for ((n=0;n<${#testcase[@]};++n)); do
    inp="${root}${testcase[$n]}.in"
    ans="${root}${testcase[$n]}.out"
    oup="${testcase[$n]}.out"

    for ((i=0;i<${#c[@]};++i)); do
        if [ -f "$oup" ] ; then
            echo -e "$oup existed!! -> \e[1;31mremove\e[0m"
            rm ${oup}
        fi
        echo "srun $p -N ${Nd[$i]} -n ${nd[$i]} -c ${c[$i]} ./${program} $inp $oup ${c[$i]}" |& tee -a $log
        { time srun $p -N ${Nd[$i]} -n ${nd[$i]} -c ${c[$i]} ./${program} $inp $oup ${c[$i]} ; } |& tee -a $log

        diff $oup $ans
        if [ $? == 0 ] ; then
            echo -e "\e[1;32mCorrect\e[0m"
            echo "Correct" >> $log
        else
            echo -e "\e[1;31mWrong\e[0m"
            echo "Wrong" >> $log
        fi
    done
done
