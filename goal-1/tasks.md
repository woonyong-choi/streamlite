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

Completed-entry prose is compacted during Task 16 after a full reread. The exact precompaction ledger is preserved in existing Git object `36529f6c57e08a04923df55f4a2c9765f7e85ff9:goal-1/tasks.md` (SHA-256 1813c082a787753bcc2215539f8352b3224430f05764ce6272bd411813915985). Use `git show 36529f6c:goal-1/tasks.md` and the unchanged section title for full work, commands, failures, limitations and historical next steps. This is a digest in the same approved tracking file, not a new context copy or rewritten history. At compaction, Task 6 source coverage/decisions/matrix and every incomplete entry remain verbatim; later execution updates only its own entry. The rejected earlier overall completion remains superseded. Current next boundary is the additional checkpoint after Task 17; original requirements/checkpoints are unchanged.

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
- Verification evidence: `python3 /Users/woonyong/.codex/skills/repo-docs/scripts/check_doc.py docs/development-report.md docs/design/streamlite.md` and `git diff --check` pass.
- Remaining risk: neither COW nor WAL multiwriter candidate has been implemented or compared; numerical workload/tail/resource budgets and failure contracts are not fixed.
- AAR: a mutable historical issue comment is weaker evidence of the original rationale than the removal commit.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `d862fd62686d216c65c2fbf833cb6abace477451`, `goal-1 task 1: Preserve the COW decision and possible withdrawal rationale`; `Goal-mode boundary: goal-1 task 1: Preserve the COW decision and possible withdrawal rationale`.
- Full record: preserved Git ledger above, same section title.

## Task 2 — Specify the proposed concurrency and resource contract

- Status: complete (contract proposal and static review only; no runtime implementation or approval of defaults)
- Work performed: expanded `docs/design/streamlite.md` with a current-versus-proposed boundary table, separate same-process/multiprocess gates, serializable validation recommendation and snapshot alternative, logical rebase versus semantic conflict, cancellation handoff, durable/group outcome states, restart-query limits, ownership and aggregate byte budgets, pin/reclamation and retention pressure, migration and fallback.
- Verification evidence: `python3 /Users/woonyong/.codex/skills/repo-docs/scripts/check_doc.py docs/design/streamlite.md` and `git diff --check` pass.
- Remaining risk: serializable and persistent result-query designs need implementation and safety evidence; cross-process admission/fencing and migration recovery remain unproved.
- AAR: current same-open commit-ring lookup must not be presented as durable restart deduplication.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `01a9475da976acccc92cbe3bfe4b75fbe4f4478b`, `docs(docs): 동시 쓰기와 자원 계약 제안 추가`; `Goal-mode boundary: goal-1 task 2: Specify the proposed concurrency and resource contract`.
- Full record: preserved Git ledger above, same section title.

## Task 3 — Fix the separated comparison and adoption gates

- Status: complete (comparison/adoption design proposal and static validation only; no experiment collection or engine decision)
- Work performed: added E0–E7 to the existing StreamLite design: eligibility, cache, cache/workspace crossover, conditional SQL, payload/state layout, actual concurrent writers, matched COW versus WAL/MVCC, SQLite and local JetStream.
- Verification evidence: `python3 /Users/woonyong/.codex/skills/repo-docs/scripts/check_doc.py docs/design/streamlite.md` and `git diff --check` pass.
- Remaining risk: contract approvals, numerical budgets, arrival rates, timeout and total collection/storage costs, actual environment/versions and new trial tools are not fixed.
- AAR: preserve pairing at the run/block level instead of treating requests as independent repeats; E2 needs all four conditions in each block.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `a07f873274252e89bab33c6bf5dca0857688bef3`, `docs(docs): 저장 구조 비교와 채택 기준 제안 추가`; `Goal-mode boundary: goal-1 task 3: Fix the separated comparison and adoption gates`.
- Full record: preserved Git ledger above, same section title.

## Checkpoint after Task 3

- Status: complete (review with one required documentation repair queued; no implementation validation)
- Work performed: previous turn is progress, verified by `a07f8732` and the Task 3 marker.
- Verification evidence: `python3 /Users/woonyong/.codex/skills/repo-docs/scripts/check_doc.py docs/development-report.md docs/design/streamlite.md` and `git diff --check 6bb8b2e8 HEAD` pass.
- Remaining risk: CP3-1 must be repaired before calling the comparison proposal complete overall or linking it as full memory-requirement coverage.
- AAR: throughput, maintenance and resource-pressure coverage can still omit the original read/listing use case.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `d6d61a5d7150eb4cd827ac9411471408c5b47e52`, `docs(docs): 세 작업 검토와 조회 검증 누락 기록 추가`; `Goal-mode boundary: goal-1 checkpoint after task 3: complete`.
- Full record: preserved Git ledger above, same section title.

## Task 4 — Restore bounded pending-query validation

- Status: complete (CP3-1 design coverage repaired; no memory measurement or consumer implementation)
- Work performed: added pending-list retrieval as a sixth workload, linked D51 and five source files, and specified Q1–Q5 for one-page metadata, one-page decoded payload, full sequential consumption, full materialization and failure/lifetime/state cases.
- Verification evidence: `python3 /Users/woonyong/.codex/skills/repo-docs/scripts/check_doc.py docs/design/streamlite.md` and `git diff --check` pass.
- Remaining risk: G1–G5 decisions, real query collector/environment and byte budgets are unresolved; the generator/caller, scan costs, RSS measurement limits, actual pending-state need and real consumer path can still defeat bounded-memory expectations.
- AAR: a page-count limit is not a byte limit, an internal iterator is not the caller's consumption policy, and zero returned payload is not zero engine I/O.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `923d2432a6ca0f7d4497cf12cffd2c6422ed4b68`, `docs(docs): 목록 조회 메모리 검증 기준 추가`; `Goal-mode boundary: goal-1 task 4: Restore bounded pending-query validation`.
- Full record: preserved Git ledger above, same section title.

