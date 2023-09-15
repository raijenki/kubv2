#!/bin/bash
picpodname=$(cat /etc/hostname)
delimiter="-"
IFS="$delimiter" read -ra parts <<< "$picpodname"
number=${parts[-2]}
picfile="/data/gem/$number/$number.inp" 
/home/sputniPIC "$picfile" &
p1=$!
wait $p1
