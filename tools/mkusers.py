"""Writes /etc/passwd and /etc/shadow. These are demo accounts for the assignment.

Each password is stored as SHA-256 of a random salt plus the password, so the
real password is never on the disk. /etc/shadow is readable by root only."""

import hashlib
import os
import secrets
import sys

# name, password, uid, gid, home
USERS = [
    ("root", "root", 0, 0, "/"),
    ("student", "student", 1000, 1000, "/home/student"),
]

folder = sys.argv[1]

with open(os.path.join(folder, "passwd"), "w") as passwd:
    for name, _, uid, gid, home in USERS:
        passwd.write(f"{name}:x:{uid}:{gid}:{home}:/bin/sh\n")

shadow_path = os.path.join(folder, "shadow")
with open(shadow_path, "w") as shadow:
    for name, password, *_ in USERS:
        salt = secrets.token_hex(8)
        digest = hashlib.sha256((salt + password).encode()).hexdigest()
        shadow.write(f"{name}:{salt}:{digest}\n")

os.chmod(shadow_path, 0o600)
