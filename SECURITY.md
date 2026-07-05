# Security Policy

## Reporting a vulnerability

If you find a security issue in NyxOS, please report it privately:

- **Discord:** `uselessalter`
- **Email:** nyxos@inbox.lv

You can expect an acknowledgment within 48 hours. We'll work on a fix and
coordinate disclosure once it's ready.

## Scope

NyxOS is a hobby operating system — there is no sandbox, no privilege
separation beyond ring 0/3, and no security guarantee. That said, please
report crashes that originate from untrusted input (network packets,
malformed ELFs, filesystem images).

## Supported versions

Only the latest release on the `master` branch receives security fixes.
Older versions are not maintained.
