#!/bin/bash
# Usage: run_tests.sh <suite-number> [test-selection]
#   suite-number : 1..13 (see the tester main menu)
#   test-selection: what to type inside the suite menu (default "0" = all)
set -u
ROOT=/home/aazzaoui/Downloads/aa
SRV=$ROOT/Webserver-main
TST=$ROOT/web-serv-Tester-main

SUITE=$1
SEL=${2:-0}

# number of tests per suite -> the "Return" entry is count+1
counts=( [1]=14 [2]=6 [3]=4 [4]=5 [5]=5 [6]=5 [7]=6 [8]=7 [9]=19 [10]=5 [11]=6 [12]=10 [13]=32 )
RET=$(( ${counts[$SUITE]} + 1 ))

pkill -x webserv >/dev/null 2>&1
sleep 0.3

cd "$SRV/EngineX/www"
mkdir -p upload no-index-dir empty_dir forbidden_dir error
chmod 700 forbidden_dir 2>/dev/null
[ -f forbidden.html ] || echo "forbidden html" > forbidden.html
[ -f forbidden.txt  ] || echo "forbidden txt"  > forbidden.txt
[ -f forbidden_dir/secret.txt ] || echo secret > forbidden_dir/secret.txt
chmod 000 forbidden.html forbidden.txt forbidden_dir
rm -rf upload/* 2>/dev/null
mkdir -p "$SRV/EngineX/www-subject-tester/upload"
rm -rf "$SRV/EngineX/www-subject-tester/upload/"* 2>/dev/null

cd "$SRV"
ulimit -n 20000 2>/dev/null
./webserv EngineX/EngineX.conf > /tmp/webserv_stdout.log 2>&1 &
SRV_PID=$!
sleep 0.6
if ! kill -0 $SRV_PID 2>/dev/null; then
  echo "!! webserv failed to start"; cat /tmp/webserv_stdout.log; exit 1
fi

cd "$TST"
printf '%s\n%s\n\n\n\n%s\n14\n' "$SUITE" "$SEL" "$RET" | TERM=dumb ./servTester.out 2>&1 \
  | sed 's/\x1b\[[0-9;]*[A-Za-z]//g'

kill $SRV_PID 2>/dev/null
wait $SRV_PID 2>/dev/null
