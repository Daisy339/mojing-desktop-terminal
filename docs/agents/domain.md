# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

## Before exploring, read these

- `CONTEXT.md` at the repo root
- `docs/adr/` for architecture decision records related to the area being changed

If either location does not exist, proceed silently. Do not flag the absence or suggest creating it upfront.

## File structure

This repo uses a single-context layout:

```text
/
|- CONTEXT.md
|- docs/adr/
|  |- 0001-example.md
|- src/
```

## Use the glossary's vocabulary

When your output names a domain concept, prefer the terminology defined in `CONTEXT.md`.

## Flag ADR conflicts

If your output contradicts an existing ADR, surface that conflict explicitly instead of silently overriding it.
