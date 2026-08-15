#!/usr/bin/env python3

body = 'CGI-GET-SENTINEL'
print('Content-Type: text/plain')
print('Content-Length: ' + str(len(body)))
print()
print(body)
