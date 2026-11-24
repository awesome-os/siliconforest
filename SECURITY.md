# Security

SiliconForest is experimental and unreleased. Its kernel is a development build without
several of the protections a shipping Darwin has (for example, code signing is not enforced
and the kernel text is not hardware-locked). Do not use it where security matters.

## Reporting a vulnerability

Please report privately through GitHub: on this repository, **Security → Report a
vulnerability** (GitHub's private vulnerability reporting). Do not open a public issue for
something that puts a user's machine or data at risk.

Useful in a report: what is affected (file, driver, tool), how to reproduce it, and what an
attacker gains. There is no bounty. You will get an answer as soon as the maintainer can
give one; this is a small project.

If the problem is in code that comes from another project (upstream PureDarwin, XNU, Linux, Mesa,
Firefox and the others in [`NOTICE`](NOTICE)), please report it to that project as well.

## Also worth reporting privately

Anything in the repository or its history that should not be public: a credential, a device
identifier, proprietary material.
