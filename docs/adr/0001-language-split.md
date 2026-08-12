# ADR-0001: C++ desktop and Python evidence service

Status: accepted; authority boundary refined by [ADR-0006](0006-authoritative-analysis-backends.md).

The repository began with a React UI and Python engineering calculations. The target architecture assigns the desktop, confirmed project state, planning, applicability, result validation, coverage, and findings to C++20/Qt 6. Python is limited to artifact acquisition, parsing, and candidate evidence packaging.

Program 01A retires the Python calculation routes rather than preserving them as a second verdict path. `backend/app/physics.py` remains only for historical equation comparison until Program 01B makes reviewed execution packages drive C++ calculations. The React UI remains a non-executing archived viewer.
