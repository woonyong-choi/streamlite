Runtime Contract:
- Resolve the active goal through goal-current before choosing work.
- If this session created goal-current or goal-N files, stop with exactly GOAL_INIT_DONE.
- At the start of every session, read input.md, plan.md, and tasks.md in full.
- Persist and follow `Language policy: zh-CN` or `Language policy: en-US` for user-facing text.
- Before task, checkpoint, or final-review work in a git repository, block on unrelated dirty worktree changes.
- Execute only the first incomplete task or required checkpoint.
- Commit code changes at the task boundary when the project is a git repository.
- Commit checkpoint-only tracking updates as `goal-N checkpoint after task M: complete` when the project is a git repository.
- Commit final-review-only tracking updates as `goal-N final review: complete` when the project is a git repository.
- If repository hooks reject a literal goal-mode commit subject, use a repo-compatible subject and preserve `Goal-mode boundary:` in the commit body.
- After any task-boundary commit, task commit skip, or task commit failure, stop immediately; checkpoint and final review must wait for a later session.
- Commit-status text in tasks.md must remain true after the commit; do not commit pending, ready-to-commit, or to-be-created wording.
- Do not ask the user questions; record assumptions and continue safely.
- Before closing a task, verify with concrete evidence.
- Update this tasks.md with work, evidence, risk, and next step.
- Keep each task record compact with a short evidence digest; reference commands, commits, files, and risks instead of pasting long logs.
- Briefly report, then stop so the client can auto-advance.
- If a Red Flag appears, stop task execution and repair the workflow state first.

Goal status: active
Language policy: en-US
Commit policy: commit code changes at the task boundary when the project is a git repository

## Initialization only — 2026-10-24

- Work: recovered all original user messages and later assistant prose through the supplied full handoff; compared latest scope with existing continuity, renewal documents, main history and live #257. Created only goal tracking files.
- Evidence: handoff and export SHA-256 matched user-supplied hashes; handoff read through line 2161; initial `git status --short` empty; main `6bb8b2e8`; no prior goal-current/goal directories; `.local/decisions/embedded-stream-renewal.tsv` has analysis and brand entries; #257 OPEN with unchecked gates.
- Risk: analysis/brand completion is not engine delivery; no task validations performed. Markdown Notes Agent and built-in goal/task tools unavailable; no Vault write attempted. Historical worktree owners must be preserved.
- Next: Task 1 only in the next continuation, with reread and dirty guard. Initialization leftovers are the permitted dirty-state exception.
- Commit status: no commit attempted; initialization-only boundary, as required.
- AAR: the oldest successful summary contradicts later user decisions. Source precedence is latest user request → actual files/evidence → continuity; never restore old ToteDB name lock or indefinite writer deferral as the current goal.

## Resumption check — 2026-10-24

- Resumed the existing goal in the user's ordinary session and requested checkout; no new goal, worktree, agent, import or native replay. Read input.md, plan.md and tasks.md fully before work. Read the 2,161-line handoff in consecutive ranges 1–400, 401–800, 801–1200, 1201–1600, 1601–1950 and 1951–2161; archived skill-output lines have historical clipping, and the live goal skill/reference were read in full. Both recorded SHA-256 values match. A read-only JSON check confirms all 59 original user-message texts from 994 messages occur verbatim in that handoff. Checked the conversation's latest request and the 14-message recovery export, whose patch and GOAL_INIT_DONE match the existing initialization files.
- Applied workspace AGENTS.md, mandatory Vault index/project guidance and shared career guide. No category, repository or nested AGENTS.md exists in this checkout. The historical lrn-sql index does not register this current path. No Vault note was written; the latest user explicitly authorizes repository work and goal tracking without Markdown Notes Agent. Goal/task tools are not exposed, so this remains a file-based workflow with no installed unattended continuation mechanism.
- Initial `git status --short` contained only `?? goal-1/` and `?? goal-current`, the allowed initialization leftovers. `git fetch origin --prune` kept origin/main at `6bb8b2e8`; main and other worktrees were not modified. #257 is OPEN, assigned to the authenticated user, with three unchecked conditions; no matching branch or open PR existed. Work uses `docs/257-docs-cow-decision` in this same checkout, as explained in plan.md.
- Checklist for this boundary: recover scope/ownership; trace Task 1 claims to original evidence; edit the existing rationale; validate, update tracking and commit Task 1 only. Previously completed demand analysis, release and benchmark campaigns are not repeated. Contracts, experiment criteria, follow-up issues and engine implementation remain separate work.