## Task 5 — Connect bounded follow-ups and reconcile #257

- Status: complete (follow-up structure, issue reconciliation and documentation only; no approved contracts, engine implementation or new measurements)
- Work performed: created #260 StreamLite follow-up implementation with nine native subissues: cache #261, transaction workspace #262, independent writers #263, consumer streams #264, pending-query memory #265, COW/WAL total cost #266, SQLite/local JetStream alternatives #267, conditional claim/ACK #268 and optional queue initialization #269.
- Verification evidence: actual GitHub readback matches all 11 expected issue bodies, ownership, OPEN state, nine native children, 20 blocking edges, project hold status and exact resume-condition fields.
- Remaining risk: real contracts and budgets, implementation, concurrency/fault/query tests and comparable performance/product evidence are still future work.
- AAR: separate the design gate from the implementation umbrella and preserve smaller existing decision issues.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `02dbcab7dd800d9e52077193f4c1a59d2b91eba5`, `docs(docs): StreamLite 후속 과업과 검증 연결 추가`; `Goal-mode boundary: goal-1 task 5: Connect bounded follow-ups and reconcile #257`.
- Full record: preserved Git ledger above, same section title.

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
- Verification evidence: check_doc.py passes both existing public documents; git diff --check 6bb8b2e8 HEAD passes; protected src/include/python/tests/bench/Makefile/.github/README/experiment diff against main is empty.
- Remaining risk: present architecture judgment, public rationale corrections, contracts, new engine work and matched measurements remain uncompleted.
- AAR: exact recovered source can contradict a previously passing document audit.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `3331503e9617f21a317acdd488c50b3e68520605`, `docs(docs): 대화 복구 검토와 설계 판단 조건 갱신`; `Goal-mode boundary: goal-1 checkpoint after task 6: complete`.
- Full record: preserved Git ledger above, same section title.

## Task 7 — Make the present storage architecture judgment

- Status: complete (present architecture recommendation and source/document verification; no new backend implementation, measured superiority or product cutover)
- Current recommendation: page/redo WAL with a bounded buffer pool, MVCC and early key/range coordination is the recommended direction for the renewed engine.
- Verification evidence: check_doc.py passes docs/design/streamlite.md; git diff --check passes; read-only validation verifies all 25 relative links/anchors, six G gates, E0–E7/Q1–Q5 and 15 scenarios, proposal status and public/private scope.
- Remaining risk: WAL recovery/shared-state implementation burden, actual budgets/API defaults/version format and matched performance/safety remain open.
- AAR: a current recommendation can be justified without inventing a measured win.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `cd4413a6966f3a0001ce16f67ffe08a8f1ad89c9`, `docs(docs): WAL 기반 동시 쓰기 권고와 선택 근거 갱신`; `Goal-mode boundary: goal-1 task 7: Make the present storage architecture judgment`.
- Full record: preserved Git ledger above, same section title.

## Task 8 — Reconcile existing design and rationale with the judgment

- Status: complete (existing public rationale reconciled; no backend implementation, new measurement or product cutover)
- Work performed: updated D52 to state the present page/redo WAL, bounded buffer pool, MVCC and early-locking recommendation first.
- Verification evidence: check_doc.py passes docs/development-report.md and docs/design/streamlite.md; git diff --check passes.
- Remaining risk: new backend, G1–G6 details, actual workload/budgets, matched correctness/performance/recovery evidence and the ten restored execution items remain unfinished. #257 and remote issue wording/dependencies are not updated or recertified by this boundary; Task 9 must reconcile them and create actual execution tasks.
- AAR: runtime preservation, rejection of a combined path and the new engine's recommended direction are different claims.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `099f0d5af363837486839433bffdfe78639e77c8`, `docs(docs): COW 제외 이력과 WAL 권고 근거 갱신`; `Goal-mode boundary: goal-1 task 8: Reconcile existing design and rationale with the judgment`.
- Full record: preserved Git ledger above, same section title.

## Task 9 — Reconcile actual unfinished work and issue gates

- Status: complete (issue/rationale reconciliation and restored execution queue only; all new execution tasks remain incomplete)
- Work performed: updated only #238/#239/#257/#260/#263/#266 bodies to preserve the recovered early-coordination and old combined-path rejection, current WAL recommendation/falsifiers, G6 recovery obligations, common-cost follow-ups and prototype-before-comparison versus product-after-comparison distinction.
- Remaining risk: thirty execution tasks plus checkpoints/possible repairs remain.
- AAR: an acyclic issue graph alone does not distinguish a comparison prototype from later product integration.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `c9db86a8cb4305fa295684ee2723fb57d00f11ff`, `docs(docs): StreamLite 구현·실측 실행 과업 복구`; `Goal-mode boundary: goal-1 task 9: Reconcile actual unfinished work and issue gates`.
- Full record: preserved Git ledger above, same section title.

## Checkpoint after Task 9

- Status: complete (scope/order/evidence review with a tracking clarification; no engine or measurement validation)
- Work performed: previous turn is progress, verified by c9db86a8, its Task 9 marker and exact three-file diff.
- Verification evidence: check_doc.py passes both public documents; git diff --check 6bb8b2e8 HEAD passes.
- Remaining risk: detailed contracts, real candidate/stream/cache/workspace code, equal-guarantee fault and total-cost measurements, original pending/scale results and product integration are still unproved.
- AAR: listing UNKNOWN and restart tests is insufficient unless the chosen request/result-resolution path and its absence/expiry behavior are explicit in both candidates and product APIs.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `f91ed5b51b6cb85d70fd8debf3917ee13551c6c9`, `docs(docs): 실행 과업 검토와 결과 조회 조건 갱신`; `Goal-mode boundary: goal-1 checkpoint after task 9: complete`.
- Full record: preserved Git ledger above, same section title.

