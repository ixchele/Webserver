#!/usr/bin/python3
import sys
sys.stdout.write("Content-Type: text/html\r\n")
sys.stdout.write("\r\n")
sys.stdout.write("<html><body><h1>Hello from CGI</h1></body></html>")
sys.stdout.flush()