## Task 1 — Preserve the COW decision and possible withdrawal rationale

- Status: complete (documentation and static source verification only)
- Scope: update existing approved development/renewal material for blog reuse, not a new blog publication. Separate earlier WAL removal, H2/H3 withdrawal, retained COW and hypothetical future COW replacement. Identify evidence, tradeoffs, unmet criteria and what remains unmeasured.
- Work performed: added D52 to `docs/development-report.md` and linked it from `docs/design/streamlite.md`. Distinguished the historical statement-WAL removal, later one-sync optimization, H2/H3 withdrawal, retained single-writer COW and proposed buffered-COW/WAL comparison. Preserved all three product features and adoption requirements, safety/performance tradeoffs, unmeasured budgets and the evidence/costs to record if COW is actually replaced. Replaced the stale indefinite-writer-deferral summary with the current #257 review. Updated only existing Continuity plus explicitly approved goal tracking; no engine, API, format, durability, release or benchmark changes.
- Verification evidence: `python3 /Users/woonyong/.codex/skills/repo-docs/scripts/check_doc.py docs/development-report.md docs/design/streamlite.md` and `git diff --check` pass. Read-only Python added-link check passes all 16 relative links/anchors and verifies referenced commit objects. `git diff e49a3d2f 6bb8b2e8 -- src include python` is empty, so the prior report's product-source baseline still matches. `git diff --exit-code -- docs/experiments README.md README.en.md src include python bench tests .github Makefile` passes. Goal-state/scope assertions pass: D01–D52 are ordered, only Task 1 is complete, main is preserved and only the six intended public/tracking files enter this boundary. The first heading check wrongly compared padded IDs as strings; integer normalization corrected the checker and the full rerun passed. No product tests/builds or benchmarks were rerun for this documentation-only boundary.
- Claim-to-source review: WAL rationale comes from `git show d137130 --format=fuller --stat`, not inferred speed; the mutable #9 comment now only records removal. Current writer lock/end are `src/sql/database.c:1163–1172,1229–1251`; statement finish precedes stage adoption at 718–755; clone encode/decode is `src/storage/catalog.c:249–257`. Fresh versus fallback reclamation and conditional sync/UNKNOWN are `include/storage/page_file.h:183–243`. The committed H2/H3 JSON reports cumulative 5-pair medians 0.9999303272 [0.9544115115,1.0109332554] and 1.0022001208 [0.9774942903,1.0367110895], confirming lack of cumulative adoption evidence, not correctness failure or COW abandonment. Existing renewal report sections 6–8 and live #257 keep COW/WAL trials unimplemented; official SQLite WAL and PostgreSQL 18 WAL pages were checked for one-writer and WAL-before-data claims. Full added diff reviewed against these sources; no measured winner is claimed.
- Remaining risk: neither COW nor WAL multiwriter candidate has been implemented or compared; numerical workload/tail/resource budgets and failure contracts are not fixed. Existing fault-test reports do not prove physical power-loss safety. Blog reuse must retain these limits and distinguish current interpretation from original intent. Local commit is not main integration, publication or engine delivery.
- Next step: stop at this boundary. The next continuation performs Task 2 only: proposed concurrency/resource contracts. Do not execute Task 3, checkpoint, final review, push/PR/merge or deploy after this task commit. Any later publication must review the private paths/verbatim recovery material in the approved goal files before making them public.
- Commit record: task-boundary subject `goal-1 task 1: Preserve the COW decision and possible withdrawal rationale`; body marker `Goal-mode boundary: goal-1 task 1: Preserve the COW decision and possible withdrawal rationale`. This record and the initialization files belong to that boundary; `git log -1 --format=%s%n%b` is the commit evidence. Main remains at `6bb8b2e8`.
- AAR: a mutable historical issue comment is weaker evidence of the original rationale than the removal commit. Add no invented numerical rejection threshold; Task 3 must define it before comparisons. The old assumption that writer work stays indefinitely deferred is superseded by #257. Repeated trap avoided: treating completed research or withdrawn H2/H3 candidates as proof of engine completion or COW failure.

