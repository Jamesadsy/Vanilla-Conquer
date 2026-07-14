# Vanilla Conquer — Codex Guidance

This repository uses a Brain/Hands workflow: **Brain** handles planning, investigation, and review; **Hands** handles implementation, builds, and tests.

## Local configuration

If `AGENTS.local.md` exists, read it for local paths and environment details. It is local-only configuration and **must never be committed**. If it does not exist and local paths are required to complete the task, ask the user for them.

## Start a task correctly

Read the relevant onboarding material and the latest matching handover before editing. Use this repository as the source of truth for code; onboarding material explains intent, not checkout state.

## Brain operating contract

1. Inspect the relevant onboarding material and repository without changing code unless explicitly asked.
2. Identify the game scope: **TD**, **RA**, or **Shared**.
3. Write a handover using the local handover location, named `YYYY-MM-DD_<scope>_<short-task-name>_handover.md`.
4. Include the goal, evidence, exact repository paths, constraints, implementation steps, validation commands, and any required target test or data environment.
5. Do not report a task as ready for Hands without a handover file.

## Hands operating contract

1. Verify repository state with `git status --short --branch`; preserve unrelated changes.
2. Implement only the accepted handover scope. Keep TD and RA paths separate unless the handover says the change is shared.
3. Run the specified validation.
4. Write a completion report alongside the handover: `YYYY-MM-DD_<scope>_<short-task-name>_completion.md`. Include changed files, commands run, outcomes, and unresolved issues.

## Handover template

```markdown
# <Scope> — <Task>

## Goal

## Evidence and context

## Repository locations to change

## Constraints / do not change

## Implementation plan

## Validation

## Target environments
- Game scope: TD | RA | Shared
- Test install: <path or not needed>
- Data master: <path or not needed>

## Acceptance criteria
```

## Conventions

- Use `TD`, `RA`, or `Shared` in handover filenames and headings.
- Treat operational test installs and data masters as separate assets. Do not modify them unless the task expressly requires it and the required local location is available.
- Place generated artifacts and human handovers/reports only in their designated local locations.
