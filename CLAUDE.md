# Bootloader

x86_64 BIOS bootloader built stage by stage (see README.md for the roadmap).

- Build: `make all`. End-to-end test: `make test` (scripts/test_boot.py drives QEMU
  over serial + monitor; must stay green before every commit).
- Docs: one deep dive per stage in docs/; update the matching one with code changes.
- Commits are authored by the repo owner only. Never add `Co-Authored-By` or
  any other contributor trailer to commit messages.

## Skill routing

When the user's request matches an available skill, invoke it via the Skill tool. Route only to skills in the session's available-skills list; answer directly for quick questions or small scoped edits.

Key routing rules:
- Product ideas/brainstorming → invoke /office-hours
- Strategy/scope → invoke /plan-ceo-review
- Architecture → invoke /plan-eng-review
- Design system/plan review → invoke /design-consultation or /plan-design-review
- Full review pipeline → invoke /autoplan
- Bugs/errors → invoke /investigate
- QA/testing site behavior → invoke /qa or /qa-only
- Code review/diff check → invoke /review
- Visual polish → invoke /design-review
- Ship/deploy/PR → invoke /ship or /land-and-deploy
- Save progress → invoke /context-save
- Resume context → invoke /context-restore
- Author a backlog-ready spec/issue → invoke /spec
