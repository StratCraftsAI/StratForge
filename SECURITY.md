# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.1.x | Yes |
| Older releases | Best effort |

## Reporting a Vulnerability

Report security issues privately to: `nonagonal.portal@gmail.com`

Suggested subject line: `[SECURITY] StratForge - brief description`

Include:

- A clear description of the issue
- Steps to reproduce
- Impact assessment
- Affected platform, compiler, and build mode
- Any proof-of-concept or sanitizer output that helps triage the report

## Response Timeline

- **Acknowledgment**: within 48 hours
- **Initial assessment**: within 5 business days
- **Fix or mitigation**: coordinated with the reporter as soon as reasonably possible

## Scope

This policy covers the public StratForge codebase, especially:

- Header-only library code under `include/stratforge/`
- Example and benchmark utilities when they expose unsafe parsing or filesystem behavior
- Test or sample data handling when malformed input can trigger memory safety issues

Typical concerns include:

- Memory safety defects
- Integer overflow or undefined behavior in calculations
- Denial-of-service conditions from crafted market data or malformed CSV input

## Disclosure

Please do not open public issues for security vulnerabilities. Use coordinated disclosure so fixes can be prepared before details are published.