## Execution closure rules after Task 9

This queue restores authorized work; it does not assert implementation, approved customer SLOs or measured benefit. Each task below is incomplete and must record its actual work, evidence, residual risk, next action and task-boundary commit at execution. Resolve its applicable contracts before changing behavior. Use reversible test defaults with reasons and falsifiers; do not wait indefinitely for an unnamed approval. A required failed test/measurement or unmet user outcome inserts a repair before dependent work, without replacing the original criterion or erasing failed samples. Candidate rejection can finish that experiment, not an unmet product requirement. Keep diagnostics and noninstrumented outcomes separate, and preregister collection before formal samples. Reuse existing approved docs and execution areas; no new context copies, consumer writes, production migration or publication is inferred.

GitHub's #266 → #263 edge is the comparison-result → product-integration gate. Tasks 22–30 implement and verify comparison candidates for #266 before that result, while Task 33 integrates the chosen product path. Relevant phase contracts and this prototype/product distinction must be reflected in actual issue readiness before code execution; an entire unpublished design issue is not an indefinite stop for authorized reversible local work. Do not claim remote issue closure while its PR/evidence conditions remain unmet.

## Task 10 — Fix the common execution and resource contract

- Status: complete (common contract selection, source/document verification and phase readiness; no cache/workspace/lifecycle implementation or new measurement)
- Work performed: added five common-contract sections and ten explicit input/oracle rows to docs/design/streamlite.md.
- Verification evidence: check_doc.py and git diff --check pass; source/contract oracle review covers all ten new scenarios, syntax/metadata separation, input limits, cache fallback, scope accounting, rollback/I/O boundaries and explicit-use lifetime.
- Remaining risk: immutable-template construction/instantiation accounting, schema reload miss cost, actual OOM/close/rollback correctness, cache/workspace/conditional effects and all new backend/stream work remain unproved.
- AAR: persistent cache memory cannot remain charged to a query ledger that session_exec resets after every statement.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `2612a1222e015e3218f5298155d5e039c8249035`, `docs(docs): 공통 실행 계약과 캐시 구현 착수 조건 갱신`; `Goal-mode boundary: goal-1 task 10: Fix the common execution and resource contract`.
- Full record: preserved Git ledger above, same section title.

## Task 11 — Implement bounded SQL template reuse

- Status: complete (local internal comparison implementation and regression; no measured adoption, public prepared API or new storage backend)
- Work performed: added parameter-origin metadata, binding-free syntax cloning and exact-SQL/version-keyed per-handle LRU for SELECT/INSERT/UPDATE/DELETE.
- Regression evidence: built the 20 relevant native suites with SQL_TEMPLATE_CACHE=0 and 1 under -Wall/-Wextra/-Werror/UBSan; build/task11-off-build.log and task11-on-build.log retain actual flags/errors. build/fault/task11-on-final-*.json records 20 passing ON suites; after clone/overflow hardening, task11-on-reviewed-* and task11-on-overflow-* rerun the affected cache/parser/SQLite/fault/session audit paths successfully.
- Remaining risk: AST/value cloning, linear bounded lookup, conservative reload misses, resource pressure and cache-lifetime counter resets can reduce benefit; whole-app memory/RSS, tails/fairness and cache/workspace four-condition results remain Tasks 14/15.
- AAR: a persistent cache changes valid residency and hit preparation peaks, so transient-only/cold-only test assumptions cannot stand in for budget/ownership invariants.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `54b43509838d5f98a485b14d896b8ce29bc0a7a6`, `feat(repo): SQL 구문 캐시와 실행별 바인딩 분리 추가`; `Goal-mode boundary: goal-1 task 11: Implement bounded SQL template reuse`.
- Full record: preserved Git ledger above, same section title.

## Task 12 — Implement optional queue lifecycle

- Status: complete (local Python lifecycle implementation and regression; no noninstrumented cost/adoption result or stream implementation)
- Work performed: declare the existing registry without allocating an instance value on SQL-only construction; create it at first explicit Queue registration through instance-dict setdefault.
- Remaining risk: registry lookup/close check, post-use empty set/snapshot and imports/common native state remain; whole-app memory, timing and feature-used overhead still need Task 14 preregistration and Task 17 collection.
- AAR: lazy allocation exposes a failure after native resource acquisition and a concurrent first-publication window.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `3adb24badffd8f3697f18521242de0afab5c37e2`, `feat(repo): Python 큐 등록부 지연 생성과 실패 정리 추가`; `Goal-mode boundary: goal-1 task 12: Implement optional queue lifecycle`.
- Full record: preserved Git ledger above, same section title.

## Checkpoint after Task 12

- Status: complete (common-contract/cache/lifecycle audit; documentation consistency repair queued, not engine or performance completion)
- Work performed: previous turn is progress, verified by 3adb24ba, its Task 12 marker and eight-file scope.
- Verification evidence: native source/ABI/C tests match 54b43509; verified both sets of 20 Task 11 native final reports and affected overflow reruns have exit 0/expected outcomes.
- Remaining risk: CP12-1/2 still require their actual repair.
- AAR: a passing document checker and accurate newer paragraph do not repair an older contradictory execution-order sentence.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `f94bd0fc8f1cbf65ef5385c1f9ff33aa134478a8`, `docs(docs): 공통 실행과 선택형 수명 검토 기록 갱신`; `Goal-mode boundary: goal-1 checkpoint after task 12: complete`.
- Full record: preserved Git ledger above, same section title.

