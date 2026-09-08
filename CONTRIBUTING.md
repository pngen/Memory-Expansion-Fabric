# Contributing to Memory Expansion Fabric

Thank you for contributing. This repository is licensed under the Apache
License 2.0; by contributing you agree that your contributions are offered
under those terms.

## Getting started
1. Fork and clone the repository.
2. Build with CMake (see README). Keep first-party code warning-clean:
   - MSVC: /W4 /WX.
3. Run the test suite before and after changes.

## Guidance
- Keep the systems boundary narrow. Memory Expansion Fabric owns the generic
  expanded-memory control plane; do not pull in malloc semantics, GPU memory
  service, transfer-fabric execution, CXL protocol, RDMA, or storage paging.
- Preserve the explicit lifecycle / capability / evidence / authority model.
  An UNKNOWN capability must fail closed; a stale generation must be rejected.
- Add or update tests for any material contract you touch. Tests must complete
  naturally (no timeouts, no watchdog passes).
- Do not add telemetry. This project transmits nothing.
- Do not add AI attribution or Co-authored-by trailers.

## No CLA
No Contributor License Agreement is required.

## Reporting issues
Open an issue describing the contract you believe is violated and the minimum
reproducing input. A hanging test or a warnings build is a defect.