## Task 2 — Specify the proposed concurrency and resource contract

- Status: incomplete
- Scope: existing StreamLite design material; writer/process scope, isolation and conflicts, cancellation/UNKNOWN, ownership and memory bounds, retention, migration/fallback. Keep proposed guarantees distinct from current runtime.
- Work performed: not started.
- Verification evidence: required — scenario-to-contract matrix including disjoint rows sharing a leaf, unique/range conflicts, stalled writer, long reader, slow consumer, sync failure and uncertain response.
- Remaining risk: approval-dependent public/storage contracts must not be treated as implemented or approved defaults.
- Next step: after Task 1 boundary, formalize proposals and name unresolved gates without silently changing code.
- Commit status: no task commit attempted.

## Task 3 — Fix the separated comparison and adoption gates

- Status: incomplete
- Scope: existing renewal design/experiment material; common cache/workspace cost, buffered COW versus WAL/MVCC, SQLite product alternative and local JetStream with matched durability. Include cleanup/checkpoint/GC and negative results.
- Work performed: not started.
- Verification evidence: required — predeclared workload, controls, correctness gates, metrics, repetitions, stop rules and criteria; independently identify unapproved numerical budgets. No claim of a measured winner without execution.
- Remaining risk: unequal sync/retention/semantics or omitted maintenance can create false wins; no broad benchmark rerun is authorized merely to recover context.
- Next step: make experiment gates reviewable, then stop at the task boundary.
- Commit status: no task commit attempted.

## Checkpoint after Task 3

- Status: incomplete; separate continuation required.
- Work performed: not started.
- Verification evidence: inspect requirement drift, factual claims, docs/code agreement, security/durability, resource bounds, rollback and relevant build/test applicability. Only tasks.md may change during this checkpoint.
- Remaining risk: repairs outside tracking require a separately inserted task.
- Next step: tracking commit and stop; do not execute Task 4 in this checkpoint session.
- Commit status: no checkpoint commit attempted.

## Task 4 — Connect bounded follow-ups and reconcile #257

- Status: incomplete
- Scope: link independently verifiable implementation and validation gates to #257; update completion conditions only when evidence supports them. Reuse existing issues where applicable, preserve other owners, and leave actual engine work clearly outstanding.
- Work performed: not started.
- Verification evidence: required — issue links, current checklist state, and explicit completed-analysis versus proposed/unimplemented ledger.
- Remaining risk: expanding a decision issue into implicit blanket approval for rewrite, migration, deployment or another repository's changes.
- Next step: confirm dependency and approval gates, link follow-ups, record exact remaining work.
- Commit status: no task commit attempted.

## Final review

- Status: incomplete; separate continuation after all tasks/checkpoints.
- Work performed: not started.
- Verification evidence: required — latest request addressed, COW rationale source-backed, no invented winner/implementation/contribution, links and task commits verified, ownership and dirty state checked, no unapproved release/format/API changes.
- Remaining risk: completing these decision deliverables does not complete future cache/multiwriter/stream implementation.
- Next step: perform final-review-only tracking commit before setting goal complete.
- Commit status: no final-review commit attempted.
