#!/usr/bin/env python3
import os
qs = os.environ.get("QUERY_STRING", "MISSING")
print("Content-Type: text/plain")
print("Content-Length: " + str(len(qs)))

print()

print(qs)