## Task 12a — Reconcile cache admission and implementation status

- Status: complete (documentation and issue-summary repair only; no runtime/default change or new measured effect)
- Work performed: corrected the older design sentence that could imply parser execution after rejected storage.
- Verification evidence: check_doc.py passes both public documents; git diff --check passes.
- Remaining risk: local implementation/regression does not prove noninstrumented speed, whole-app memory, adoption, public integration or physical durability.
- AAR: admission rejection has different meanings before preparation and after a successful statement; only the former may enter an uncached execution path.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `6e150cc1f251f56be5e971f2354f2ec23cf02ab3`, `docs(docs): 캐시 저장 순서와 내부 구현 상태 설명 수정`; `Goal-mode boundary: goal-1 task 12a: Reconcile cache admission and implementation status`.
- Full record: preserved Git ledger above, same section title.

## Task 13 — Implement transaction-private accumulated changes

- Status: complete (local default-OFF workspace implementation and regression; no noninstrumented effect/adoption, new backend or actual independent writers)
- Work performed: added per-work private page images and private catalog reuse behind TXN_WORKSPACE=0/1 and per-handle internal controls, default OFF.
- Remaining risk: internal single-writer COW buffering still retains copies, conservative metadata admission and per-statement heap/index/AST costs.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `ce7a9f4609d19fbcac3e12eeda6f37a1b8d62c28`, `feat(repo): 거래별 COW 변경 누적과 커밋 전 쓰기 추가`; `Goal-mode boundary: goal-1 task 13: Implement transaction-private accumulated changes`.
- Full record: preserved Git ledger above, same section title.

## Task 14 — Build and preregister common-cost comparisons

- Status: complete (collector, preregistration and controls only; no formal performance collection, adoption or new backend)
- Work performed: added optimized native driver, Python lifecycle/queue worker, fixed JSON config, registration/collection/verification and paired BCa analysis.
- Verification evidence: -O2/-Wall/-Wextra/-Werror native driver builds pass in existing build and build-benchmark; verify_benchmark.py confirms actual OFF/ON libraries.
- Remaining risk: 10 pairs with 192/12/8 adjusted interval families can be imprecise or degenerate; lack of adoption is not equivalence.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `28d68e3f53ec395824b94b5fd09e3bd5b844f13f`, `feat(repo): 공통 비용 수집기와 사전 판정 기준 추가`; `Goal-mode boundary: goal-1 task 14: Build and preregister common-cost comparisons`.
- Full record: preserved Git ledger above, same section title.

## Checkpoint after Task 14

- Status: complete (review with a required collector repair before formal collection; no engine or performance completion)
- Work performed: previous turn is progress, verified by 28d68e3f, its Task 14 marker, eleven-file scope and ce7a9f46 parent.
- Remaining risk: CP14-1 must be repaired before formal Task 15; CP14-2 must be corrected before presenting that lifecycle input.
- AAR: equal repeated summaries, valid checksums and complete row counts can coexist with a violated stopping rule.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `8ab71f713241979ad32b64cbfff86219ad296ed8`, `docs(docs): 공통 비용 검토와 시간 한도 결함 기록 갱신`; `Goal-mode boundary: goal-1 checkpoint after task 14: complete`.
- Full record: preserved Git ledger above, same section title.

## Task 14a — Repair common-cost campaign deadline and reporting

- Status: complete (collector deadline/reporting repair and controls; no formal effect collection, adoption or new backend)
- Work performed: added a single monotonic deadline and remaining-time worker bounds for fixture creation and actual native/Python workers.
- Regression evidence: build/task14a-collector-final.log has 42 passing -W error checks (the original 21 plus final-case, nine deterministic deadline boundaries, shared remaining time, real subprocess timeout, invalid elapsed and count-corruption cases).
- Remaining risk: synchronous OS calls and failure-evidence hashing can finish after the deadline; overrun rejects the campaign rather than promising an exact wall-clock kill or inferring DB NOT_APPLIED.
- AAR: a campaign can have every case complete and still fail its budget, so separate case accounting from campaign status and test the final persistence boundary.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `9455208029013751cb74161f328f7814d0a8e658`, `fix(repo): 공통 비용 수집 시간 한도와 완료 판정 수정`; `Goal-mode boundary: goal-1 task 14a: Repair common-cost campaign deadline and reporting`.
- Full record: preserved Git ledger above, same section title.

## Task 15 — Collect and judge cache and workspace effects

- Status: complete (preregistered E1/E2 collection and hold disposition; no default adoption, new backend or product integration)
- Work/evidence: build/task15-cache-formal.registration.json pins plan/head 9455208029013751cb74161f328f7814d0a8e658, unchanged config/product source/optimized OFF+ON binaries/environment and exact 576-case schedule; control=false and registration SHA-256 c01613f810abc1442ea0b129c34c7bbfd6857346bfe2e37a8ab642efcd1a6f25.
- Remaining risk: both adoption hypotheses remain held, including broad tail/CPU uncertainty and RSS interval degeneration.
- AAR: preserve paired-ratio inference even when separate condition medians suggest a different direction; never change aggregation to select a win.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `0c8b575cb60c596c29f369cfc0d8a0f4fcecc302`, `docs(docs): 캐시와 거래 누적 실측의 채택 보류 기록 추가`; `Goal-mode boundary: goal-1 task 15: Collect and judge cache and workspace effects`.
- Full record: preserved Git ledger above, same section title.

## Checkpoint after Task 15

