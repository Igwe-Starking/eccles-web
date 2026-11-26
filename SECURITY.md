# Security Policy

ECCLES WEB parses pages and runs their scripts. Privileges, fuel limits and fixed memory budgets restrict
what a page can do, but the engine has **not** been fuzzed or audited. Do not treat it as a hardened
sandbox for hostile content.

- Grant `ECCLES_WEB_PERM_HOST` only to pages you trust.
- Privileges restrict the page. Host C code calling the API is trusted.
- Your network backend is responsible for TLS and certificate checks.

## Reporting a vulnerability

If a page crashes the host, reads or writes out of bounds, or bypasses a privilege or the fuel limit,
please report it privately through GitHub's **Security → Report a vulnerability** on this repository.
Include the page, the build configuration and the platform.
