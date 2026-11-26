#!/usr/bin/env python3
"""Check that the build tools are available."""
import os
import shutil
import sys

missing = [t for t in ("cmake", "ninja", "clang") if not shutil.which(t)]
for tool in ("cmake", "ninja", "clang"):
    print(f"{tool:8}: {'missing' if tool in missing else 'found'}")
print(f"Python  : {sys.version.split()[0]}")
print(f"NDK     : {os.environ.get('ANDROID_NDK_HOME', 'not set (needed for Android only)')}")
sys.exit(1 if missing else 0)