- Status: complete (evidence/requirements review; bounded analysis-metadata repair queued, no new implementation or collection)
- Work performed: previous progress is 0c8b575c with the exact Task 15 marker, parent 94552080 and three-file scope.
- AAR: correct complete raw rows and identical analysis do not prove a metadata label is correct.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `dc6f2cc87971fdbdfc8df7ad92b8cffa20db3ec6`, `docs(docs): 공통 비용 실측 검토와 표본 분류 수리 기록 추가`; `Goal-mode boundary: goal-1 checkpoint after task 15: complete`.
- Full record: preserved Git ledger above, same section title.

## Task 15a — Repair sample classification and historical analysis regeneration

- Status: complete (sample metadata and offline reproduction repair; no new formal effect collection or product/default change)
- Work performed: native and Python analysis schema 2 separately reports preflight/formal/diagnostic counts, retaining instrumented_records as diagnostic count.
- Regression evidence: build/task15a-counts-before.log has three expected baseline failures: native combines 8 instead of 4 preflight samples, while both Python phases omit counts.
- AAR: source pinning includes the whole collector, not only an unchanged statistical function, so a nearby commit is not an acceptable substitute for intermediate working sources.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `92c2ec617a4802dfc56acf335f3551cacc9118a9`, `fix(repo): 표본 분류와 과거 분석 재현 경로 수정`; `Goal-mode boundary: goal-1 task 15a: Repair sample classification and historical analysis regeneration`.
- Full record: preserved Git ledger above, same section title.

## Task 16p — Support indexed scalar-subquery equality

- Status: complete (general SQL prerequisite and regression only; original conditional queue integration and measurement remain incomplete)
- Work performed: database_expr.c reuses only an existing noncorrelated materialized scalar relation with one row/column, non-NULL value and exact column type.
- Regression evidence: actual make BUILD_DIR=build-regression builds with -Wall/-Wextra/-Werror/UBSan and global cache/workspace 00 and 11 each pass seven native suites (task16p-final-off-* and task16p-on-* JSON/stdout/stderr).
- Remaining risk: original Task 16 still owes real claim/ACK integration, minimum/expiry/fence/overflow and duplicate/stale/FAILED behavior, normal/conflict/timeout/cancel/OOM/reopen queue regression and actual scan/binding/copy/I/O diagnostics.
- AAR: supported SQL syntax does not establish an indexed DML access path; SELECT's existing preparation can differ from UPDATE/DELETE.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `36529f6c57e08a04923df55f4a2c9765f7e85ff9`, `feat(repo): 비상관 서브쿼리 동등 조건의 색인 탐침 추가`; `Goal-mode boundary: goal-1 task 16p: Support indexed scalar-subquery equality`.
- Full record: preserved Git ledger above, same section title.

## Task 16 — Implement conditional queue updates

- Status: complete (local internal comparison implementation and regression; no formal effect collection, adoption or new backend)
- Work performed: added default-OFF ACKQ_CONDITIONAL=0/1 and per-queue selection for claim/claim_id and ACK/fail. Minimum scalar selection uses the same two ordered state probes, COALESCE plus full eligibility and overflow guards; same-transaction sequential batch selection retains minimum ID and lease/fence. Zero-row mutation restores local dirty state and uses read-only metadata for empty/overflow/missing/ineligible/duplicate/stale; no mutation replay or skip. FAILED and first fail-note remain distinct. Caller failed-transaction/explicit rollback, payload ownership and UNKNOWN output suppression stay intact; no queue hook in SQL-only core or public ABI/default/format change.
- Regression evidence: build/fault/task16-fixed-{on,off}-*.json records ten native suites each under -Wall/-Wextra/-Werror/UBSan with halt_on_error=1 and empty stderr. The new differential suite has 852 checks per run across four cache/workspace conditions; existing queue/fault/C ABI, SQL/SQLite/fault, session audit and cache/workspace suites pass. Cancellation at statement boundaries, partially changed batch, payload allocation failures, SQL query-budget refusal, caller rollback, writer timeout, overflow, result lifetime and reopen are covered. Standard make test-ack-queue also passes all four binaries; build/task16-fixed-standard-queue.log and its named reports preserve evidence.
- Python/collector evidence: four optimized actual baseline/candidate OFF/ON libraries each pass 62 Python checks with -W error (build/task16-fixed-python-*.log); export sets equal original ABI and actual instrumentation matches (task16-fixed-symbol-checks.json). Eight direct worker controls complete 64 queue cycles with payloads 256/4096 (task16-fixed-worker-controls.json); these are controls, not formal effects. Collector has 87 passing checks including six exact flag-match guards; Ruff/format/mypy pass. Workload/config/thresholds and original validated hashes are unchanged.
- Negative diagnostics: task16-fixed-cost-observations.json has 64 test-hook observations. Normal claim SQL 3→1 and ACK 2→1, but partial batch 4→5, duplicate/stale ACK 1→2, empty claim 2→3. Normal claim query reservation rises from 40,425–62,730B to 132,245–210,325B; fixed 96KiB query budget refuses candidate without effects. Full 32-item batch in condition00 has SQL 34→32, index lookups 34→96 and index pages 358→544. Preserve binding/copy/query/result/I/O and wall/CPU limits; these are requested bytes and instrumented observations, not whole-app RSS, physical I/O or noninstrumented improvement.
- Essential prerequisite repair: stderr review found empty UNION NULL-pointer arithmetic in database_select.c despite exit 0. Preserved pre-fix binaries/reports and exact failing DB (task16-pre-union-fix-identities.json, task16-pre-fix-standard-reports.json, task16-union-failed-fixtures.json); fatal reproduction task16-union-fatal-before.json terminates with signal6 and expected-error mismatch, retained as failure. Guard row copying when empty and add four general SQL cases. Fatal post-fix reproduction and all final fixed-prefix runs pass; earlier warning-bearing runs are not successful evidence.
- Provenance/scope: build/task16-verification.json and task16-eligibility-evidence.json pin actual source/build/artifact hashes. Predecessor 36529f6c is explicitly a base, not the source commit of new binaries; eligibility still needs sealing against this Task16 containing commit before Task17 registration. Original 8,099 prior artifact entries, protected optimized binaries/fingerprints, failed DB, main/origin-main and four other worktree heads verify unchanged. #268 remains OPEN/in progress with three unchecked conditions; twelve dependency edges/nine children/board metadata and issue body/comments are unchanged. No remote mutation, formal campaign, YCSB alteration, Vault write or publication.
- Tracking maintenance: after full reread, compacted only completed records against the sealed Git ledger above, retaining actual boundary commit identities. Task6 remains verbatim; every checkpoint/task after16 remains verbatim. Original Tasks1–39 and repairs/checkpoints are retained; input.md is unchanged.
- Remaining risk: repeated candidate probes and higher query memory may defeat fewer SQL calls, especially batches or constrained budgets. Default OFF is preserved. Actual lifecycle/E3 effects, formal provenance sealing/registration, writer/stream/pending/scale and maintenance-inclusive WAL versus buffered-COW comparisons remain unfinished. Present WAL/buffer/MVCC/early-locking recommendation is unchanged and is not implemented by this task.
- AAR: a zero exit code does not imply sanitizer success; inspect stderr and make UBSan fatal. Empty set behavior is part of candidate eligibility. Record SQL-count reductions beside increased scans/memory rather than treating the former as adoption evidence.
- Next step: stop at this task-boundary commit. Next continuation performs only the added checkpoint after16, then original Task17 performs separately registered lifecycle/E3 comparison against this task's sealed source/build evidence. Goal remains active.
- Commit record: subject `feat(repo): 조건부 큐 갱신과 실패 상태 조회 추가`; body marker `Goal-mode boundary: goal-1 task 16: Implement conditional queue updates`. The containing commit records this implementation and evidence under the configured user; verify git log -1 and clean status at closure.

