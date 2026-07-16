# Security Policy

## Reporting a vulnerability

Please do not disclose suspected vulnerabilities in a public issue. Before publication, configure a private security-reporting channel in the GitHub repository, such as GitHub Private Vulnerability Reporting, and direct reports there.

A useful report includes:

- Affected commit or release
- Operating system, architecture, and compiler
- Minimal reproduction or malformed artifact
- Expected and actual behavior
- Security impact
- Whether the issue crosses an embedding, bytecode, JIT, AOT, or module trust boundary

## Security-sensitive areas

Pay particular attention to executable-memory transitions, bytecode and AOT validation, integer bounds, relocation processing, native branch fixups, stack maps, deoptimization reconstruction, GC rooting, runtime-helper dispatch, and generated-code cache lifetime.

## Supported versions

Until tagged public releases exist, only the latest default branch should be considered supported.
