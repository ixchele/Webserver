#!/usr/bin/env python3
import sys

# sys.stdin.read() without arguments reads until EOF (stdin closed)
body = sys.stdin.read()

print('Content-Type: text/plain')
print('Content-Length: ' + str(len(body)))
print()

sys.stdout.write(body)