## Additional checkpoint after Task 16

- Status: complete (scope/source/evidence review; no new runtime implementation or formal measurement)
- Work performed: previous goal turn is progress, verified by 9d70c9b4, its Task 16 marker, configured user, 36529f6c parent and exact 15-file scope. Reviewed Tasks 15a/16p/16 for requirements, code/ownership, data/failure/resource safety, build/type/test evidence, rollback and document/issue consistency. No new repair task is required by this bounded review. UI/UX is not applicable to these internal native/collector paths.
- Source evidence: scalar probes use only an already materialized one-row/one-column exact-type non-NULL relation; correlated subqueries stay separately refreshed and full WHERE still applies. Conditional queue operations hold the existing writer transaction, retain minimum/expiry/overflow/lease-fence and terminal readback, copy owned results and preserve caller rollback/UNKNOWN boundaries. Empty UNION copy guard has four public SQL regressions; original signal6 failure and DB remain preserved. Source-only review does not add independent-writer or physical power-loss proof.
- Verification evidence: build/checkpoint16-verification.json records 8,395 exact prior artifact entries, all 74 runtime source hashes equal to Task 16 Git objects and four exact optimized comparison builds. Rechecked 20 final native reports and empty stderr, four 62-check Python results, 87 collector checks and Ruff/format/mypy evidence against unchanged inputs; no new test-run claim. Three public docs pass check_doc.py, 172 local file targets exist, and git diff --check 6bb8b2e8 HEAD passes. Task 15a's historical reproduction/metadata correction remains separate from current-source registration and its original failure controls remain intact.
- Scope/provenance: Task 6 recovery remains verbatim against 36529f6c; Task 17 and every later checkpoint/task remain unchanged. main/origin/main/live main remain 6bb8b2e8, other four worktree heads and protected optimized builds are unchanged. Fresh #257/#268/#269/#239 reads preserve OPEN/checklists; 12 dependencies/nine children/board fields are unchanged. Runtime source/build identities match 9d70c9b4, but the existing eligibility evidence accurately remains unsealed; Task 17 must seal it to that actual implementation commit before clean-source formal registration. Do not use predecessor 36529f6c as the source of new binaries.
- Remaining risk: normal SQL reduction coexists with larger query reservations, 96KiB refusal and increased batch probes. Formal E3's two payload cells measure the fixed single-item put/claim/ack/purge cycle, not bulk-claim performance; keep the existing full/partial batch negative diagnostics. ON diagnosis is not OFF effect, and validation/control runs are not adoption. Default OFF, current COW and the WAL/buffer/MVCC/early-locking recommendation remain distinct. Original writer/stream/pending/scale/maintenance/product requirements are still incomplete; goal stays active.
- AAR: successful logs and matching hashes prove only the covered execution/source identity. Eligibility sealing is a later provenance action, and a single-item campaign cannot establish a general batching benefit. Keep failure stderr and contrary resource/scan evidence beside positive SQL counts; no new or repeated defect was found by this checkpoint.
- Next step: stop at this tracking-only checkpoint commit. Next continuation executes original Task 17, with verified commit sealing and preregistration before formal lifecycle/E3 samples. Retain the after 17/18 and all later checkpoints. No Task 17 execution, remote mutation, push/PR/merge/deployment, Vault write or source/default change in this boundary.
- Commit record: subject `docs(docs): 조건부 큐 갱신 검토와 실측 진입 조건 기록 갱신`; body marker `Goal-mode boundary: goal-1 checkpoint after task 16: complete`. This containing tracking-only commit records the checkpoint under the configured user; verify git log -1 and clean status at closure.

