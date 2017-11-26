#!/bin/bash

program="APSP_seq"
p='-p batch'
pc=('1')
c=('1')
testcase=("v200_e200" "v200_e1k" "v200_e10k" "v200_e20k")
log="seq_time_measure.log"


#if [ -f "$log" ] ; then
#    echo -e "$log existed!! -> \e[1;31mremove\e[0m"
#    rm $log
#fi

for ((n=0;n<${#testcase[@]};++n)); do
    inp="${testcase[$n]}.in"
    oup="${testcase[$n]}.out"

    for ((i=0;i<${#c[@]};++i)); do
        if [ -f "$oup" ] ; then
            echo -e "$oup existed!! -> \e[1;31mremove\e[0m"
            rm ${oup}
        fi
        echo "srun $p -N 1 -n 1 ./${program} $inp $oup ${c[$i]}" |& tee -a $log
        { time srun $p -N 1 -n 1 ./${program} $inp $oup ${c[$i]} |& tee -a $log ; } |& tee -a $log

        echo -e "\e[1;32mDone\e[0m"
    done
done
