# Continuous integration

CI checks `master` and all open pull requests daily at 00:00 UTC (GitHub may delay scheduled runs).
Linux, Windows, and macOS builds run only for head commits not previously queued for that target.
Commit statuses record queued and completed builds, including on fork PRs; unchanged commits are
not rebuilt, even after a failed build. Failed dispatches are retried at the next daily check.
The first check builds every current target. PRs are built at their head commit, not a test merge.
The platform workflows use `workflow_dispatch` for the scheduler, not push or PR triggers.
These workflows must be present on the repository's default branch before scheduling takes effect.
