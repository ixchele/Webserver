#!/usr/bin/env python3
import sys
import os

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("=== ENV VARIABLES ===\n\n")

for key, value in os.environ.items():
    sys.stdout.write(f"{key} = {value}\n")

sys.stdout.write("\n=== TEST VARIABLES ===\n")
methode = os.environ.get("REQUEST_METHOD", "none")
query = os.environ.get("QUERY_STRING", "none")

sys.stdout.write(f"Methode : {methode}\n")
sys.stdout.write(f"Query : {query}\n")
