#!/usr/bin/env python3
import os
print("Content-Type: text/plain\r\n\r")
print("Hello from the CGI script!")
print("QUERY_STRING=" + os.environ.get("QUERY_STRING", ""))
print("REQUEST_METHOD=" + os.environ.get("REQUEST_METHOD", ""))
