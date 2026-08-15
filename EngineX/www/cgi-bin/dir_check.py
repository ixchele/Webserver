#!/usr/bin/env python3

with open("data.txt", "r") as f:
    body = f.read().strip()

print("Content-Type: text/plain")
print("Content-Length: " + str(len(body)))
print()
print(body, end="")