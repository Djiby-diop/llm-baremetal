# Security Policy

## Reporting a vulnerability

If you believe you have found a security vulnerability in this repository:

- Please **do not** open a public issue.
- Prefer a **private disclosure** using GitHub Security Advisories:
  - Repo → **Security** → **Advisories** → **New draft security advisory**.

If you cannot use GitHub Security Advisories, open a minimal issue that contains **no exploit details** and ask for a private channel.

## Scope

This repository includes:

- UEFI firmware code (C, freestanding)
- Host tooling (Rust) such as `oo-guard` and OS-G tools
- Build and image creation scripts

Security-sensitive areas include: parsing, filesystem reads, memory allocators, and any command execution surfaces (`/oo_*`).
