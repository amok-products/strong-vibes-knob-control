# Security Policy

This firmware controls a **physical motor** in an intimate-use device. Security
and safety issues can have real-world physical consequences. Please report them
privately.

## Reporting a vulnerability

**Do not open a public issue for security or safety vulnerabilities.**

Report privately via one of:
- GitHub's private vulnerability reporting ("Report a vulnerability" under the
  repository's **Security** tab), or
- email **security@europemagicwand.com**

Please include: affected version/commit, hardware revision if known, a
description, and reproduction steps. We aim to acknowledge within 5 business
days.

## Scope

In scope: motor-drive safety (unexpected/unbounded actuation), BLE handling
(unauthenticated control, spoofing, malformed-packet handling), OTA/update
integrity, and secret handling.

## Disclosure

We follow coordinated disclosure. Please give us reasonable time to ship a fix
before any public write-up.
