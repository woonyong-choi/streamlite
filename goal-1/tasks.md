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

## Audit preservation and current execution

Completed-entry prose is compacted during Task 16 after a full reread. The exact precompaction ledger is preserved in existing Git object `36529f6c57e08a04923df55f4a2c9765f7e85ff9:goal-1/tasks.md` (SHA-256 1813c082a787753bcc2215539f8352b3224430f05764ce6272bd411813915985). Use `git show 36529f6c:goal-1/tasks.md` and the unchanged section title for full work, commands, failures, limitations and historical next steps. This is a digest in the same approved tracking file, not a new context copy or rewritten history. At compaction, Task 6 source coverage/decisions/matrix and every incomplete entry remain verbatim; later execution updates only its own entry. The rejected earlier overall completion remains superseded. Current first incomplete boundary is the Additional checkpoint after Task23. The candidate-file durable recovery integration is complete at Task23;23a–r remain its prerequisite evidence. This closes the isolated owning recovery/query path, not product SQL/ACK activation, actual writers, live buffering/MVCC/GC/log reuse, stream integration or maintenance-inclusive COW/WAL measurements. The whole goal stays active. The checkpoint is a separate continuation after this task-boundary commit; Task24 follows that checkpoint. Existing runtime/default COW, data/ABI and all later requirements remain preserved.

Task 23d compacts only completed Task16–23c entries and their completed checkpoints after source review. Their exact full ledger remains in Git object `623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md` (SHA-256 31dd489d6de49399434544cbfa2353500aa28e3cb458536bc2fbfe5a1d14d280). Task6 source coverage/requirement matrix and all incomplete entries remain verbatim; no source history, failure artifact or requirement is removed. This adds no context copy.

Task 23g compacts only the completed 23d/e/checkpoint23e/23f records against Git `f0219294c5fc47a8cf312ea7b728a8dceb0bbd17:goal-1/tasks.md` (SHA-256 c2b59381598f3c179d9d785cda63ec682df8de7149863009fc19486c2eec6223). Task6 and all existing incomplete sections remain byte-identical. This is tracking maintenance within the allocation implementation boundary; no new context file or removed requirement.

Task23n compacts completed23l/m only against Git `c7d5333023e93fedb11c3395c78fbf469e00677c:goal-1/tasks.md` (SHA-256 3bc228f973e7d25cf346aa5812bff8264cc3fea19e6a18bb7305df072a85b467); Task6 and all25 old incomplete sections stay byte-identical. Original failures and full records remain in Git and existing execution artifacts.

Task23r compacts61 completed task/checkpoint records against Git `92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md` (SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066). A direct section comparison confirms Task6 and all26 previously incomplete entries remain byte-identical before23r closure. Full records, raw data and failure samples remain available in their original Git/execution locations. Only the current23r entry changes at this task boundary; the25 other incomplete entries, including original23, stay preserved.

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
- Work performed: added D52 to `docs/development-report.md` and linked it from `docs/design/streamlite.md`.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 2 — Specify the proposed concurrency and resource contract

- Status: complete (contract proposal and static review only; no runtime implementation or approval of defaults)
- Work performed: expanded `docs/design/streamlite.md` with a current-versus-proposed boundary table, separate same-process/multiprocess gates, serializable validation recommendation and snapshot alternative, logical rebase versus semantic conflict, cancellation handoff, durable/group outcome states, restart-query limits, ownership and aggregate byte budgets, pin/reclamation and retention pressure, migration and fallback.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 3 — Fix the separated comparison and adoption gates

- Status: complete (comparison/adoption design proposal and static validation only; no experiment collection or engine decision)
- Work performed: added E0–E7 to the existing StreamLite design: eligibility, cache, cache/workspace crossover, conditional SQL, payload/state layout, actual concurrent writers, matched COW versus WAL/MVCC, SQLite and local JetStream.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Checkpoint after Task 3

- Status: complete (review with one required documentation repair queued; no implementation validation)
- Work performed: previous turn is progress, verified by `a07f8732` and the Task 3 marker.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 4 — Restore bounded pending-query validation

- Status: complete (CP3-1 design coverage repaired; no memory measurement or consumer implementation)
- Work performed: added pending-list retrieval as a sixth workload, linked D51 and five source files, and specified Q1–Q5 for one-page metadata, one-page decoded payload, full sequential consumption, full materialization and failure/lifetime/state cases.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 5 — Connect bounded follow-ups and reconcile #257

- Status: complete (follow-up structure, issue reconciliation and documentation only; no approved contracts, engine implementation or new measurements)
- Work performed: created #260 StreamLite follow-up implementation with nine native subissues: cache #261, transaction workspace #262, independent writers #263, consumer streams #264, pending-query memory #265, COW/WAL total cost #266, SQLite/local JetStream alternatives #267, conditional claim/ACK #268 and optional queue initialization #269.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Prior final review — superseded by user correction

- Status: complete (the registered decision/documentation objective only; contracts remain proposals and future engine work remains open)
- Work performed: previous turn is progress, proved by 02dbcab7 and its Task 5 marker plus live follow-up issues.
- Remaining risk: G1–G5 approvals and numbers, real workload/collector environments, cache/workspace/conditional SQL/optional lifecycle, stream/multiwriter implementation, failure recovery and fair measured product benefits remain future #257/#260–269 work.
- AAR: a completed proposal objective can coexist with open product gates only when each unresolved gate stays explicit and linked.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `b5cad56650e82b106d9fcdd5548b330dff78917c`, `docs(docs): StreamLite 결정 작업 최종 검토 기록 갱신`; `Goal-mode boundary: goal-1 final review: complete`.
- Full record: preserved Git ledger above, same section title.

## Reopening correction — 2026-10-24

- The user explicitly rejects the prior overall completion and requests full conversation analysis plus present architectural inference. The prior audit/commits remain evidence of documentation work, not accepted completion of the original objective. Restore active state in this same goal-1 and preserve goal-current. No duplicate initialization, background session, history rewrite or Vault write.
- Initial checkout is clean at b5cad566. get_goal returns null after prior completion; create_goal registers this resumed objective, not another goal directory. The current task is Task 6 only; all following boundaries remain incomplete. Original unfinished scope will be restored from evidence, not narrowed to what already passes document checks.

## Task 6 — Recover full conversation and reconcile original scope

- Status: complete (conversation/scope recovery and static verification only; architecture choice, implementation and new measurements remain incomplete)
- Work performed: reopened this existing goal after the user's rejection of the prior narrow completion. Read the original DB conversation, related DB histories and later resumption records; reconciled requirements, approval changes, prior decisions and current source/issues. Changed only the three explicitly approved goal tracking files. No duplicate initialization, background delegation, public issue mutation, Vault write or product change.

### Source coverage and reproducibility

| Source | Reading coverage and boundary |
|---|---|
| Supplied handoff and original export | Handoff read through line 2161; both input.md SHA-256 values match. Export has 994 records: 59 user, 788 assistant, 45 system, 47 idle, 50 synthetic, three locations and two compactions. All 176 visible user/assistant messages are read: 88,867 prose characters, 101,227 characters including deterministic index/ID/role headers; coverage [0,101227). Reconstruct directly from export data.messages in original order using user text and assistant type=text content parts. |
| Archived review/context records | Entire non-shell synthetic stream [0,101557) read, including review results and restart context. Three location records and completed compaction 383 summary read; its old name/single-writer deferral is superseded. Compaction 992 failed with compaction.interrupted. Active-context JSON has 611 records, all IDs and visible text match the export; it adds no new visible requirement. |
| Execution evidence | Inventoried 1,201 tool records: skill 40, read 350, shell 572, glob 13, subagent 69, patch 65, execute 9, grep 72, question 2, websearch 3, webfetch 6. Inspected material edits/results and current artifacts; did not reread every duplicate shell/CI log, system tool catalog, compaction recent-tool copy or native transport/reasoning byte. 1,117 reasoning fragments were excluded from visible prose and never replayed. |
| Root OpenCode DB history | Read all visible prose in ses_ead8791c7ffeQ5sV0c2JNUBcpr (9 records), original ses_ead84ac28ffeMw744QnRXnNA0x (994), ses_e95b7bce5ffdHxQuYRxuulsny6 (14), and subsequent ses_e95a3f090ffdQocRJlocDQNbqo (45). Source: read-only SQLite session_v2/session_message in /Users/woonyong/.local/share/opencode/opencode.db. Later Task 1 committed d862fd62; last Task 2 patch failed, so it is not engine implementation evidence. |
| Renewal analysis children | All visible prose read in architecture ses_e9839bf1cffevbqkZLjOrW3Uxk (14 records) and synthesis ses_e9831f95dffe3zTZLyztBQKwmP (18). Their starting prompts already prescribed considering/phasing A+C first; the resulting conditional A+C recommendation is not independent comparative proof. |
| Codex ToteDB | 01a149bc-1dcb-7f33-8df2-979efde662a6: 27 turns / 187 visible items / 60,820 reading-stream characters, all read. |
| Codex SQL | 01a12fb7-7c0d-7f43-9dce-549efd5d6f06: 235 turns / 1,008 visible items / 479,634 characters, all read. |
| Codex SQL Astra Control | 01a13baa-fef7-7631-b7cc-ab6b7c13f60f: five turns / 38 visible items / 12,075 characters, all read. Archived 01a13b29-be7e-7e21-a086-1ce25d9abe95: six turns / 60 visible items / 22,012 characters, all read. Historical delegation/reset commands are evidence, not new authorization. |
| Earlier Codex repository discovery | Archived “내 레포에서 sql 레포 찾아”, 01a10fec-ad84-7560-8fa5-ce68f7df16b5: 15 turns / 138 visible items / 50,422 characters, all read. Explains original PG scope and issue creation before its later replacement. |
| Consumer origin | Related 옵시디언 01a12c18-903b-72d0-8ace-d9ba7ca1b968: retrieved 170 turns to locate DB origin; read selected DB-origin turns 01a13722, 01a13727, 01a1373f and 01a1375d, including the single-process idempotency correction. Remaining unrelated Wiki/LLM conversation is not claimed fully read. Single-process intake serialization and restart reconstruction are distinct; separate-process broker operation was originally rejected as excess burden. |
| Discovery limits | Current list_threads and archived metadata through ten pages/500 archived entries, reaching July 2026 before this recovered SQL sequence, located the five DB chats above. This is the recoverable DB corpus, not a claim to inaccessible/deleted conversations or every unrelated chat. Provider hasMore=false for each fully read DB chat. Sources remain in original stores; no context copies were created. |

