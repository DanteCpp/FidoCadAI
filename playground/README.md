# Playground

Normal workspace for using `fcai` / `fcai-gui` on real `.fcd` drawings
(schematics and PCB layouts). No structure is imposed — work here as you would
anywhere.

When something is wrong, surprising or missing, point an agent at the file(s)
that show it. The drawing is the reference; the agent decides what to change:
core, GUI, CLI, skill or docs.

## For agents

Treat files here as user-produced references, not fixtures. Inspect them
(`context` / `json` / `render` / `diff`) and fix what they demonstrate.
Smoke-test with the relevant `fcai` command and report back fast — leave the
full gate (`make test`, `make sanitize`) for pushes. When core behaviour
changes, add a minimised reproduction under `tests/fixtures/`; sync
`share/skills/fidocad/SKILL.md` when behaviour or workflow changes.
