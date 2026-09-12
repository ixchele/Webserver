#!/bin/bash
# Usage: run_suites.sh "<main-menu selection>"   e.g. "1 2 3 4 5 6 7 8 9 10 12"
# Multi-selection on the main menu runs each chosen suite end-to-end.
set -u
ROOT=/home/aazzaoui/Downloads/aa
SRV=$ROOT/Webserver-main
TST=$ROOT/web-serv-Tester-main
SEL=$1

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
kill -0 $SRV_PID 2>/dev/null || { echo "!! webserv failed to start"; cat /tmp/webserv_stdout.log; exit 1; }

cd "$TST"
printf '%s\n\n14\n' "$SEL" | TERM=dumb ./servTester.out 2>&1 | sed 's/\x1b\[[0-9;]*[A-Za-z]//g'

if kill -0 $SRV_PID 2>/dev/null; then
  echo ">> webserv still alive at the end"
  kill $SRV_PID 2>/dev/null
else
  wait $SRV_PID; echo ">> webserv EXITED EARLY with status $?"
fi
wait $SRV_PID 2>/dev/null