## Task 17 — Measure conditional SQL and optional lifecycle

- Status: complete (fixed-plan formal collection, verified regeneration and hold disposition; no runtime/default adoption or backend implementation)
- Work performed: previous turn is progress, verified by df37ea61, its after16 marker, 9d70c9b4 parent and tasks-only scope; initial checkout clean. Sealed 74 runtime source hashes and four actual build/test identities against 9d70c9b4 in `build/task17-conditional-eligibility.json`, preserving original unsealed evidence. Build/test execution predates that containing commit; exact Git-content equivalence was verified before registration without inventing clean-build-at-commit history. Reused Task16's minimum/expiry/fence/duplicate/stale/FAILED/cancel/OOM/readback/reopen regressions; no implementation/control campaign repeated.
- Preregistration: both `build/task17-{lifecycle,conditional}-formal.registration.json` files were persisted before any samples at collector/plan ancestor df37ea61, exact frozen workloads/thresholds, CPython 3.9.6/NumPy 1.26.4/SciPy 1.13.1 and control=false. Runtime source is 9d70c9b4, four optimized actual OFF/ON baseline/candidate builds, cache/workspace00. Eager class 54b43509 versus current lazy shares native/Queue code; E3 toggles only ACKQ_CONDITIONAL plus allowed instrumentation. Registration SHA256 starts 0d7ff6ac/29dd0d2e; exact hashes are in task17-before/disposition/verification JSON. Command logs stay outside reserved campaign prefixes; no source/criteria guard relaxation.
- Collection evidence: sequential lifecycle 72/72 in 17.027s (60 formal/6 preflight/6 diagnostic, 32 DBs per worker); E3 48/48 in 41.154s (40/4/4, 1,024 put/claim/ack/purge cycles per worker). Failed/unexecuted 0 in both; original worker 120s/campaign 1,800s/storage 8GiB/disk headroom rules unchanged. All worker stderr empty and registered runtime/source/build identities unchanged at collection. Formal E3 covers 40,960 cycles; formal lifecycle covers 1,920 synthetic DBs. Covered SQL/queue/reopen and ON integrity oracles pass; broader failure eligibility remains Task16 evidence.
- Verification/regeneration: actual CLI verify returns 72 and 48 with control=false. Each analyze runs twice with -W error and byte-identical JSON, hashes 43bc3d09/bcfaef47 in `build/task17-analysis-{lifecycle,conditional}.json`. `task17-archive-audit.json` independently rehashes all 2,352 gzip DBs against 393,805,824 uncompressed bytes. `task17-disposition.json` records exact cells/criterion failures/resources and descriptive open/execute/close plus ON/preflight ratios; descriptions are not adoption or OFF attribution. `task17-verification.json` seals final evidence.
- Measured disposition: both performance hypotheses hold. SQL-only lazy/eager throughput paired median 1.001, adjusted BCa 0.652–1.109; conditional 256B candidate/baseline 0.963, 0.723–1.044. Primary lower 1.05 and each cell's tail/CPU upper 1.05 are unmet; remaining throughput lower 0.95 also fails. Queue-lifecycle 256/4096 medians 1.067/0.936 and conditional 4096 median 0.972 do not override failed guards. RSS peaks 18.344/17.094MiB, formal paired RSS delta maxima -0.812/+0.406MiB, logical DB size ratio 1.000; passing resource guards do not substitute for effects. No equivalence, universal regression or isolated allocator benefit claim.
- Scope/limits: existing design/report record all five cells and lifecycle creation/execution/close costs, actual sources and local-only evidence/reproduction. Both Python sides retain the same public C API unlimited pool; 128MiB requested-byte cap is native UPDATE-only, Python uses RSS 256MiB and time/storage collection gates. No bounded C API or whole-process allocation-cap claim. Eager source loading is outside API timing but inside process/RSS; short shared-host runs, guards and reopen constrain attribution. E3 is single-item lifecycle, not batch/backlog/actual concurrent writer performance. Preserve Task16's 96KiB refusal and adverse batch probes; conditional stays default OFF, lazy stays for ownership/unused-object semantics without 5%/zero-cost claims.
- Preservation: 8,395 prior artifact entries, original optimized libraries/drivers/fingerprints, failing DB/binaries/logs, main/origin/main/live main 6bb8b2e8 and four other worktree heads unchanged. Fresh #268/#269/#239 bodies/comments/checklists and 12 dependency edges/nine children/board fields unchanged and OPEN. No product/bench/config/Python/YCSB change, remote mutation, consumer write, Vault write, publication or live conversion. WAL/buffer/MVCC/early-locking recommendation and matched buffered-COW comparison remain unchanged; original product requirements remain incomplete.
- AAR: fewer SQL boundaries did not establish whole-workload improvement. Favorable point estimates or RSS observations cannot bypass preregistered lower-bound/tail/CPU failures. Keep source-loading/RSS and native-requested-byte versus public-C-API boundaries explicit, preserving source proof and contrary evidence instead of claiming an unenforced cap.
- Remaining risk: both hypotheses held in this sampled scope; no silent threshold/method change or success-only recollection. Pending/caller memory, basic-key scale, independent writers, WAL/buffer/MVCC and buffered-COW fault/maintenance/total-cost results, streams and product integration remain unfinished. Goal stays active.
- Next step: stop at this task-boundary commit. Next continuation performs only the additional checkpoint after17, then original Task18 or a specifically evidenced repair. Preserve original after18 and all later checkpoints/tasks; no second task execution here.
- Commit record: subject `docs(docs): 조건부 SQL과 선택형 수명 실측 보류 결과 기록 추가`; body marker `Goal-mode boundary: goal-1 task 17: Measure conditional SQL and optional lifecycle`. The containing commit records this validated result under the configured user; verify git log -1 and clean status at closure.

