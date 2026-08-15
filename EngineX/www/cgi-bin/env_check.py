#!/usr/bin/env python3

import os

line = 'REQUEST_METHOD=' + os.environ.get('REQUEST_METHOD', 'MISSING')

print('Content-Type: text/plain')
print('Content-Length: ' + str(len(line)))
print()
print(line)