#!/bin/bash

program="APSP_Pthread"
p='-p batch'
pc=('1' '2' '4' '8' '12' '12' '12' '12' '12'  '12')
c=('1'  '2' '4' '8' '12' '16' '32' '64' '128' '2000')
testcase=("v2k_e2k" "v2k_e200k" "v2k_e1m" "v2k_e2m")
root="testcase/"
log="pthread_final_test.log"


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
        echo "srun $p -N 1 -n 1 -c ${pc[$i]} ./${program} $inp $oup ${c[$i]}" |& tee -a $log
        { time srun $p -N 1 -n 1 -c ${pc[$i]} ./${program} $inp $oup ${c[$i]} |& tee -a $log ; } |& tee -a $log

        diff $oup $ans
        if [ $? == 0 ] ; then
            echo -e "\e[1;32mCorrect\e[0m" |& tee -a $log
        else
            echo -e "\e[1;31mWrong\e[0m" |& tee -a $log
        fi
    done
done