## Additional checkpoint after Task 17

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — original-scope coverage, provenance-aware sample metadata repair and actual source/test/measurement status for Tasks 15a/16/17; review safety, resources, failure outcomes and document/issue consistency. Tracking-only checkpoint before Task 18, preserving the original after-18 and later checkpoints.
- Remaining risk: metadata regeneration must retain frozen results and source guards; fewer SQL calls or absent registry objects do not prove whole-app benefit or product completion.
- Next step: checkpoint in its own continuation after Task 17, then execute the first incomplete task or required repair.
- Commit status: no checkpoint commit attempted.

## Task 18 — Build and preregister pending-query measurements

- Status: incomplete
- Scope: Implement Q1–Q5 collector with fixed persist-queue version/serializer, equal requested result/state/order, metadata/payload pages, sequential consumption, full accumulation and failure/lifetime axes. Issues: #265.
- Work performed: not started.
- Verification evidence: required — Positive and corrupt-result controls; pinned page/backlog/payload/environment; fresh-worker baseline/peak/retained/released memory and native/Python/RSS/read/copy accounting.
- Remaining risk: Unsupported pending filters or assumed snapshot semantics can invalidate equivalence.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Checkpoint after Task 18

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — original-scope coverage, actual source/test/measurement status, safety/data/resource/compatibility and document/issue consistency for Tasks 16–18; tracking-only checkpoint.
- Remaining risk: defer missing implementation/validation repair explicitly, never mark the full goal complete from this checkpoint.
- Next step: checkpoint in its own continuation; then execute the first incomplete task or required repair.
- Commit status: no checkpoint commit attempted.

## Task 19 — Measure pending queries and the real caller boundary

- Status: incomplete
- Scope: Collect Q1–Q5 and audit the real consumer read-only. Compare bounded result consumption with full materialization, including caller accumulation, mixed-state and page-transition failures. Issues: #265.
- Work performed: not started.
- Verification evidence: required — Same decoded results and serializer, original workaround/unsupported scenarios, all raw/failed samples, memory/read/copy/deserialization results and reproducible report.
- Remaining risk: Do not claim the original problem solved if the caller still retains all payloads; add a repository-side repair if required, never edit the consumer without authorization.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 20 — Prepare the missing primary-key scale validation

- Status: incomplete
- Scope: Audit existing #158/#137 raw inputs and collector compatibility; reuse valid evidence and prepare the same-input 1k/10k/100k three-repeat scale/scan/latency collector without replacing old failures. Issues: #158.
- Work performed: not started.
- Verification evidence: required — Exact fixture and input/binary/environment/stop-rule registration, current PK path/scan counter checks, positive/negative controls; no unrelated campaign rerun.
- Remaining risk: Changing fixture loading or stop limits silently would no longer complete the original criterion.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 21 — Execute the retained primary-key scale validation

- Status: incomplete
- Scope: Collect the missing same-condition 1k/10k/100k three repeats, scan counts and p50/p95/p99 against the compatible SQLite input; preserve partial loads/timeouts separately. Issues: #158.
- Work performed: not started.
- Verification evidence: required — Full requested row counts, per-repeat correctness/reopen/scan and percentile evidence, raw hashes/aggregation; distinguish original historical and new baseline results.
- Remaining risk: If the required scale still fails, diagnose and queue a justified repair; timeout documentation alone is not goal completion.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Checkpoint after Task 21

- Status: incomplete
- Work performed: not started.
- Verification evidence: required — original-scope coverage, actual source/test/measurement status, safety/data/resource/compatibility and document/issue consistency for Tasks 19–21; tracking-only checkpoint.
- Remaining risk: defer missing implementation/validation repair explicitly, never mark the full goal complete from this checkpoint.
- Next step: checkpoint in its own continuation; then execute the first incomplete task or required repair.
- Commit status: no checkpoint commit attempted.

## Task 22 — Specify WAL and version recovery contracts

- Status: incomplete
- Scope: Resolve G1/G2/G4/G6 record/version/page layout, commit visibility, logical abort versus structural changes, LSN/full-page/checksum, checkpoint/GC and owner fencing for isolated candidate files. Fix result-query/admission bounds, the same logical payload/consumer-state layout for E4 and failure oracles. Issues: #238, #257, #263, #266.
- Work performed: not started.
- Verification evidence: required — Complete state/ownership/error/restart matrix and format invariants; explicit SNAPSHOT preservation, no uncommitted writeback without proof, local test format separate from existing DB. Select and justify G2's persistent request-ID/result lookup or explicit business-key corroboration, including absent/expired/changed-content IDs, process generations, response-body retention and unsupported automatic-retry boundaries; use the same contract for both candidates.
- Remaining risk: Redo alone cannot undo an uncommitted in-place overwrite; recovery/shared-state gaps block implementation.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

## Task 23 — Implement WAL records and durable commit recovery

- Status: incomplete
- Scope: Implement the candidate's log framing/checksum/order, transaction commit/outcome markers, append/barrier and restart scan without SQL/callback replay. Implement the selected G2 outcome-resolution storage path from Task 22, preserving its explicit unsupported/corroboration boundary if persistent lookup is not selected. Issues: #263, #266.
- Work performed: not started.
- Verification evidence: required — Truncate/short/torn/reordered I/O, checksum, crash before/after barrier and UNKNOWN outcome tests on isolated files; acknowledged commit preservation. Test the selected G2 restart/ID/content/retention behavior without treating missing or expired records as NOT_APPLIED or reexecuting callbacks.
- Remaining risk: A successful append is not a durable commit; keep old backend and files intact.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

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
