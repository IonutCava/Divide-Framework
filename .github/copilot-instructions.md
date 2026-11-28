# Copilot Instructions for Divide-Framework

Read and follow [`AGENTS.md`](../AGENTS.md) for shared project conventions, documentation, and validation guidance.

For files covered by `.github/instructions/*.instructions.md`, follow the applicable task-scoped guidance and linked architecture guides.

## Cost-conscious engine workflow

Use the `architect` agent once for a coding task only when the root cause is unclear or the work crosses systems, has significant lifetime, concurrency, data-flow, or API implications, or needs a design decision. This applies across engine areas such as rendering, audio, physics, input, gameplay, and asset processing.

For clear, localized changes, skip the separate planning pass and implement directly. When using the architect, ask for a concise, evidence-based spec with relevant paths, root cause, implementation steps, and focused validation. Hand that spec and only the necessary context to the `backend-worker` agent. Do not add parallel agents or duplicate review passes unless they address an independent question or a concrete blocker.
