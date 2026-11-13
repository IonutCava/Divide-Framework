# Copilot Instructions for Divide-Framework

Read and follow [`AGENTS.md`](../AGENTS.md) at the repository root for all shared agent instructions, repository conventions, build and validation guidance, and codebase references.

## Code Style
- Prefer avoiding component-specific explicit template instantiations in `SceneGraphNode.cpp`; colocate such instantiations with component implementation files or refactor factory calls instead.
- When using `XML::detail::SerializerContext`, always use a small struct for serialized fields, even if there is only one field; apply this in both framework and ProjectManager code.

## Project-Specific Rules
- For project config XML handling, use `PROJECT_CONFIG_FILE_NAME` instead of hardcoded filenames.
- Require wrapped `config.*` paths (no legacy flat tags).
- Parse/write fields using `GET_PARAM`/`PUT_PARAM` style consistent with main `XML/config.xml` handling.
