# Copilot Instructions for Divide-Framework

Read and follow [`AGENTS.md`](../AGENTS.md) at the repository root for all shared agent instructions, repository conventions, build and validation guidance, and codebase references.

## Code Style
- Prefer avoiding component-specific explicit template instantiations in `SceneGraphNode.cpp`; colocate such instantiations with component implementation files or refactor factory calls instead.