The older SQL-cost Vault note was requested read-only through Markdown Notes Agent after list_vaults. read_document returned “This note is ignored or is a rule file”. Its current body was not read, no ignore rule was bypassed and no Vault write attempted. Historical quotations and live #137 comment 6202455142 independently establish the earlier decision below; inability to read that note does not block repository work.

### Recovered decisions, approvals and contradictions

1. The user accurately remembered an earlier rejection. SQL turn 01a1415d-70df-72e0-9a9f-833d4f99fb67, item msg_0f15d1543873b7c4016ad142e7484c8191acc4d15a7b34e707 explicitly states: “현행 COW+SQL 재실행 WAL은 앞으로의 기본 커밋 방식으로 채택하지 않습니다.” Live #137 comment 6202455142 corroborates removing that combined path from the default candidates and evaluating incremental COW versus real page WAL separately. Later SQL turn 01a1417a leaves final alternative selection unresolved. This is neither no prior rejection nor proof that every COW design was abandoned/WAL implemented. The resumed D52/#257 summary omitted this distinction. Corrected the earlier denial to the user immediately.
2. Date separate changes: old combined-path rejection → actual SQL replay-WAL removal d1371303 → conditional-sync change e3023e79 → latest StreamLite renewal. Current page-COW source is not the old COW+SQL replay-WAL combination. H2/H3 were withdrawn for inadequate adoption evidence, not correctness failure or wholesale COW rejection. D52's “COW not withdrawn” requires this qualification in Task 8.
3. SQL turn 01a140b4-4738-7200-95ae-bf2e6741aba2 rejects doing all work then discovering a hot-row conflict and rolling back everything as the recommended default. Restore early row/key write ownership, bounded waits, deadlock/cancellation, re-evaluation and same-leaf lost-update handling to the architecture review. Serializable read/range validation is a separate requirement; Task 2's optimistic proposal is not user acceptance of late abort.
4. Early user forbade code changes, later expressly authorized independent DB/optional ACK implementation and SQLite comparisons; original message 92 also authorized hypothesis-by-hypothesis implementation/testing. Old terminal reset/parallel ownership instructions are not authority to restart those sessions. Old main integration/release/cleanup requests are historical, not fresh publish/delete authority here.
5. PG full compatibility was explicitly replaced by independent memory-efficient DB plus optional ACK (#139). Current #160–163 are closed with implementation evidence; #164 and #185 are closed as outside the revised scope, comments 6205499414/6205500099. Do not resurrect all PG commands, ROW_NUMBER or the old external outbox API merely because they appeared in older chat. Required local stream semantics and SQL transaction coupling remain distinct.
6. Original messages 243–272 discussed then temporarily deferred independent writers for single-writer evaluation. Messages 895 and 897 later request renewal satisfying SQL/stream/ACK with actual multiple writers and a present performance/safety judgment. Messages 788/794 had already rejected vague source-only uncertainty. A future comparison plan cannot substitute for the judgment requested now. Message 983 requires why any COW replacement is chosen for later blog reuse; 986/988 and the subsequent resumption “그럼 계획 마자해야지! 검증까지 해야하고” reject premature completion.
7. SQL candidate scoring/6,144 enumerated combinations and the primed A+C synthesis are analysis inputs, not measured optimality. Existing 420-run/280-run research, release and H2/H3 evaluations are preserved completed work; do not repeat them to manufacture recovery progress. New backend cost attribution and independent-writer/product validation remain unperformed.

### Requirement-to-current-state reconciliation

| Recovered requirement | Current evidence/state | Remaining work |
|---|---|---|
| Present architecture recommendation, including why any prior path is rejected | Exact SQL decision above; original 897; current #257 lists alternatives, D52 remains conditional | Task 7 must choose now using source/workload/resource/recovery reasoning. Task 8 records chosen/rejected reasons, falsifiers and implementation limits. No invented speed ratio or measured winner. |
| Truly independent writers, safe and fair, without default late conflict loss | database_work_begin at src/sql/database.c:1163–1172 holds exclusive lock until database_work_end:1229–1251; #238/#263 OPEN | Early conflict policy, same-leaf/root merge, isolation, bounded waits, allocation/rollback/reclamation, multiprocess death, durable/group/UNKNOWN and restart evidence. Proposed scenarios are not passing tests. |
| SQL, ACK and local replay/fanout stream independently and in one transaction | Existing DB/ACK implemented; StreamLite is display branding; #264 OPEN | Actual consumer/group progress, lease/fence, retry/failure/redrive, ordering, retention and pressure; preserve package/ABI/current DB and migration boundaries. No NATS wire/HA scope. |
| SQL preparation reuse and transaction-private accumulated changes | Parser binding ownership and statement finish/encode/write remain; #239/#261/#262 OPEN | Implement bounded template/binding cache and private workspace separately, then matched correctness/failure and four-condition cache/workspace evaluation. No unsupported H2/H3 readoption. |
| No queue/stream-specific unused-path cost; fewer normal claim/ACK execution boundaries | SQL core independent, Python constructor/close retain queue management; #268/#269 OPEN | Optional lifecycle and conditional SQL implementation/regression, full safety/fence/result ownership preserved; SQL count alone does not establish speed. |
| Original pending payload/memory problem and real caller behavior | persistqueue_compat.py:159–161 materializes all payloads; Markchain _pending reconstruction identified in original source review; #265 OPEN | Bounded result versus full accumulation, same fields/state/serializer/lifetime and backlog/payload/query axes; whole-app/native/Python/RSS accounting. Consumer source read only; no unauthorized Markchain modification. |
| Fair COW/WAL and product alternatives | #266/#267 OPEN; existing comparison reports are historical and lack new prototypes | Include COW rewrite/merge/pins/reclamation and WAL redo/writeback/checkpoint/version GC, cleanup-inclusive lifecycle, fairness/tails/resources/recovery with matched contracts. New measurements must confirm or refute the current design inference. |
| Original unfinished basic-key scale validation | Live #158 OPEN, implementation checked but 1k/10k/100k three-repeat criterion unchecked; comment 6233945394 reopens it because 100k timed out | Preserve failures and current implementation. Task 9 must retain this separate unfinished measurement, check existing compatible artifacts/ownership, and either perform the bounded missing validation or record a concrete unresolved limit. Other reports cannot silently close it. |
| Complete the authorized work rather than rename completion | #257 OPEN, one checked/two unchecked; #260–269 all OPEN, implementation absent from product source | Task 9 must split the restored execution queue into concrete one-boundary implementation/test/measurement tasks before final review. Issue creation, contract proposals and docs checks do not discharge execution. |

### Restored execution queue and closure boundary

The required queue after Tasks 7–9 retains separate cache (#261), workspace (#262), real writers (#263), streams (#264), pending memory (#265), storage total cost (#266), product alternatives (#267), conditional SQL (#268), optional lifecycle (#269), and #158's outstanding scale validation. Task 9 must order and split these into verifiable tasks with checkpoints, preserving completed baseline work and gating each implementation by the relevant resolved contract. Resolve reversible design/collection defaults explicitly; do not turn unresolved proposal labels into an indefinite administrative hold or silently invent approved SLOs. Comparisons require safe isolated test data and correctness first, not automatic live format cutover. No final review is eligible while required execution remains omitted/unproved.

- Verification evidence: both source hashes match; original/context ID and visible-text equality passes; complete reading cursors verified for the three large Codex streams, both additional DB streams and original/synthetic prose. Live GitHub reads verify #137/#139/#140, #158–164/#185, #238/#239, #257 and #260–269; selected historical comments corroborate decisions and deferred scope. git diff --check passes. Protected-path diffs against b5cad566 (all docs and goal-current) and 6bb8b2e8 (src/include/python/tests/bench/Makefile/.github/README/experiments) are empty. Main/origin-main remain 6bb8b2e8 and four other worktree heads are unchanged. Source/static checks confirm the writer lock and full-list path. No executable changed; no product build/test/benchmark was run or claimed for this recovery-only boundary.
- Remaining risk: unmeasured new architecture, current public rationale contradictions and unfinished execution remain explicit. Raw logs were not exhaustively rehashed; original raw/YCSB/failure data were not edited. Inaccessible Vault note remains unverified; archived earlier intent is not claimed as its current body.
- Next step: stop at this Task 6 boundary. Next continuation performs the checkpoint after Task 6 only, then Task 7 makes the actual architecture judgment. Goal remains active; do not complete it, execute Task 7 in this boundary, or publish anything.
- Commit record: subject `docs(docs): 이전 DB 대화와 미완료 목표 복구 기록 갱신`; body marker `Goal-mode boundary: goal-1 task 6: Recover full conversation and reconcile original scope`. The containing tracking-only commit records this audit under the configured user's identity; verify git log -1 and clean status.
- AAR: selected handoff prose was insufficient. Full earlier source exposed an actual default-path rejection, a rejected conflict policy, primed architecture analysis, a superseded PG scope and an open measurement hidden by older completion statements. Preserve those distinctions instead of defending the earlier narrow final review.

## Checkpoint after Task 6

- Status: complete (recovered-scope audit; required architecture/document repairs remain queued)
- Work performed: previous goal turn is progress, verified by 03f9573f, its Task 6 boundary marker and exact three-file tracking diff.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 7 — Make the present storage architecture judgment

- Status: complete (present architecture recommendation and source/document verification; no new backend implementation, measured superiority or product cutover)
- Current recommendation: page/redo WAL with a bounded buffer pool, MVCC and early key/range coordination is the recommended direction for the renewed engine.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 8 — Reconcile existing design and rationale with the judgment

- Status: complete (existing public rationale reconciled; no backend implementation, new measurement or product cutover)
- Work performed: updated D52 to state the present page/redo WAL, bounded buffer pool, MVCC and early-locking recommendation first.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 9 — Reconcile actual unfinished work and issue gates

- Status: complete (issue/rationale reconciliation and restored execution queue only; all new execution tasks remain incomplete)
- Work performed: updated only #238/#239/#257/#260/#263/#266 bodies to preserve the recovered early-coordination and old combined-path rejection, current WAL recommendation/falsifiers, G6 recovery obligations, common-cost follow-ups and prototype-before-comparison versus product-after-comparison distinction.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Checkpoint after Task 9

- Status: complete (scope/order/evidence review with a tracking clarification; no engine or measurement validation)
- Work performed: previous turn is progress, verified by c9db86a8, its Task 9 marker and exact three-file diff.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Execution closure rules after Task 9

This queue restores authorized work; it does not assert implementation, approved customer SLOs or measured benefit. Each task below is incomplete and must record its actual work, evidence, residual risk, next action and task-boundary commit at execution. Resolve its applicable contracts before changing behavior. Use reversible test defaults with reasons and falsifiers; do not wait indefinitely for an unnamed approval. A required failed test/measurement or unmet user outcome inserts a repair before dependent work, without replacing the original criterion or erasing failed samples. Candidate rejection can finish that experiment, not an unmet product requirement. Keep diagnostics and noninstrumented outcomes separate, and preregister collection before formal samples. Reuse existing approved docs and execution areas; no new context copies, consumer writes, production migration or publication is inferred.

GitHub's #266 → #263 edge is the comparison-result → product-integration gate. Tasks 22–30 implement and verify comparison candidates for #266 before that result, while Task 33 integrates the chosen product path. Relevant phase contracts and this prototype/product distinction must be reflected in actual issue readiness before code execution; an entire unpublished design issue is not an indefinite stop for authorized reversible local work. Do not claim remote issue closure while its PR/evidence conditions remain unmet.

## Task 10 — Fix the common execution and resource contract

- Status: complete (common contract selection, source/document verification and phase readiness; no cache/workspace/lifecycle implementation or new measurement)
- Work performed: added five common-contract sections and ten explicit input/oracle rows to docs/design/streamlite.md.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 11 — Implement bounded SQL template reuse

- Status: complete (local internal comparison implementation and regression; no measured adoption, public prepared API or new storage backend)
- Work performed: added parameter-origin metadata, binding-free syntax cloning and exact-SQL/version-keyed per-handle LRU for SELECT/INSERT/UPDATE/DELETE.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 12 — Implement optional queue lifecycle

- Status: complete (local Python lifecycle implementation and regression; no noninstrumented cost/adoption result or stream implementation)
- Work performed: declare the existing registry without allocating an instance value on SQL-only construction; create it at first explicit Queue registration through instance-dict setdefault.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Checkpoint after Task 12

- Status: complete (common-contract/cache/lifecycle audit; documentation consistency repair queued, not engine or performance completion)
- Work performed: previous turn is progress, verified by 3adb24ba, its Task 12 marker and eight-file scope.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 12a — Reconcile cache admission and implementation status

- Status: complete (documentation and issue-summary repair only; no runtime/default change or new measured effect)
- Work performed: corrected the older design sentence that could imply parser execution after rejected storage.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 13 — Implement transaction-private accumulated changes

- Status: complete (local default-OFF workspace implementation and regression; no noninstrumented effect/adoption, new backend or actual independent writers)
- Work performed: added per-work private page images and private catalog reuse behind TXN_WORKSPACE=0/1 and per-handle internal controls, default OFF.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 14 — Build and preregister common-cost comparisons

- Status: complete (collector, preregistration and controls only; no formal performance collection, adoption or new backend)
- Work performed: added optimized native driver, Python lifecycle/queue worker, fixed JSON config, registration/collection/verification and paired BCa analysis.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Checkpoint after Task 14

- Status: complete (review with a required collector repair before formal collection; no engine or performance completion)
- Work performed: previous turn is progress, verified by 28d68e3f, its Task 14 marker, eleven-file scope and ce7a9f46 parent.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 14a — Repair common-cost campaign deadline and reporting

- Status: complete (collector deadline/reporting repair and controls; no formal effect collection, adoption or new backend)
- Work performed: added a single monotonic deadline and remaining-time worker bounds for fixture creation and actual native/Python workers.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 15 — Collect and judge cache and workspace effects

- Status: complete (preregistered E1/E2 collection and hold disposition; no default adoption, new backend or product integration)
- Work/evidence: build/task15-cache-formal.registration.json pins plan/head 9455208029013751cb74161f328f7814d0a8e658, unchanged config/product source/optimized OFF+ON binaries/environment and exact 576-case schedule; control=false and registration SHA-256 c01613f810abc1442ea0b129c34c7bbfd6857346bfe2e37a8ab642efcd1a6f25.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Checkpoint after Task 15

- Status: complete (evidence/requirements review; bounded analysis-metadata repair queued, no new implementation or collection)
- Work performed: previous progress is 0c8b575c with the exact Task 15 marker, parent 94552080 and three-file scope.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 15a — Repair sample classification and historical analysis regeneration

- Status: complete (sample metadata and offline reproduction repair; no new formal effect collection or product/default change)
- Work performed: native and Python analysis schema 2 separately reports preflight/formal/diagnostic counts, retaining instrumented_records as diagnostic count.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 16p — Support indexed scalar-subquery equality

- Status: complete (general SQL prerequisite and regression only; original conditional queue integration and measurement remain incomplete)
- Work performed: database_expr.c reuses only an existing noncorrelated materialized scalar relation with one row/column, non-NULL value and exact column type.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 16 — Implement conditional queue updates

- Status: complete (local internal comparison implementation and regression; no formal effect collection, adoption or new backend)
- Work performed: 조건부 claim/ACK·실패 readback과 empty UNION 경계 수리, 기본OFF 유지.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Additional checkpoint after Task 16

- Status: complete (scope/source/evidence review; no new runtime implementation or formal measurement)
- Work performed: 15a/16p/16의 source·실패·자원·문서/범위 검토.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 17 — Measure conditional SQL and optional lifecycle

- Status: complete (fixed-plan formal collection, verified regeneration and hold disposition; no runtime/default adoption or backend implementation)
- Work performed: 사전 등록 lifecycle72/E3조건부48 전체 수집과 두 가설 보류 판정.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Additional checkpoint after Task 17

- Status: complete (scope, provenance and measured-disposition audit; no new runtime implementation or formal samples)
- Work performed: 고정 표본·pairing·판정과 과거 source 재분석 검토.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 18 — Build and preregister pending-query measurements

- Status: complete (로컬 수집기·사전 판정·정상/실패 대조만 완료; 정식 효과·소비자 교체·새 backend 미완료)
- Work performed: Q1–Q5 collector·고정 계획·실패/수명 control 구현.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Checkpoint after Task 18

- Status: complete (원래 범위·구현/실측 근거 검토 완료; CP18-1/2 검증기 수리는 미완료로 추가)
- Work performed: 자료 hash 집합 완전성 결함 CP18-1/2를 실제 반례로 확인.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 18a — Require complete pending-query artifact coverage

- Status: complete (로컬 pending 자료 coverage 수리와 회귀만 완료; 공통 수리·정식 효과·제품 전환 미완료)
- Work performed: pending 필수 자료·정규 경로·hash를 분석 전에 검증.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 18b — Require complete common-cost artifact coverage

- Status: complete (로컬 공통 자료 검증 수리·과거 재처리·연동 대조 완료; 새 정식 효과·제품 전환 미완료)
- Work performed: 공통 자료 coverage/gzip 역검증과 명시 과거 source 재현 수리.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 19 — Measure pending queries and the real caller boundary

- Status: complete (고정 계획의 전체 격자 수집과 읽기 전용 감사 완료; actual caller replacement, whole-app solution and new backend remain incomplete)
- Work performed: 1728case 정식 pending 수집·독립 판정과 원 consumer 읽기 전용 감사.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Additional checkpoint after Task 19

- Status: complete (범위·자료 완전성·재현·판정 감사; 새 구현·효과 표본이나 원 소비자 해결 완료와 구분)
- Work performed: pending raw/판정·누락 거부·재현·consumer 경계 검토.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 20 — Prepare the missing primary-key scale validation

- Status: complete (수집기·고정 계획·작은 대조 검증만 완료; Task21의 정식 규모 수집은 미완료)
- Work performed: 원 #158 seed137/120초 조건의 수집기·계획·작은 대조 구현.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 21 — Execute the retained primary-key scale validation

- Status: complete (원래 고정 규모9case의 로컬 정식 수집·검증 완료; 새 backend·제품 통합/발행과 구분)
- Work performed: 원 1k/10k/100k 각3회·120초 규모9case의 정식 수집 완료.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Checkpoint after Task 21

- Status: complete (로컬 범위·source·자료·재현·판정 검토 완료; 새 효과/제품 구현·원격 상태 확인과 구분)
- Work performed: pending/scale 자료·재분석·누락 거부와 범위 검토.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 22 — Specify WAL and version recovery contracts

- Status: complete (격리된 후보의 계약·대안·정적/제한 모형 검증 완료; 실제 엔진 구현·중단 복구·성능 미완료)
- Work performed: 불변 version·논리 abort·NO-STEAL·G2영속 결과·checkpoint/소유권 후보 계약.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23a — Implement segmented WAL framing and restart scan

- Status: complete (격리된 WAL transport 구현·실파일/byte 검증만 완료; 원래23의 거래/영속 결과 복구·제품 미완료)
- Work performed: 격리된 segmented WAL codec/append/force/scan/명시 tail 수리 구현.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23b — Validate transaction and physical-group WAL witnesses

- Status: complete (격리된 WAL의 의미 분석·실파일/독립 byte 검증 완료; 영속 본문·redo·복구 활성화는 미완료)
- Work performed: 읽기 전용 거래 chain/physical group/terminal witness 분석 구현.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Additional checkpoint after Task 23b

- Status: complete (22/23a/23b 검토와 실제 결함 재현 완료; CP23b-1 수리를 다음 과업으로 추가)
- Work performed: cwd 변경 뒤 다른 inode를 fsync하는 CP23b-1을 재현하고23c 추가.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23c — Bind WAL operations to the owned files

- Status: complete (후보 파일 소유권·barrier 결함 수리와 로컬 검증 완료; 거래 복구·제품 통합 미완료)
- Work performed: 디렉터리 fd와 segment inode/열린fd를 고정하고 누락·교체를 차단.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23d — Encode version pages and verify stored result bodies

- Status: complete (일반 page codec·소유 본문 검증·합성 WAL/page roundtrip 완료; ID 조회·일반 복구 미완료)
- Work/evidence: build/task23d-verification.json; codec34096/WAL866·독립 bytes/CRC/hashlib135·clang 검사 통과. 일반 page/소유 본문만 검증했다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23e — Store and look up bounded request outcome metadata

- Status: complete (bounded metadata codec·raw ID 조회와 합성 WAL/data 검증; 일반 복구·APPLIED 미완료)
- Work/evidence: build/task23e-verification.json; metadata50978/page34096/WAL892·실제133page/4group와4096record byte 대조. raw lookup은 APPLIED가 아니다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Additional checkpoint after Task 23e

- Status: complete (23c–23e의 코드·자료·계층 경계 검토; 원래23과 전체 목표는 미완료)
- Work/evidence: build/checkpoint23e-verification.json; 읽기 전용7조건43검사·27312artifact와9059tracked 보존. terminal 불일치여도 raw MATCH/body OK가 가능함을 확인했다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23f — Implement recovery control slots and durable anchor publication

- Status: complete (control codec·소유 파일·합성 의존성 anchor/내구 게시 검증; 원래23의 일반 복구는 미완료)
- Work/evidence: build/task23f-verification.json; control16978·독립16384B/16torn×5·실제종료/ACK후SIGKILL·정적검사. 옵션변경 이전source 실패도 보존했다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23g — Encode allocation snapshots and validate generation transitions

- Status: complete (type5 snapshot·소유 표·메모리 세대 전이 검증; 내구 system/일반 복구는 미완료)
- Work/evidence: type5 lifetime snapshot/atomic RAM transitions: native236823, independent8files/16archives; no durable issuance/recovery.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23h — Validate WAL system groups and recovery state transitions

- Status: complete (opt-in system/할당·예약·epoch witness와 metadata 대조; 내구 발급·일반 복구는 미완료)
- Work/evidence: opt-in system witnesses: native22236, legacy10237/result892, independent31frames/37archives; exact old format object restored, no general recovery.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Additional checkpoint after Task 23h

- Status: complete (23f/g/h component/source/evidence review; original23 and the full goal remain incomplete)
- Work/evidence: tracking-only review: exact4096success/4097LIMIT, native19781 and independentbytes;28787prior entries/22seals preserved, original23 remains incomplete.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23i — Read anchored checkpoint state and verify complete chunks

- Status: complete (실제 checkpoint 형식·control anchor 부분 검증·소유 snapshot; 일반 복구 미완료)
- Work/evidence: SLCP codec/owning reader: native2731, active64, independent15fixture/28file/18archive, peak426320B; 8B terminal 반례 수리 전후 보존. build/task23i-final-verification.json
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23j — Corroborate checkpoint state with retained WAL history

- Status: complete (checkpoint/보존 WAL의 읽기 전용 의미 대조; 일반 복구 미완료)
- Work/evidence: BEGIN cut/dirty group/terminal 이력 대조: native5368/용량58864, independent36fixture/52file/14archive, peak2108056B; 구 ABI 시험 연결 실패도 보존. build/task23j-verification.json
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23k — Implement bounded physical WAL redo

- Status: complete (격리 target의 image/delta 복원·재실행; 일반 복구 활성화 미완료)
- Work/evidence: bounded redo: native6925/5process 종료, independent3fixture/8file/25archive, workspace7866584B; 의미 view 반례 beforeOK7write1sync/동일 test object afterCORRUPT0write. build/task23k-verification.json
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Additional checkpoint after Task 23k

- Status: complete (23i/j/k component/source/자료 검토; 일반 복구와 전체 목표 미완료)
- Work/evidence: 실제8192page/128group·4096txn/96chunk/17segment의64MiB 복원과 재실행0write, native58084/independent35file;30347원artifact/26seal 보존. build/checkpoint23k-verification.json
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23l — Corroborate persisted outcomes with WAL and result bodies

- Status: complete (component implementation/real-file validation only; general recovery and product remain incomplete)
- Work/evidence: owning outcome/WAL/body corroboration: native2938/catalog50978, independent21fixture42file, peak3747808B; build/task23l-verification.json.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23m — Coordinate exclusive recovery and validate reconstructed state

- Status: complete (component implementation/real-file validation only; general recovery and product remain incomplete)
- Work/evidence: exclusive resolved-snapshot coordinator: native1051185, independent27fixture82file, same-object reentry before/after; build/task23m-verification.json, commit c7d53330.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23n — Durably resolve unfinished WAL transaction terminals

- Status: complete (WAL terminal 내구 정리·실파일/실제 중단 검증; 일반 복구 활성화와 전체 목표 미완료)
- Work performed: cleanc7d53330의 사용자 작성자·23m marker·10파일 commit 범위와 source/raw를 검증한 progress에서 시작했다. 원래23에 필요한 첫 내구 전이만23n으로 분리했다. 같은 data/WAL/control 소유권 안에서 실제 control/checkpoint·전체 semantic WAL을 검증한다. 미완료 physical group은 STALE로 차단하며 그 밖의 transport tail만 수리한다. prefix force→미완료 거래의 실제 first/last LSN·record 수·CRC를 담은 ABORT append→전체 force→재검증을 연결하고 실패 receipt는0이다. 변경 시 owner는 선택 anchor보다 커야 하며 WAL의 관측 owner 이상이어야 한다. 동일 owner 재시작은 완성 terminal을 중복 추가하지 않는다. COMMIT·data/control bytes는 보존하고 ready handle/결과는 반환하지 않는다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Additional checkpoint after Task 23n

- Status: complete (23l/m/n 검토·실제 용량/정합 반례 재현; CP23n-1 수리를 다음 과업으로 추가)
- Work performed: 직전717a6c38의 사용자 작성자·23n marker·11파일과 실제 source/raw를 검증한 progress에서 시작했다. 목표3파일·goal skill/reference·적용 지침을 다시 읽고 원래 Task6 요구, WAL 권고와 실제 구현/제품/실측 경계를 대조했다. ownership·입력 사본·OOM/partial I/O·barrier/재시작·WAL/data 순서·typed root/할당/terminal·보존/회수·롤백·문서/공개 기본 경로를 검토했다. 이 내부 native 경계에 별도 UI는 없다. 구현·plan·공개 문서·기존 자료를 고치지 않았다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23o — Reconcile aborted result fragments with persisted outcome semantics

- Status: complete (CP23n-1 결과 의미 수리·실파일/독립 검증; 일반 복구 활성화와 전체 목표 미완료)
- Work performed: clean e330570e의 사용자 작성자·checkpoint23n marker·tasks.md 단일파일과 원 source/raw를 검증한 progress에서 시작했다. raw witness의 중간 bytes/chunks/digest는 보존하고, codec이 검증한 정규 NULL/0/0/SHA-256(empty) ABORT만 결과 없는 final descriptor로 대조한다. 요청·admission·terminal 상태/끝·CSN 검증은 그대로다. non-NULL 보존 ABORT는 빈 응답을 포함해 전체 chunk/참조/digest/본문 대조를 유지하고 COMMIT에는 예외를 적용하지 않는다. 기존 설계·header·plan에 기본값과 제외 이유·물리 이력 검증 한계를 기록했다. 기존 시험은 부분 ABORT의 WAL-backed metadata 재구성을 다루지 않아 전용 시험을 추가했고 내부 Make module override로 옛 object를 보존했다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23p — Durably repair incomplete WAL physical tails

- Status: complete (미완료 물리 WAL tail의 내구 정리·실파일 검증; 일반 복구 활성화와 전체 목표 미완료)
- Work performed: clean38615869의 사용자 작성자·23o marker·7파일과 source/raw를 검증한 progress에서 시작했다. 원래23의 누락된 물리 tail 전이를23p로 분리하고 다른25미완료 섹션은 보존했다. 새 `candidate_recovery_repair_wal`은 같은 data/WAL/control 소유권 안에서 선택 checkpoint·전체 보존 WAL의 의미 검증이 읽은 실제 record를 SHA-256 chain으로 묶는다. 두 번째 transport scan의 동일 chain/끝/tail과 정확한 final PHYS_BEGIN/member만 확인한 뒤 최신 segment부터 제거·각 directory fsync, 시작 segment truncate/fsync, 보존 prefix 재분석·force, 정확한 ABORT append/force·재검증을 연결한다. 기존 terminal 전용 진입점은 미완료 그룹을 STALE로 계속 거절한다. 앞선 COMMIT·data/control과 기존 제품 기본 경로는 보존하며 receipt는 WAL 정리만 뜻한다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23q — Reconstruct complete retained request outcomes

- Status: complete (소유 결과표 재구성·미게시 encoding·실파일 검증; 일반 복구 활성화와 전체 목표 미완료)
- Work performed: clean7f55e7dc의 사용자 작성자·23p marker·12파일과 source/raw를 검증한 progress에서 시작했다. 기존 대조 계층은 오래된 표를 거절할 뿐 재구성하지 못하므로 원래23의 owning 재구성/encoding만23q로 분리했다. 같은 checkpoint-aware semantic scan에서 admission·chunk ref·WAL hash를 모으고 전체 forced prefix·완료 거래/group을 요구한다. 기존 terminal envelope는 엄격히 보존하고 빠진/ADMITTED nonempty COMMIT은 완전한 chunk·본문으로 복원한다. 새 ABORT는 정규 결과 없는 형태이며 손상된 보존 응답은 고쳐 숨기지 않는다. 빈 COMMIT의 빠진 참조는 STALE다. 최종 system/anchor epoch·floor·highwater를 대조하고4096개/4epoch·epoch당1024개와 공간 하한을 본문 읽기 전에 검사한다. 기존 catalog의 transaction ID 순서로 소유 descriptor를 반환하고 caller buffer에만 node/root를 인코딩한다. 기존 설계·plan과 별도checkpoint23q를 함께 갱신했으며 원래23과 다른25미완료 섹션은 보존했다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Additional checkpoint after Task 23q

- Status: complete (23o/p/q 계약·자료 검토와 실제 연결 반례 재현; CP23q-1 수리를 다음 과업으로 추가)
- Work performed: 직전dc1e188b의 사용자 작성자·23q marker·9파일과 source/raw를 검증한 progress에서 시작했다. 목표3파일·goal 규칙과 Task6 원 요구, WAL 권고/기존 COW 보존·총비용/제품 미완료 경계를 대조했다. ABORT 중간 조각/보존 응답, 같은 semantic view hash, tail 제거의 directory/truncate/force/재시작, 소유권·입력 사본·OOM·bounds·typed root/본문·보존과 rollback을 검토했다. 이 native component에는 UI가 없다. 구현·plan·공개 문서·기존 원자료와 continuity는 변경하지 않았다.
- Evidence/risk/AAR/next/commit: full unchanged record in `git show 92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md`, same section title; whole-file SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066. Historical boundary only; resume the current first incomplete task.

## Task 23r — Bind checkpoint-aware system history to the selected anchor

- Status: complete (CP23q-1 상태 연결 수리·실파일/독립 검증; 일반 복구 활성화와 전체 목표 미완료)
- Scope: CP23q-1의 선택 checkpoint BEGIN 이후 epoch/floor·transaction/page highwater와 첫 system 전이의 이전 값을 연결한다. 같은 checkpoint-aware witness를 쓰는 재구성·redo·terminal/tail 복구도 잘못된 전이를 파일 변경 전에 거절해야 한다. Issues: #266, #263.
- Work performed: clean92c7babf의 사용자 작성자·checkpoint23q marker·tasks.md 단일파일과 원 source/raw를 검증한 progress에서 시작했다. 선택 BEGIN의 기존 거래·dirty·system 대조 뒤 기존 system 숫자 필드를 anchor에 연결한다. 첫 REQUEST_EPOCH/ID_RESERVE의 이전 값과 이후 admission/metadata가 같은 상태를 사용한다. BEGIN 전 미래 admission과 미완료 요청을 넘긴 floor도 거절한다. 별도 상태표·가짜 event·새 소유 할당을 추가하지 않고 raw 의미·ABI·전체 보존 WAL 요구를 유지한다. 기존 설계·공개 header·plan을 맞췄고 재사용 fixture에 별도 상태 연결 시험을 추가했다. 사용자 후속 실행 지시는 plan에 지속하며 이번 경계 이전에 작성된 수리를 다른 실행자가 작성한 것으로 바꾸지 않았다.
- Counterexample evidence: `build/task23r-before-command.json`과 `build/task23r-after-command.json`은 같은 `build/task23r-state-test.o`를 서로 다른 module에 연결한다. 수리 전32998검사/4위반·후16594검사/0위반이며 정상2개는 유지하고 잘못된2개는 CORRUPT/out=NULL·projection 미생성으로 바뀐다. 최초 guard 버전과 실행물·원 test/source/object도 별도로 보존했다. 최종 실제 Make 명령은 `build/task23r-reviewed-command.json`, native는 `build/fault/task23r-state-reviewed.json`:17fixture/17438검사/0위반, checkpoint 사이 전이·이전 이력·admission 경계·완료 group 뒤 checkpoint를 포함한다. 잘못된10개는 redo target read/write/sync와 snapshot/terminal/tail write/sync/truncate 전에 거절한다.
- Independent evidence: `python3 build/task23r-independent.py`의 실제 첫 실행은 `build/task23r-claude-independent-attempt-1.json`, 결과는 `build/task23r-independent.json`: C helper 없이 CRC32C/struct/hashlib1329검사·127파일을 대조했다. 전/후/최종의 같은4fixture WAL/source/pages bytes 일치, 선택 checkpoint chain·scalar/거래·control10개·정상 flat root와 source/after/pages 보존을 확인했다. 추가 `build/task23r-extra-byte-checks.json`은25fixture의36checkpoint·21group·31typed image의 owner·chunk ordinal/크기·CRC/UUID/PageRef/endLSN/previousLSN180검사를 확인했다. 합성 target 검증이며 실제 할당/ready DB 증명은 아니다.
- Regression/review evidence: 기존 object를 고정한 checkpoint5368/system22236/호환 witness10237과 redo6925/snapshot1051185/terminal4260/tail21835/outcome2938/ABORT2237/rebuild93590/capacity55748/empty16458/space34861이 모두 성공·stderr0이다. 기존 OOM·옵션 사본/재진입 검사도 포함한다. 실제 링크·run·object hash는 `build/task23r-reviewed-regression-commands.json`, `build/task23r-claude-regressions.json`과 두 run report에 있다. `build/task23r-claude-static.json`의 clang3개는 경고0이다. `build/task23r-review.json`의 독립 검토 둘과 공개 계약 재확인에서 남은 P1/P2/MUST KILL은0이다. 공개 문서3개의 check_doc와 diff/link·기본 object 목록 검사를 통과했다. 전체 검증/보존 manifest는 `build/task23r-verification.json`이며 사용자 identity·parent/scope/marker·clean의 최종 확인은 별도 postcommit 증거로 남긴다.
- Preservation/AAR: 이전34349artifact/42seal·보호9088tracked·main/origin6bb8b2e8·다른4worktree와 모든 기존 실패/원자료를 보존했다. completed group은 pending event buffer를 남기므로 그 count를 active-group guard로 쓰면 안 된다. 이를 제거한 최종 source/object와 정상 회귀를 보존했다. 오래된 witness test object의 enable_system_groups 전 ABI 오연결 SIGABRT도 지우지 않고 기존 호환23h object로 검증했다. redo의 최초 SIGALRM14·90초 제한 실패와 모든 재실행도 보존했다. 같은 binary와 기존23k binary, 새 고정 링크가 원 한도에서 성공했지만 최초 원인과 성능 회귀 여부는 미확정이다. `build/task23r-redo-assessment.json`은 나중의 부하·출력 시각만으로 fsync 원인이나 성능 우위를 단정한 해석을 채택하지 않는다. 완료 이력61개는 위 고정 Git/SHA로만 압축했고 Task6와 다른25미완료 entry는 그대로다.
- Remaining risk: 회수된 이력의 checkpoint seed, 실제 metadata 할당·공간 quota·내구 게시·owner/checkpoint 활성화, 독립 writer·공정성/메모리·COW/WAL 양쪽 유지보수/복구 총비용·제품/stream은 미완료다. 새 성능 우위나 물리 전원 손실 내구성을 주장하지 않는다. 원래23과 모든 뒤 과업/checkpoint 및 실제 비교가 계속 필요하다. Vault/consumer/원격 쓰기·발행·전환은 없다.
- Next step: 이23r task-boundary 뒤 멈춘다. 다음 continuation은 원래23의 남은 내구 복구를 수행하며 별도 checkpoint를 이번 턴에 실행하지 않는다. goal remains active.
- Commit record: containing task-boundary commit subject `fix(repo): checkpoint 이후 WAL 상태 연결 검증 수정`; exact body marker `Goal-mode boundary: goal-1 task 23r: Bind checkpoint-aware system history to the selected anchor`. 사용자 Git identity로 이 수리와 관련 tracking만 함께 기록하고 최신 log와 clean status를 검증한다.

## Task 23 — Implement WAL records and durable commit recovery

- Status: complete (isolated candidate durable recovery integration and local validation; product integration, actual writers and total-cost measurements remain incomplete)
- Scope: Implement the candidate's framing/checksum/order, commit/outcome markers, append/barrier and restart recovery without SQL/callback replay. Implement Task22's selected durable G2 result-resolution path. Issues: #263, #266. Prior23a–r remain component evidence; product SQL/ACK/writers, live buffering/MVCC/GC and matched COW/WAL total cost remain later tasks.
- Work performed: started from clean6d3dd1a9487632bac430e4264601fe5b76950788 after verifying23r user identity/scope/marker and36375 old artifacts/44 seals. Baseline is build/task23-publication-before.json; historical build/task23-before.json was preserved. Applied just-mode feature/how/architect/arena with read-only sketches and an independent judge. Selected stable allocation/outcome root refs, bounded per-group publication and complete capacity/physical-space planning. A new outcome root requires retry discovery; shadowing the allocation tree conflicts with its one-root-image/group contract. A whole-image plan duplicates ownership and holds unnecessary memory. The judge rejected strict redo for tail-bearing selection and checkpoint-only owner advancement; a private read-only completed-group preview and actual allocation/root-owner transition address those conditions.
- Implementation: candidate_recovery_recover owns the existing data/WAL/control files through selection, tail/terminal repair, redo, retained outcome reconstruction, reservation/ALLOC/staging, root/data force, checkpoint/control publication and strict final verification. Success alone returns an owning query handle. Failure returns NULL but effects/control publication can remain uncertain. Existing strict open and WAL-only repair retain their distinct contracts. Same-owner published reentry has0 writes. Actual C/tests/byte-checker writing uses the user's foreground nonpersistent local Claude process; the parent supervises executions, independent review, approved prose/metadata and commit. No new checkout/background conversation or remote publication.
- Selection and ownership: reject a stored terminal without WAL terminal as STALE before writes, without using CORRUPT fallback to hide it. Apply reconstruction scalar ceilings and signed data-address bounds before metadata effects. Preserve valid dependency CORRUPT fallback and IO/NOMEM/LIMIT/UNSUPPORTED/STALE behavior. Root refs remain stable, but historical roots need not share latest epoch/floor. Search every LIVE root for a valid reservation carrier; otherwise allocate a self-accounted temporary empty root without clearing the selected root's retained descriptors. Actual ID_RESERVE/ALLOC images include their own lifetime entries, fixed-point node growth and final owner. Reuse unused reserved IDs only from complete same-owner provenance, full allocation/physical history, exclusive membership and absence of surviving references.
- Source binding: pin the entire anchor-independent transport view before the first candidate and retain source failure across fallback. Every actual semantic observer, selection-to-repair transition and final verifier shares that proof. Trim only to the verified known prefix and extend only from the exact encoded append bytes frozen before mutable I/O callbacks. Requested random LSN, decoded LSN and record digest must agree. Strict redo also binds canonical group_end before data writes, preventing changed PAGE bytes or an unchanged DELTA with moved END from poisoning later retries. Legacy strict open is pinned too. A late whole-stream hash alone was rejected because it detects corruption after a wrong data write.
- Capacity/accounting:4096 outcomes require90 nodes and multiple groups, not a64-node truncation. Plan IDs, signed offsets, post-carrier allocation base,202-entry boundaries,64-image/128-member group limits, retained group/events, actual segment placement and final checkpoint sizes before mutation. Charge distinct LIVE type4 metadata plus result VersionRef.previous/overflow dependencies; validate distinct slots even on shared pages and deduplicate physical charges. Keep type5 maintenance and sparse logical extent separate. Publication inventory fields are before publication; allocation_pages excludes carrier preparation. Final extent reports actual file bytes/LIVE type5/all ALLOC/carrier pages. New reserved+reused reserved IDs equals staging+carrier preparation pages.

### Preserved counterexamples and repairs

All report families below retain their full stdout/stderr, source/object/binary input hashes and raw targets. These are historical failures, not final successful executions. No failed or contradictory fixture is erased.

| Evidence | Observation, cause and bounded repair |
|---|---|
| initial v1/t1, c4 source manifests and t1c4 runs | f1 persisted-terminal-without-WAL and f2 scalar ceiling initially write WAL before rejection; f3 oversized HWM writes11 WAL/2data before LIMIT; f4 misses previous/overflow pages; source-view writes2data from alternate read-at bytes. c4 same frozen t1 objects pass9 small cases after pre-effect checks, full physical accounting and canonical page-record indexing. Dense f4 initial counts8189/8191 are failures, not quota passes. ino_t hook narrowing was removed. |
| stale-root-carrier/alloc/retry and source-view families | Valid older selected roots initially return STALE, while copying stale scalars into a reservation could create a corrupt group. Final carrier logic preserves the selected root and supports both existing LIVE and new self-accounted carrier cases. Unselected root identity is not assumed fixed. |
| g5-faults and g5c5-faults | Eighth WAL write leaves complete type16 ID_RESERVE member without PHYS_END at174480. Old23p tail proof only admits4–7 and retry returns CORRUPT. c5 permits known system members15/16; same frozen fault objects then pass225iterations/119651checks including112 OOM positions. This earlier cohort is component evidence; final current-source fault cohort below supersedes it. |
| s1/s2 c6 and s6 c6shim source reports | scan-lsn accepts a wrong-LSN frame; selectionA/repairB writes WAL/data then stableAretryCORRUPT; cross fallback acceptsB and21writes. PAGE apply writesB then stableAretryOK leavesB. Moved-END writes1data with pageLSN60920 instead of60784 and stableAretryCORRUPT. Before/after actual target files and A/B dumps remain. Uncalled legacy private-pin shim only permits linking the same test object to oldc6; it is not exercised. Old poisoned samples are detection evidence, never a healing claim. |
| s2/s3 carrier and placement controls | c6 carrier33 returnsLIMIT/0writes though the33rd eligible LIVE root should fit at8192 protected pages. c7 recovery itself succeeds, then the fixture captures WAL while still owned and failsBUSY; corrected full continuation later times out400s, so neither is a full pass. Streaming carrier search and closing before capture repair those separate causes. Initial4096 placement fixture fails epoch encoding; its further4097transaction issue is a static bound finding. Corrected3072-request/67-node layout still needed exact checkpoint transaction-size normalization before causal acceptance. |
| corrected u10/o10 paired placement reports | Initial padded fixture changes checkpoint transaction count, confounding the missing image. Final fit uses3362transactions/start267600512, actual181records exactly match planned type/payload sizes and passes452373checks. Cap uses3363/start267600752, plan_fits1/actual_fits0, returnsLIMIT/0writes/unchanged with442792checks;66 legitimate forces remain. Same u10 test object+c6shim performs152WAL/76data writes beforeLIMIT and exits1. Source boundary142+carrier1 gives first slice143..202/two type5 images; final group images3/62/11/new67. |
| c7/s4 append and strict-open controls; parent c8 reruns | c7 rejects actual writtenA as CORRUPT/POISONED but accepts never-writtenB; strict open accepts alternateB ready1. c8 freezes pending view before callback and pins legacy open: AOK/OK, BCORRUPT/CORRUPT, strictB CORRUPT/ready0/0writes/unchanged, then stableAOK. Exact actual frameA/B dumps and physicalA equality remain. Parent independently reruns all3 cases successfully. Frozen t1+c8 small11 and existing7source controls also pass. Generic POISONED must not replace the source-CORRUPT latch. |
| s6 source12 and final fixture/comment review | Nonmasked IMAGE→partialDELTA→partialDELTA checks both halves. Moved-END keeps canonical DELTA and inserts136B event/recomputed END. c8 has firstdata0/base-page unchanged and stableAretry60784; PAGE has noB before retryA. Read-at/selection/cross reject with0writes/0forces; apply has2 legitimate prior WAL forces. Final moved-END CHECKs enforce first_data0 and whole base-page equality; whole-file unchanged1 is observed, not a separate CHECK.17 redundant comments removed,41 contract/safety/counterexample comments preserved, with no behavioral comment-only refactor. |
| s7 debug placement-fit and dense runtime diagnosis |580.007s timeout is preserved, notPASS. One-second live sample observes restore→redo source verification→checkpoint pinned scan, mostly SHA256 in that short sample. It does not establish sole delay cause, fsync attribution or performance ranking. Fresh optimized/fatalUBSan tagged unchanged hot helpers complete corrected fit/cap; old objects remain immutable. |
| u11 basic, large draft and harness failures | Basic succeeds then O_EXCL before-copy collision fails; owner9 now runs on separate clone so owner8 announced postimage stays immutable. Extent oracle now includes reused reservation IDs. cases/regress aggregate nonzero failures and diagnostics. Large draft marked bootstrap carrier transaction4096 both committed and UNFINISHED, so referenceCORRUPT was an invalid fixture; unfinished requests move off the carrier. Corrected faults15 still times out589.943s aftercontrol-synced, notfullPASS. Link argument/missing private pin/dyld and old witness/result ABI failures remain harness evidence, not engine defects. |
| r13 tail and packed redo history | r13 frozen tail test exceeds internal90s alarm. A later u17 run of the same frozen test object passes51.474s; load/time differ, so no cause is attributed to history56B→48B packing. Independent review verifies last_record=last_lsn-record_back under valid256MiB transport bounds, generation0 exclusion and unchanged apply/group_end proof. No formal speed or regression disposition follows from these elapsed diagnostics. Only current-source focused proof and explicit representation-equivalence permit older dense evidence reuse. |

### Executed verification

- Current runtime fingerprints: redo92803594493d21d181aa9742f162792835e47f115be832f0d85873966a8fde60; WALe2986058a8a6ed571c19a3b8c95580cc45e14d38215ec2522c87cbb5255390a6; recoverya4ca9f1cbc0e845f897408bc9923237ff51f6e2050d85d788657ab8bc5f248b9. Changed u17/o17 modules and current test objects use fatal UBSan; frozen objects retain their original instrumentation and every valid execution requires empty diagnostics. All other u13 runtime sources/headers match current bytes; compared u10 differences are public metric comments and later test-oracle fixes plus the same redo packing. Frozen dense/large/placement results below are explicitly those binaries' executions, never all-final-source coverage.
- u17 current-source fault suite: build/task23-publish-o17-run-o17-faults-faults.json is code0/no timeout/stderr0,227iterations/122710checks,114 allocation-failure positions,50 reservation-reuse retries, short/EINTR, write/torn/sync failures and actual process stops. Each failing call returns NULL/no leaked owning allocation; same-owner retry validates queries, strict open and idempotent settle.
- u17 current-source gates: build/task23-publish-u17-run-u17-faults-{basic,selection,empty,resolved,sparse,outcomes,outcomes-partial}.json all pass1326/1005/297/463/414/208638/213514checks. Full4096/90new/3groups and50reused40new/2groups are completed executions. Sparse extent409608192B is logical file size, not allocated bytes/RSS. Requested allocation peaks and owning resident bytes are diagnostics, not an8MiB aggregate-budget or product-memory pass.
- u18 current-source12: build/task23-publish-u18-run-u18-src-*.json all code0/PASS/no timeout/stderr0. Independent review SHA b7812190934dc5707af2db1cbfbfae0416bbee654110d7cc3bace7802e93591f confirms actual maximum markers7884064B preview/7867648B strict workspace, matching release sizes, real injected hits4/6 and no newP1/P2. Selection/cross are CORRUPT/0writes/0forces; strict open is CORRUPT/0writes with3 legitimate forces then stableA succeeds. The56B source-record index is separately bounded near14MiB; history packing saves1,048,576B onLP64, not a whole-app memory promise.
- Frozen o13-faults16 large coverage: o13-run-o13-faults16-faults-large-rep-0-3.json and parent-large16-rest.json pass4+8iterations/689630+1370458checks. Full stdout has exactly12 distinct points: first stage BEGIN/IMAGE, final member/END/data/sync, checkpoint BEGIN/END, control write/sync/synced, durable reservation. Actual recorded member types and first IMAGE are asserted. Durable-reservation retry reuses90 IDs, newreserve0, HWM162→162. Frozen binary a39566ac4d296dd99be961773afa18f96fc78f63afcb1275e6eca3787d244524 and u16 test/link inputs are pinned.
- Parent frozen boundary/cap/quota: parent-boundary16 passes14variants/29294checks. group4093pass45740/group4094LIMIT45342 and event4094pass39381/event4095LIMIT38948 all pass; over-limit cases have0writes/unchanged. parent-quotapass16/quotalimit16/quotashared16 reports pass50588/50599/189checks: protected8192 success, required8194 LIMIT/0writes/unchanged, sharedslots9. Explicit raw prefixes are build/t23pub-f4-8186-78574, t23pub-f4-8188-79596 and t23pub-f4-shared-79920. o10 carrier33 full50897checks succeeds with8192 protected/newnodes0/reserved1/root111. The opposing carrier-none control is historical c6/c7 only: s1-run-s1-srcc6-carrier-none.json and s2-run-s2-srcc7-carrier-none.json each pass50585checks withLIMIT/0writes. It is not an o10/final execution and is outside aggregate3. The46 segment-placement variants/23publication records/485000checks in g5 are c4-cohort component evidence (build/task23-publish-g5-ld-faults.json), not final-cohort executions. candidate_wal_place and the checkpoint source are byte-identical; group placement changes allocation_count to stage_base, equal when there is no new carrier and enforced equal at execution. The fixture has no new carrier by static construction, not by a RECEIPT observation. Changed carrier placement is separately exercised by corrected u10/o10 fit/cap and current focused publication; do not broaden those into an exhaustive current46-variant claim.
- Legacy regressions: r17 transport21541/result-wal-current892/checkpoint5368/system22236/compatiblewitness10237/redo6945/terminal4260/tail21835/outcome2938/aborted2237; r17b snapshot1051212/rebuild93590/capacity55748/empty16458/space34861; r17c state17438/violations0 all succeed, stderr0. Snapshot/redo check-count increases are legitimate added callback checks, not fixed-count oracles. r17b first state parser missed its CHECKPOINT summary despite native0; repaired r17c records the actual supported summary. Failed old ABI links and alarm attempts remain preserved.
- Build/static: build/task23-publish-parent-final-static1.json runs clang --analyze/-Wall/-Wextra/-Werror on10 changed modules with17 pinned source/header inputs, code0/stderr0. parent-final-make1 invokes the actual test-candidate-recovery-publish target with every reused u17/frozen/helper object and build/.build-fingerprint explicitly old-file pinned. Three initially absent binaries/main objects are fresh; fatalUBSan actual Make smokes f1-commit225/basic1326/moved-end294 all pass. No baremake/all/clean/-B or fingerprint rewrite. Existing docs/design/streamlite.md now distinguishes candidate recovery from product activation and preserves later writer/buffer/MVCC/GC/stream/total-cost requirements; check_doc and diff check pass.
- Independent bytes: build/task23-publish-bytes7-verify.json is25 fixtures/0 failures,158 distinct raw input files, exact expected set/hash d8c56da6d0e67040eced4935a668a68b34dc5ab080d7b7d083cca68b3df1bdbe and final verifier SHA319ea7bb63b4c91aee08fc4b859dd329d4d847ca9237672c748b9223561dc44d. It derives CRC32C/struct/hashlib checks without C helpers. Both format-valid slots have checkpoint chain/cut-table replay, declared ID/first/last/terminal/CSN/owner/state, active coverage and cut-aware root/result dependencies. Dirty group binding is implemented and reviewed, but all selected positive byte fixtures have0 dirty entries, so this branch is not claimed as executed byte coverage; legacy checkpoint witness tests remain separate evidence. All completed frames before the exact repair cut are raw-identical even with a partial tail/group. Physical protection includes previous/overflow/shared slots/all LIVE type4. Actual A/B frames, new first-target B absence/retryA and preserved old-target B detection are hashed. Canonical pred/readlsn/chain replay compares7 final data pages each; partial chain has2 DELTAs. bytes6 independently verifies the same3 canonical DELTA targets as published fixtures, not3 new independent populations. Eight bytes4 negative controls on basic and type15-tail inputs all reject for the intended prefix-identity/checkpoint-table reason. Group mutation recomputes page/frame/group/transaction CRC; omission is an explicit in-memory prefix guard control. Its SHA98b36ec02b424a78630dee52afa1a7e715a4b95302fd6e50d60b8354eb97ba64 differs from final only by added A/B canonical replay; negative/publication functions and inputs are unchanged. Independent read-only review closes all accepted verifier P2s; parent inspects the final A/B addition and current replay outputs. parent-byte-reuse.json reconstructs the exact previous98b36ec0 file hash from the final file by reversing only the two A/B additions, concretely establishing unchanged-path reuse.
- Failed verification infrastructure is preserved: original579-line verify.py is not final proof. bytes1 has22fixtures/12 failures from absent-epoch/adjacent-slot/B-semantics/region assumptions; bytes2's first output handler hits BrokenPipe. bytes2b/3/5 positive sets pass before final narrow repairs. Initial negative3 assumes a completed transaction0 group and hits fixture selection failure; final negative4 repairs the actual user transaction terminal CRC instead. Draft snapshot writer-parentbytes2b-sources.json preserves a001cc5781595006dc7f7170f579733db14a1d48bf8956b5324c572ff5e263a7. A late parent request duplicated the already-present bytes6 canonical DELTA proof; bytes7 adds explicit A/B replay with no new native campaign. These are checker/harness observations, not engine counterexamples.
- Final aggregation: build/task23-publish-aggregate3.json reports44 positive native reports,7 explicitly expected before failures, nonempty regression/byte summaries and8 intended negative results with0 problems. build/task23-publish-parent-proof-validation.json additionally checks all full stdout/stderr hashes, executable inputs,16 actual legacy native manifests, exact12-point large coverage, final static/Make inputs and current158 byte-input hashes, with0 problems. aggregate3 points directly to current bytes7 and the valid unchanged-path bytes6/negative4 evidence; aggregate1 and parent-aggregate2 preserve the earlier bytes5-based aggregation.
- Preservation/resource: build/task23-publish-parent-final-preservation1.json rehashes9087 protected tracked files, all36375 baseline artifacts and44 seals with0 mismatches. preservation2 records unchanged main/origin6bb8b2e8 and all four other worktree paths/HEADs/branches; the first raw-text comparison only differed in trailing LF. The existing continuity's38680-byte original prefix matches its baseline hash and subsequent entries are append-only. parent-compact-goal-scope preserves Task6 plus24 other incomplete sections byte-for-byte. Metadata-only mtime estimate6.357GiB/10857files/free11.48GiB revised only the soft artifact aim to7GiB with8GiB-free stop and no unchanged dense/fault repetitions; later free space is volatile and no DB quota/performance conclusion follows. List subtraction10.6GiB included older unmanifested files and is not a new-byte estimate. Final new-artifact/source manifest and postcommit status are recorded at the boundary. No original/failure deletion.
- Closure review: build/task23-publish-parent-record-review.json records a read-only local Claude review of parent-authored tracking/continuity/design, with no remainingP1/P2. Accepted record fixes identify g5 as c4 component/static-equivalence evidence, carrier-none as historical c6/c7, tail timing as noncausal and byte dirty binding as unexecuted in the selected0-dirty cohort. A draft link to an unpublished commit and its stale design memory number were removed; historical Git/Task23m/raw measurements remain. The final parent-only status/current-boundary changes are the administrative delta approved by that review. The foreground writer exits0 after EOF; no second code writer/session remains. Final source/raw/review manifest is build/task23-publish-final-verification.json.
- Remaining risk: the accepted Task23 implementation/validation/record findings are closed within their stated scope. The executed carrier33 cohort does not assert the new extent receipt fields; those relationships are verified in the separate final gates, not claimed as carrier33 assertions. Candidate handle/query recovery is not product APPLIED, actual independent writers, buffering/MVCC/GC/log reuse, physical power-loss proof, fair measured superiority or COW withdrawal based on measurements. Existing runtime/DB/ABI/default COW, main/YCSB, other four worktrees, Vault and consumer ownership remain preserved. No push/PR/merge/deployment/live conversion.
- AAR: selected checkpoint state, alternate read views, exact segment/checkpoint placement, actual physical protection and test-oracle validity must all agree before mutation. Preserve preparation failures and diagnostic timeouts separately from engine counterexamples. A final hash after effects cannot prevent poisoned data; safe retry needs canonical bytes and group end before each write. Final metadata checks must be independent and nonvacuous rather than trusting native receipt counts.
- Next step: stop after this containing local task-boundary commit and its identity/marker/scope/clean-status verification. The next continuation performs only the Additional checkpoint after23, before Task24. Goal remains active.
- Commit record: containing task-boundary commit subject `feat(repo): 후보 DB 내구 복구와 결과 게시 경로 통합`; exact body marker `Goal-mode boundary: goal-1 task 23: Implement WAL records and durable commit recovery`. Only this task's28 source/design/tracking files are included under the configured user identity. Verify the containing commit and clean worktree in build/task23-publish-postcommit.json; no push/PR/merge follows.

## Additional checkpoint after Task 23

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — Task22/23a/23의 계약·실제 framing/거래/영속ID·결과 복구·내구성·자원/소유권·오류/재시작·자료 보존·문서/이슈 상태를 대조한다. tracking-only checkpoint이며 원래after24는 보존한다.
- Remaining risk: transport scan·모형·같은열기 ring을 transaction recovery나실제writer 증명으로 승격하지 않는다. 필요한 수리는 독립 과업으로 추가한다.
- Next step: 원래Task23의 task-boundary 뒤 별도continuation에서 수행하고 그 뒤24로 진행한다.
- Commit status: no checkpoint commit attempted.

## Task 24 — Implement bounded buffering and checkpoint ordering

- Status: incomplete
- Scope: Implement frame ownership/eviction, page LSN WAL-before-data, full-page torn recovery, bounded writeback and checkpoint/log reuse under the Task 22 version contract. Issues: #263, #266.
- Work performed: not started.
- Verification evidence: required — Budget/OOM/disk-full/sync-failure/dirty-eviction/checkpoint-restart tests, pinned-frame pressure and exact WAL/data order observations.
- Remaining risk: Eviction or checkpoint must not publish unsafe versions or reclaim required redo.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Checkpoint after Task 24

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — original-scope coverage, actual source/test/measurement status, safety/data/resource/compatibility and document/issue consistency for Tasks 22–24; tracking-only checkpoint.
- Remaining risk: defer missing implementation/validation repair explicitly, never mark the full goal complete from this checkpoint.
- Next step: checkpoint in its own continuation; then execute the first incomplete task or required repair.
- Commit status: no checkpoint commit attempted.

## Task 25 — Implement MVCC visibility and abort recovery

- Status: incomplete
- Scope: Implement row/transaction version visibility, fixed snapshots, logical abort, index/structural recovery and old-version GC, integrated with candidate WAL/buffering. Issues: #263, #266.
- Work performed: not started.
- Verification evidence: required — Read-own/committed/uncommitted/aborted/restarted versions, index split/allocation abort, long-reader pin/GC/OOM and semantic differential tests.
- Remaining risk: Logical abort, structural split and reader-visible versions need distinct recovery rules.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 26 — Implement early key and range coordination

- Status: incomplete
- Scope: Implement serializable early key/range/table/schema coordination, short physical latches, bounded waits/deadlock handling and transaction-end release in the candidates. Issues: #238, #263, #266.
- Work performed: not started.
- Verification evidence: required — Hot-row versus independent keys, unique/range/predicate/schema conflicts, deadlock/cancel/timeout/fairness and stale SNAPSHOT/result-preservation tests.
- Remaining risk: Conservative table locks can serialize independent work; no silent retry of observable results.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 27 — Integrate same-process independent writer candidates

- Status: incomplete
- Scope: Connect SQL/ACK transaction execution and the E4 shared-payload/small-state fixture to WAL/MVCC/buffer/lock candidate modules, using explicit isolated candidate mode. Preserve current backend defaults. Issues: #263, #266.
- Work performed: not started.
- Verification evidence: required — Open A while independent B executes/commits, same-leaf different rows, same-row/range/append/cursor conflict and correctness/failure checks with actual overlap traces.
- Remaining risk: Multiple request entry points are not independent writers; no stale whole-page overwrite.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Checkpoint after Task 27

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — original-scope coverage, actual source/test/measurement status, safety/data/resource/compatibility and document/issue consistency for Tasks 25–27; tracking-only checkpoint.
- Remaining risk: defer missing implementation/validation repair explicitly, never mark the full goal complete from this checkpoint.
- Next step: checkpoint in its own continuation; then execute the first incomplete task or required repair.
- Commit status: no checkpoint commit attempted.

## Task 28 — Implement and verify cross-process ownership

- Status: incomplete
- Scope: Implement shared frame/transaction/allocation control, recovery ownership and owner-death fencing for same-host direct-file writers; preserve existing supported multiprocess single-writer behavior. Issues: #238, #263, #266.
- Work performed: not started.
- Verification evidence: required — Actual separate processes, stale owner/ABA, simultaneous allocation/rollback, death during publish/checkpoint, recovery exclusion and durable result tests.
- Remaining risk: Process-local mutexes or caches cannot establish cross-process coherence.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 29 — Implement the matched buffered-COW comparator

- Status: incomplete
- Scope: Build the isolated comparator with the same cache/workspace/payload/group policy, early coordination, latest shared-leaf/root merge and protected snapshot/fallback reclamation. Issues: #266.
- Work performed: not started.
- Verification evidence: required — Independent/same-leaf/hot-row changes, allocation/rollback/UNKNOWN/owner death, pin/reuse/restart correctness and bounded-resource checks. Apply Task 22's same G2 outcome-resolution and retention contract, including its implementation/space cost and unsupported boundaries.
- Remaining risk: Comparing unoptimized current COW with improved WAL would bias attribution.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 30 — Execute both candidates' correctness and fault gates

- Status: incomplete
- Scope: Run the 15 design scenarios and E0/E5/E6 safety cases on both real candidate paths, including grouped outcomes, ranges, long readers, cancellation, result loss and restart. Issues: #238, #263, #266.
- Work performed: not started.
- Verification evidence: required — Deterministic positive/negative controls and actual I/O/process failure traces, independent final-state oracle, no lost/duplicate/partial acknowledged updates, relevant sanitizers/differential tests. For both candidates, test response loss/restart, duplicate or changed-content IDs, absent/expired records and the selected G2 lookup/corroboration semantics rather than assuming automatic replay safety.
- Remaining risk: Any mandatory gate failure needs repair before formal performance collection; no physical power-loss claim beyond actual apparatus.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Checkpoint after Task 30

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — original-scope coverage, actual source/test/measurement status, safety/data/resource/compatibility and document/issue consistency for Tasks 28–30; tracking-only checkpoint.
- Remaining risk: defer missing implementation/validation repair explicitly, never mark the full goal complete from this checkpoint.
- Next step: checkpoint in its own continuation; then execute the first incomplete task or required repair.
- Commit status: no checkpoint commit attempted.

## Task 31 — Build and preregister backend total-cost collection

- Status: incomplete
- Scope: Implement/freeze E4–E6 matched layout/workload, arrival/group policy, independent writers/readers, budgets, fairness/tails and maintenance-inclusive collection using Task 30-safe binaries. Isolate payload/state-layout effects from publication effects. Retain Task 15's selected H4: distinguish durable barrier wall/CPU waiting from commit metadata/data writes under equal barrier/response contracts, without treating ON phase time as OFF contribution or omitting sync. Issues: #266.
- Work performed: not started.
- Verification evidence: required — Collector controls, identical guarantees/common improvements/payload layout, plan ancestor, pinned sources/config/inputs and predeclared stop/adoption/statistical rules.
- Remaining risk: Incomplete cleanup, full-page images, GC, restore or shared-memory accounting would bias the comparison.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 32 — Collect backend costs and choose the integration candidate

- Status: incomplete
- Scope: Collect paired E4–E6 layout, steady lifecycle and reader/writer pressure including COW merge/rewrite/reclamation and WAL redo/writeback/checkpoint/GC/restart. Judge the current WAL recommendation against its falsifiers. Issues: #266.
- Work performed: not started.
- Verification evidence: required — Raw hashes/regeneration, completed/debt boundaries, throughput/tails/fairness/CPU/whole-app memory/write/space/recovery and adverse conditions; source-backed retention/withdrawal rationale.
- Remaining risk: Both unsuitable or incomparable is not product success; repair the causal problem without weakening required writers/safety.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 33 — Integrate the validated backend with existing SQL and ACK

- Status: incomplete
- Scope: Integrate the selected candidate into a reviewable product path after Task 32. Preserve existing files/package/ABI and defaults unless a separately justified compatibility decision authorizes change; no live cutover. Issues: #263.
- Work performed: not started.
- Verification evidence: required — Full supported SQL/ACK/transaction/C/Python regression, current-file compatibility/rejection/export-import/backup-restore tests on copies, independent writer and failure evidence. Verify the selected G2 resolution path through product/native/Python entry points, result-body lifetime and restart/expiry/unsupported behavior; same-open ring success is not restart lookup evidence.
- Remaining risk: A comparison prototype or code-only rollback does not establish product compatibility or data restoration.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Checkpoint after Task 33

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — original-scope coverage, actual source/test/measurement status, safety/data/resource/compatibility and document/issue consistency for Tasks 31–33; tracking-only checkpoint.
- Remaining risk: defer missing implementation/validation repair explicitly, never mark the full goal complete from this checkpoint.
- Next step: checkpoint in its own continuation; then execute the first incomplete task or required repair.
- Commit status: no checkpoint commit attempted.

## Task 34 — Fix the consumer-stream contract

- Status: incomplete
- Scope: Specify subject/order/replay, consumer/group cursor, holes, lease/fence/duplicate ACK, retry/DLQ/redrive, bounded retention/backpressure and SQL coupling in the existing design using G2–G4/G6. Issues: #257, #264.
- Work performed: not started.
- Verification evidence: required — Concrete input/result/state/ownership/failure and pressure matrix, single logical payload rules, reversible test defaults with reasons and falsifiers.
- Remaining risk: No implicit consumer expiry, acknowledged data deletion or external exactly-once guarantee.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 35 — Implement stream append and independent replay

- Status: incomplete
- Scope: Implement the shared logical payload log, group registration/cursors, append/filter/replay and SQL/event transaction coupling on the selected validated backend. Issues: #264.
- Work performed: not started.
- Verification evidence: required — Independent group delivery and within-group distribution, sequence holes/order/cursor/restart, atomic SQL+event/no-event rollback and SQL/ACK-only regressions.
- Remaining risk: Sharing an event payload must not let one group's progress remove another group's required replay.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 36 — Implement consumer lifecycle and bounded retention

- Status: incomplete
- Scope: Implement claim/lease/fence/ACK holes, retry/failure isolation/DLQ/redrive and retention/pressure with ownership-safe cleanup and restart. Issues: #264.
- Work performed: not started.
- Verification evidence: required — Stale/duplicate/out-of-order ACK, lease reacquisition, dead consumer, slow/long reader, quotas/disk/OOM and interrupted cleanup/reopen tests; same-transaction completion and UNKNOWN.
- Remaining risk: Retention pressure must use the chosen refusal/wait policy; no unapproved silent loss.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Checkpoint after Task 36

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — original-scope coverage, actual source/test/measurement status, safety/data/resource/compatibility and document/issue consistency for Tasks 34–36; tracking-only checkpoint.
- Remaining risk: defer missing implementation/validation repair explicitly, never mark the full goal complete from this checkpoint.
- Next step: checkpoint in its own continuation; then execute the first incomplete task or required repair.
- Commit status: no checkpoint commit attempted.

## Task 37 — Build and preregister product-alternative comparisons

- Status: incomplete
- Scope: Prepare actual version-pinned SQLite state-machine and local NATS R=1 file alternatives, capabilities and E7 collector for messaging versus external-SQL coupling. Issues: #267.
- Work performed: not started.
- Verification evidence: required — Matched filter/fanout/lease/fence/retry/replay/retention/outcome controls, sync modes separated, process/client/relay cost included, safe local synthetic fixtures and adoption criteria.
- Remaining risk: External SQL plus broker is not a single DB transaction; missing capabilities stay explicit.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 38 — Collect whole-app alternative and integration costs

- Status: incomplete
- Scope: Collect E7 results plus installation/config/integration observations, finalization/retention/recovery and actual whole-app resources against fixed capabilities. Issues: #267.
- Work performed: not started.
- Verification evidence: required — Reproducible raw/results, failure and mismatch samples, CPU/RSS/tails/storage/cleanup/restore, observed effort rather than code-line time estimates, each product hypothesis judged.
- Remaining risk: No market demand, universal speed, development-time or physical durability claim from a narrow synthetic comparison.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 39 — Verify the integrated product and reconcile final evidence

- Status: incomplete
- Scope: Run integrated SQL/ACK/stream/native/Python, memory/lifecycle, concurrency/recovery and existing-file compatibility regression on the selected product; reconcile existing design/report/issues with actual evidence. Issues: #257, #260, #238, #239, #261, #262, #263, #264, #265, #266, #267, #268, #269, #158.
- Work performed: not started.
- Verification evidence: required — Requirement-by-requirement source/tests/measurement mapping, all mandatory repairs closed, original data and private/public boundaries preserved; no closure from document checks or task count alone.
- Remaining risk: Missing required behavior/measurement inserts repair tasks before final review; no implicit release or live migration.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Checkpoint after Task 39

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — original-scope coverage, actual source/test/measurement status, safety/data/resource/compatibility and document/issue consistency for Tasks 37–39; tracking-only checkpoint.
- Remaining risk: defer missing implementation/validation repair explicitly, never mark the full goal complete from this checkpoint.
- Next step: checkpoint in its own continuation; then execute the first incomplete task or required repair.
- Commit status: no checkpoint commit attempted.

## Final review after scope repair

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — every recovered explicit requirement, original authorized unfinished work, present recommendation/rationale, current artifacts and actual test/measurement status; user's correction addressed without scope substitution.
- Remaining risk: a proposal audit cannot prove implementation, measurements, user approval or publication.
- Next step: perform only after all required tasks and checkpoints; do not mark complete on task count alone.
- Commit status: no final-review commit attempted.
