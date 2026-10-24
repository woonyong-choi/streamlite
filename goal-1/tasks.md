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

Completed-entry prose is compacted during Task 16 after a full reread. The exact precompaction ledger is preserved in existing Git object `36529f6c57e08a04923df55f4a2c9765f7e85ff9:goal-1/tasks.md` (SHA-256 1813c082a787753bcc2215539f8352b3224430f05764ce6272bd411813915985). Use `git show 36529f6c:goal-1/tasks.md` and the unchanged section title for full work, commands, failures, limitations and historical next steps. This is a digest in the same approved tracking file, not a new context copy or rewritten history. At compaction, Task 6 source coverage/decisions/matrix and every incomplete entry remain verbatim; later execution updates only its own entry. The rejected earlier overall completion remains superseded. Current first incomplete boundary is the checkpoint after Task 21, followed by Task 22; original requirements/checkpoints remain, with evidence-driven Task18a/18b and the completed additional after19 checkpoint.

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

- Status: complete (scope, provenance and measured-disposition audit; no new runtime implementation or formal samples)
- Work performed: previous goal turn is progress, verified by ba4c47ce, its Task 17 marker, df37ea61 parent, configured user and exact four-file documentation/tracking scope. Initial checkout clean. Reviewed Tasks 15a/16/17 and their checkpoint for requirement coverage, sample classification/source guards, ownership/failures/resources, build/type/test evidence, data preservation, rollback and report consistency. No new repair task is required by this bounded review; UI/UX is not applicable to these internal paths.
- Verification evidence: `build/checkpoint17-verification.json` records all 13,366 preserved artifact entries, 74 runtime source hashes and four comparison build identities. Actual raw schedule is 72/48, with 100 formal/10 preflight/10 diagnostic and no failed/unexecuted cases. Independently recomputed all five cells' ten paired throughput ratios/medians and checked each failed lower-bound/tail/CPU criterion plus passing RSS/storage guards. Both hold conclusions follow from the fixed criteria; no successful-only exclusion or new rule. Five effect rows and three descriptive lifecycle-phase rows match exact JSON.
- Reproduction evidence: after Task 17's document commit, actual `analyze --source-commit df37ea61` with -W error verifies the registered sources and raw manifests and regenerates both JSONs byte-identically (43bc3d09/bcfaef47). New `build/checkpoint17-{lifecycle,conditional}-reanalysis.json` and empty stderr preserve this check. This is offline reprocessing, not recollection or new effect evidence. Schema2 counts, plan ancestry, all criteria source hashes, exact four optimized builds and original failure/negative-control artifacts remain intact. Native/Python/collector/Ruff/mypy evidence is reused only with unchanged runtime/test/collector inputs; no new suite-run claim.
- Scope/resource review: whole-worker timing/CPU includes guards, close and reopen; descriptive phases and ON/preflight ratios do not enter adoption criteria. Eager source loading affects process RSS, and Python retains the existing unlimited C API pool equally on both sides; native 128MiB request cap is not a Python guarantee. RSS gate passing cannot establish a bounded C API, zero unused cost or original pending-list resolution. Conditional stays default OFF and Task16's adverse query/batch diagnostics remain. Current COW preservation, WAL/buffer/MVCC/early-locking recommendation and matched buffered-COW work are not changed by these single-writer/one-item measurements.
- Preservation/documentation: no runtime/bench/config/test/Python change since 9d70c9b4. Three public docs pass check_doc.py, 173 local file targets exist and git diff --check 6bb8b2e8 HEAD passes. main/origin/main/live main remain 6bb8b2e8; other four worktree heads and original optimized/failure/raw/YCSB evidence are unchanged. Fresh #268/#269/#239 readback and 12-edge/nine-child board graph match Task 17; #265 remains OPEN with four unchecked requirements. Task 6 recovery and Task 18 onward remain verbatim. No remote mutation, Vault/consumer write, push/PR/merge/deployment or data conversion.
- AAR: matching hashes do not establish interpretation; independently preserve pairing, denominator, sample class and criterion direction. Reprocessing against the registered ancestor proves reproducibility after document changes without relabeling old sources or generating new samples. A completed negative experiment does not complete original product requirements. No new defect or stale-context repair was identified here.
- Remaining risk: five-cell uncertainty and attribution limits remain. Q1–Q5 still needs equal serializer/results/state/lifetime, byte budgets, native/Python/RSS/read/copy observations and real-caller accumulation; current APIs have no assumed pending-state filter or cross-page snapshot. Original scale, actual writers, maintenance-inclusive storage/product comparisons, stream and integration work remain unfinished. Goal stays active.
- Next step: stop at this tracking-only checkpoint commit. Next continuation executes original Task 18's collector/preregistration only, choosing and recording its reversible inputs/guards before collection; Task 19 later measures and audits the consumer read-only. Keep original after18 and every later task/checkpoint. No Task 18 execution in this boundary.
- Commit record: subject `docs(docs): 수명·조건부 실측 검토와 조회 측정 경계 기록 갱신`; body marker `Goal-mode boundary: goal-1 checkpoint after task 17: complete`. The containing tracking-only commit records this checkpoint under the configured user; verify git log -1 and clean status at closure.

## Task 18 — Build and preregister pending-query measurements

- Status: complete (로컬 수집기·사전 판정·정상/실패 대조만 완료; 정식 효과·소비자 교체·새 backend 미완료)
- Work performed: clean 18167df8에서 같은 checkout의 feat/265-repo-pending-query-measurements로 #265를 bind했다. Q1–Q4의 vendor persist-queue1.1.0/호환/native 경로, 동일 JSON·ID/status/timestamp/body digest와 Q5 실패·수명을 구현했다. 새 worker·수집/검증/분석·JSON 계획·native ledger/scan/pread driver와 C 실패 대조는 한 검증 단위다. src/include/python·기본 backend·공통 비용 기준은 바꾸지 않았다. 기존 설계·빌드 문서·개발 보고에 명령과 계측 한계를 기록했다.
- Preregistration: backlog128/1024/10000, body256/4096B, page16/100, 네 과제·세 조건, 대응10 block+사전1+진단1을 고정했다. 예정1728건은 정식1440/사전144/진단144이며 이 경계에서는 수집하지 않았다. seed26518 인접 역순으로 각 비교 앞뒤5회, 계획 commit의 정확한 SHA/조상·clean source·일정·runtime Git 내용·interpreter/vendor/SQLite/환경·OFF/ON binary/새 driver hash를 등록한다. 초기 문안의 family216은 실제 계산하는 96cell×wall/RSS 두 지표인192로 정식 수집 전에 바로잡았다. 95%·percentile bootstrap20000, 퇴화 보류, 메모리 증가는 유한 격자 진단이다. 모든 한도·허용폭은 연구 가정이며 고객 SLO가 아니다.
- Measurement boundary: baseline/held/released/closed RSS snapshot·고수위, open 이후의 tracked Python baseline·query peak/증가분·GC 객체, query/전체 process wall·CPU를 구분한다. 진단만 load/bytes·C 목록/free·payload 복사·겹친 ItemInfo 수명을 센다. 별도 native driver는 128/32MiB ledger와 반환 payload/caller metadata·scan/index/pread를 관측한다. 열기 임시·큐의 별도 malloc·CAPI/Python 배열 전체를 ledger가 계상하지 않으며, Python CAPI는 여전히 unlimited이고 RSS/tracemalloc 수치는 사후 수용 gate이지 OS 강제 할당 상한이 아니다. Q4 전체 축적은 Python worker에서 확인하고 물리 I/O·전체 allocator peak·상태 필터·page 간 snapshot을 주장하지 않는다.
- Verification evidence: build/task18-verified-pytest.log의 현재 입력 29검사와 변경 없는 공통 수집기 87검사(build/task18-closure-pytest.log)를 대조한다. 작은 control=true36건은 각 과제/조건의 동일 결과·12/12/12 분류·재생성을 검증하며 효과 표본으로 쓰지 않는다. 새 C 대조는 payload malloc4곳·session reserve366곳에서 부분 결과 0·잔여 소유권 0·다음 조회·close 복귀를 fatal UBSan과 빈 stderr로 확인했다(build/task18-native-fault.stdout/stderr). 혼합 상태·빈 결과·0/음수/상한·payload 거절·중단·두 번째 page 변환 실패·serializer MemoryError·cancel·source/binary/일정/raw/한도 손상·timeout 보존을 검사했다. 원자료 fsync 뒤 deadline에서 completed가 적게 기록되는 순서를 수정하고, 완료1/시도1/실패case0/미실행35와 campaign 실패를 별도로 남기는 대조를 추가했다. page 기준군의 상태 정규화도 최종 선택 page에만 적용해 불필요한 전체 추가 처리를 제외했다. 최종 CLI verify/analyze와 hash는 build/task18-verification.json에 연결한다. Ruff/format/mypy, 두 native 빌드, 문서 검사·상대 링크·whitespace를 검증했다.
- Failed/control preservation: 최초 control은 SQLite -shm hash 변화를 데이터 변조로 잘못 분류해 2완료/3시도/33미실행으로 멈췄다. 원 registration/raw/manifest·DB·stdout/stderr를 그대로 두고 데이터 파일만 불변 입력으로 검사하도록 수정했다. pool 자체 상주 bytes를 0으로 요구한 새 driver의 첫 검사 오류도 empty-pool 기준으로 고쳤다. 당시 첫 진단 호출은 도구 출력과 같은 입력만 남고 수정 전 driver binary를 별도로 고정하지 못했으므로 그 bytes의 재현은 주장하지 않는다. 중간 미커밋 control의 source hash는 현재/정식 근거로 재해석하지 않는다. 별도 pytest 첫 실행의 잘못된 CANCELLED enum 기대값(17→실제16) 실패 로그도 보존했다. 새 실패 주입 표본·오류·부분 자료와 기존 실패 DB는 삭제하지 않았다.
- Scope/readiness: build/task18-preservation.json은 기존13366 artifact hash·main/origin/main 6bb8b2e8·보호 runtime/common collector/YCSB를 재확인한다. 다른 네 worktree HEAD와 Task6·Task19 이후·input.md/goal-current는 그대로다. GitHub 연결 도구에 없는 의존/보드 기능만 gh로 처리해 #265의 해결된 #257 query-contract edge 하나를 제거하고 보류→대기→진행으로 옮겼다. 실제 재조회는 이 변화만 확인하며 나머지11 edge/9 child와 issue body·4미체크·OPEN을 보존했다. #257 전체 설계·제품 gate는 완료하지 않았다. consumer/Vault 쓰기·push/PR/merge/배포/포맷 전환 없음.
- AAR: SQLite 임시 공유 상태와 논리 데이터의 불변성을 구분해야 하며, 해제 뒤 정상 pool 상주 메모리를 누수로 세면 안 된다. profiler는 open 전부터 켜되 query peak는 결과 감사 전에 읽어 baseline·보유·감사를 섞지 않는다. 첫 새 진단 binary도 실패 순간 별도 고정해야 한다. bootstrap 구간 수와 완료/시도/실패case/미실행은 실제 코드에서 다시 계산하며 작은 대조를 정식 효과로 부르지 않는다.
- Remaining risk: 10000개 정식 전체 격자·원 소비자 상태/축적 수명·RSS/시간 효과는 Task19에서 아직 검증해야 한다. 수치 상한이 현재 CAPI 전체에 구현된 것은 아니다. frozen vendor/플랫폼과 작은 대조 범위를 넘어선 결과, 실제 writer·WAL/buffer/MVCC/early locking·matched buffered-COW·stream·scale·제품 비용은 미완료다. 현재 WAL 권고는 유지하며 goal은 active다.
- Next step: 이 task-boundary commit에서 멈춘다. 다음 continuation은 원래 checkpoint after18만 수행하고, 그 뒤 Task19가 이 수집기/계획을 담은 commit으로 새 등록 후 정식 수집과 consumer 읽기 전용 감사를 진행한다. 모든 후속 Task1–39·수리·checkpoint를 보존한다.
- Commit record: subject `feat(repo): 목록 조회 수집기와 메모리 사전 기준 추가`; body marker `Goal-mode boundary: goal-1 task 18: Build and preregister pending-query measurements`. 이 기록을 포함하는 커밋은 설정된 사용자 작성자로 검증한 수집기와 근거를 담으며, 경계에서 git log -1과 clean 상태를 확인한다.

## Checkpoint after Task 18

- Status: complete (원래 범위·구현/실측 근거 검토 완료; CP18-1/2 검증기 수리는 미완료로 추가)
- Work performed: 이전 turn은 progress다. b9cfef4a의 Task18 marker·18167df8 parent·설정된 사용자·13파일 변경과 시작 시 clean 상태를 확인했다. Tasks16–18의 요구·소스·소유권·실패/자원·빌드/타입/시험·문서·복원 경계를 대조했다. UI는 내부 CLI/자료 검증에 해당 없음이다. 현재 제품 source는9d70c9b4와 같고 조건부 기본OFF·lazy 소유 계약·WAL 권고와 동등 buffered-COW 비교를 유지한다.
- Verification evidence: build/checkpoint18-verification.json과 build/checkpoint18-preservation.json은 기존13366+Task18의1228=14594 artifact 항목, Task18 기준 source10개와 runtime79개의 실제 Git 내용을 대조했다. b9cfef4a에 source가 고정됐다는 사실은 그 이전에 수행한 build를 clean-build-at-commit으로 바꾸지 않는다. 29 pending/87 공통 회귀·malloc4/reserve366 fatalUBSan 및 빈 stderr는 입력·binary·로그 hash가 같은 범위에서 재사용했고 새 suite나 정식 표본은 만들지 않았다. 문서3개·local link182개·whitespace와 main/origin/live main6bb8b2e8·다른 네 worktree HEAD를 확인했다.
- Reproduction/source review: 원본 pending control36건은 현재 verify를 통과하고 analyze 재생성은 기존 JSON과 byte 동일하다(build/checkpoint18-pending-reanalysis.json, sha256864bf53cff14dbe1e0270a5b791a8ca425a0e69665f02c734172038e7e2cbceb, stderr 빈값). 등록 일정1728/정식1440·조건 순서5/5·동일 serializer/result·Q1–Q5 수명/오류와 baseline/peak/held/released 범위, ledger와 무제한 Python CAPI/RSS 사후 gate의 차이를 소스에 대조했다. 작은 control과 유한 격자 진단은 원 소비자 해결·전체 allocator cap·실제 writer/새 backend/stream 증명이 아니다.
- CP18-1: pending verify가 manifest.artifacts의 주어진 항목만 훑고 필수 hash 집합의 완전성을 검사하지 않는다. raw·stdout·stderr·fixture DB hash를 각각 빼거나 map을 비운 다섯 대조 모두36건을 수용했다. raw hash를 뺀 뒤 가상 wall_ns252458→504916으로 바꿔도36건을 수용했다. 실제 기능의 Path.read_text만 메모리에서 바꾼 읽기 전용 대조이며 원본 raw/manifest bytes는 변하지 않았다. build/checkpoint18-artifact-coverage-controls.json이 입력·관측·원본 hash를 보존한다. Task18a로 수리하기 전 새 정식 수집을 시작하지 않는다.
- CP18-2: 공통 verify에도 같은 선택적 hash-loop가 남아 있다. 기존 Task17 conditional48건과 등록 source df37ea61을 명시한 정상 대조 뒤 raw hash 누락·전체 map 누락·raw hash 누락+가상 wall_ns568756333→1137512666을 각각 수용하는 세 반례를 재현했다(build/checkpoint18-common-coverage-controls.json). 원본 파일은 그대로이며 이 대조는 새 성능 표본이 아니다. 외부 보존 장부의14594개 hash가 일치하므로 기존 관측값·보류 결과를 바꾸거나 재수집하지 않는다. Task18b로 독립 수리하고 과거 source 처리도 강화된 자료 coverage 검사 뒤에만 허용한다.
- Scope/remote: #257/#265/#268/#269/#239는 OPEN·미체크2/4/3/3/4이며 #265 body는 그대로다. 실제 보드11edge/9child와 상태가 Task18 종료 때와 같다. Task6 복구·기존 Task1–39/수리/원래 checkpoint·input/goal-current를 보존한다. 이 checkpoint는 tasks.md만 추적 변경하며 implementation·plan·기준·remote·Vault·consumer는 수정하지 않았다. push/PR/merge/배포·데이터 전환 없음이다.
- AAR: 파일 hash가 맞는 항목을 검사하는 것과 필수 파일 모두의 hash가 있다는 것은 다른 조건이다. 잘못된 digest·JSON·개수 거부와 같은 JSON 재생성만으로 자료 완전성을 보장할 수 없었다. 등록 일정과 fixture에서 요구 파일을 도출하고 누락을 먼저 거부하는 반례를 두 검증기에 각각 넣어야 한다. checksum 보강을 악의적인 전체 장부 교체에 대한 인증 보장으로 확대하지 않는다.
- Remaining risk: CP18-1/2가 남아 있어 정식 수집 적격성은 보완 필요다. 현재 원자료 손상이나 기존 수치 변경은 확인되지 않았다. 기존 driver 첫 실패 binary 미고정 한계는 그대로 명시하며 되살린 것처럼 쓰지 않는다. 원 pending 소비자·규모·실제 writer·정리 포함 COW/WAL·stream·제품 검증은 여전히 미완료이고 goal은 active다.
- Next step: 이 tracking-only commit에서 멈춘다. 다음은 Task18a pending guard만 수행한다. 이후18b 공통 guard, 원래19와 추가 checkpoint after19 순서다. 두 수리를 함께 담은 실제 source/계획 commit으로 Task19를 새 등록하며 workload·기준·표본 수를 바꾸지 않는다. 원래 after21과 모든 후속 checkpoint는 그대로 둔다.
- Commit record: subject `docs(docs): 조회 검토와 자료 해시 검증 수리 과업 추가`; body marker `Goal-mode boundary: goal-1 checkpoint after task 18: complete`. 이 기록을 포함하는 tracking-only commit의 marker·작성자와 clean 상태를 경계에서 확인한다.

## Task 18a — Require complete pending-query artifact coverage

- Status: complete (로컬 pending 자료 coverage 수리와 회귀만 완료; 공통 수리·정식 효과·제품 전환 미완료)
- Scope: CP18-1의 pending manifest 검증 누락을 수리한다. 실제 등록 일정·fixture 구조에서 raw/fixture 등록·필수 DB·각 worker와 native probe stdout/stderr의 요구 집합을 도출하고, 누락·잘못된 경로·중복 alias·잘못된 hash를 결과 분석 전에 거절한다. 합법적인 보존 부속 파일은 별도로 검증하며 늦게 만든 분석 출력까지 무작정 같은 필수 집합으로 삼지 않는다. Issue: #265, 기존 checkout/branch 재사용.
- Work performed: clean f31457fd·#265 소유권과 기존 branch에서 시작했다. 등록 계획·일정으로 필수 파일을 도출하고 manifest의 누락부터 거절한다. control은 필수89/부속3/fixture2, 정식 일정은 필수3591/부속18/fixture12다. SQLite WAL/SHM·native lock은 존재하면 hash가 필요하고 기재한 부속 파일은 실제 bytes를 대조한다. 허용 집합 밖 경로·상대/비정규 경로·symlink·중복 JSON key·잘못된 SHA256과 fixture 두 등록의 경로/hash 불일치를 거절한다. 늦게 생성한 분석 출력은 필수 집합으로 삼지 않는다. 기존 수집기 안에서 국소 수리해 source pin 목록·숫자 계획·통계·제품 source를 보존했다. 기존 설계/빌드 문서와 Continuity만 갱신했다.
- Regression evidence: build/task18a-before-pytest.log는 같은 public verify 반례 여섯 개가 수정 전 모두 수용되어 예상대로 실패한 기록이다. 수정 후 `PYTHONPATH=python:bench:.local/runs/local-db-comparison/deps-queue TOTEDB_LIB=$PWD/build-regression/conditional-baseline-on/lib/libtotedb.dylib build/check-env/bin/python -W error -m pytest -q -s tests/test_pending_query.py`가65검사를 통과했다(build/task18a-pytest.log). 기존29개와 새 coverage36개를 포함하며, fixture/native 로그·부속 hash·alias/digest·등록 불일치·분석 출력 공존과 기존 실패/중단/소유권 대조를 확인했다. 경로/symlink/JSON 변형 검사는 읽기·resolve의 메모리 대조이며 원본을 손상시키지 않았다. native 실패 sweep도 고정된 기존 binary로 재실행했고 stderr가 비었다. 새 C 빌드나 정식 효과 실행으로 세지 않는다.
- Verification evidence: build/task18a-controls.json에서 새 current-source 정상36건은 통과하고 CP18-1 여섯 반례는 raw JSON 읽기0회에서 거절했다. build/task18a-historical-coverage.json은 b9cfef4a 원자료의 새 자료 검사 component만 정상/여섯 누락 거절을 확인한다. 과거 등록의 default verify는 여전히 source 변경으로 거절하므로 과거 자료를 현재 표본으로 바꾸지 않았다. CLI verify36/control=true와 analyze 두 번이 통과하고 byte 동일 SHA256 c32c02cb90cb0abe33add8548c84890bff261e095e14fbd5110a306d6871ea00이다(build/task18a-cli.json). Ruff/format120/mypy·두 문서·whitespace 검사 모두 통과했다.
- Preservation evidence: build/task18a-verification.json은 기존14601 artifact 항목(기존14594+checkpoint18의7개), 새 실행·실패 자료, current source10/runtime79·고정 binary를 확인한다. 원본 raw/manifest hash3f23975b/fbe6a990와 실패 DB·중간 control·원자료를 유지했다. main/origin-main6bb8b2e8와 다른 네 worktree HEAD는 같다. 공통 수집기·제품/runtime·고정 계획·분석/worker/Cdriver·Makefile은 f31457fd와 차이 없다. current-source/plan-ancestor·완료/실패·deadline·자원 gate는 원래 구현을 유지한다. #265 OPEN·4미체크·사용자 소유를 확인했고 원격 변경·consumer/Vault 쓰기·push/PR/merge/배포·데이터 전환은 없다.
- Remaining risk: Task18b의 공통 검증기 누락은 아직 미완료다. 그 source도 pending 등록에 포함되므로 Task19는 두 수리를 담은 commit으로 새 등록해야 한다. checksum coverage는 전체 장부 교체에 대한 인증이 아니다. 예전 첫 driver 실패 binary 미고정 한계와 원 pending 소비자·정식 전체 격자·scale·실제 writer·COW/WAL 정리/복구 비용·stream·제품 검증은 그대로 남고 goal은 active다. WAL/buffer/MVCC/early-locking 권고와 동등 buffered-COW 비교를 유지한다.
- AAR: hash-loop의 성공과 요구 자료 집합의 완전성은 별도 조건이다. 필수 집합을 manifest 자체나 현재 glob에서 추론하면 누락 또는 늦은 분석 파일 때문에 잘못 판정한다. 계획·일정에서 도출하고 합법적인 부속 파일은 별도 취급한다. 수정 전 실패/수정 후 거절과 과거 component 검사/default source 거절을 나눠 기록했다.
- Next step: 이 task-boundary commit에서 멈춘다. 다음 continuation은 Task18b만 수행하며 공통 과거 source 재분석과 새 pending 통합 guard를 검증한다. 이후 원래19와 추가 checkpoint19이며 후속 모든 원래 과업/경계를 보존한다.
- Commit record: subject `fix(repo): 목록 조회 자료 해시 누락 검증 수정`; body marker `Goal-mode boundary: goal-1 task 18a: Require complete pending-query artifact coverage`. 이 기록을 포함하는 커밋의 작성자·marker와 clean 상태를 경계에서 확인한다.

## Task 18b — Require complete common-cost artifact coverage

- Status: complete (로컬 공통 자료 검증 수리·과거 재처리·연동 대조 완료; 새 정식 효과·제품 전환 미완료)
- Scope: CP18-2의 공통 manifest 검증 누락을 독립 수리한다. 단계별 등록 일정과 fixture/보존 구조에서 필수 raw·stdout/stderr·DB 또는 검증된 gzip 보존 자료의 요구 집합을 확인한다. 현재와 명시적 source-commit 과거 처리를 모두 자료 coverage 검사에 통과시킨 뒤 원래 분석을 실행한다. Issue: #239. 같은 checkout에서 기존 issue branch/소유권을 대조하고 모든 선행 commit을 보존한다.
- Work performed: 이전 turn은 e028684f의 Task18a marker·설정된 작성자·clean 상태로 확인한 progress다. #239 OPEN·사용자 소유·4미체크와 열린 동일 이슈 PR 없음, 다른 worktree 소유권을 대조했다. 기존 feat/239-repo-common-cost-collector를92c2ec61에서e028684f로 같은 checkout에서 fast-forward하고 과업을#239에 bind했다. 새 common_cost_artifacts.py가 일정에서 필수 raw·등록·worker/fixture 로그·DB gzip을 도출한다. 누락·비정규/외부 경로·alias·symlink·중복 JSON key·digest를 거절하고, gzip 역해시·해제/압축 bytes·storage 합계·fixture hash를 검증한다. 새 API collect도 등록 JSON을 보존하고 다른 내용이면 시작 전에 거절한다. 분석 출력은 필수 집합이 아니다. 717줄 수집기에 구현을 몰지 않고 새 helper로 분리했다.
- Source/compatibility: 새 기준 source는 helper를 포함한6개다. 명시적 과거 처리는 해당 로컬 조상 commit의 실제 기준 파일 목록을 Git에서 도출해 등록 목록/hash와 대조하므로 과거5개 형식도 유지한다. default의 현재 source 검사와 과거의 조상·내용 검사는 그대로다. metadata correction의 processor hash는 새 helper까지3개를 기록하며 이전2개 hash의 원 JSON은 보존한다. pending source pin에도 helper를 추가했고 변경된 helper hash를 거절했다. plan·통계 계산·원 효과·제품/runtime·binary·기본값은 바꾸지 않았다.
- Regression evidence: build/task18b-before-pytest.log는 세 단계×세 누락 유형×현재/명시 source의18반례가 수정 전 모두 수용돼 예상대로 실패한 기록이다. 최종 `PYTHONPATH=python:bench:.local/runs/local-db-comparison/deps-queue TOTEDB_LIB=$PWD/build-regression/conditional-baseline-on/lib/libtotedb.dylib build/check-env/bin/python -W error -m pytest -q -s tests/test_common_cost.py tests/test_common_cost_artifacts.py tests/test_pending_query.py`는227검사를 통과했다(build/task18b-pytest-final.log). 이후 추가한 등록 보존/불일치2검사와 legacy 자료 component3검사는 별도 명령으로 통과했다(각 task18b-registration-pytest.log, task18b-legacy-component-pytest.log). 단일232검사 실행으로 합치지 않는다. Ruff/format120/mypy·기존 두 문서·whitespace 검사도 통과했다.
- Current controls: build/task18b-controls-and-cli.json에서 cache72/lifecycle12/conditional6/pending36의 control=true CLI verify와 각 analyze 두 번의 byte 동일성을 확인했다. 이 conditional 대조는 archive 구조 검증을 위해 baseline/candidate에 같은 기존 binary를 연결한 시험 fixture이며 후보 효과·정식 E3 표본이 아니다. 정식 register의 후보 구분·eligibility guard는 변경하지 않았다. 공통 원 CP18-2의 Task17 conditional48건에서 raw hash 누락·빈 map·raw hash 누락+가상 wall 변경 세 반례가 raw JSON 읽기0회에서 거절됐다. 원본 실물은 변하지 않았다. 새 current-source pending은11개 source를 고정하며 helper hash 변경도 분석 전에 거절했다.
- Historical evidence: build/task18b-historical-cli.json은 Task15 cache576건(source94552080), Task17 lifecycle72/conditional48건(sourcedf37ea61)의 강화된 public verify와 각 두 번 분석을 기록한다. 보존한 원 분석 JSON과 각각 byte 동일하며 새 효과 수집이 아니다. 실제 gzip 원 DB bytes까지 재검증했다. 추가 --correct-metadata CLI도 기존 수정 JSON의 효과·구간·판정·표본 수와 같고 바뀐 현재 처리기3개 hash만 반영했다(task18b-corrected-metadata-check.json). default는 과거 기준 source를 현재 것으로 받아들이지 않는다.
- Failure/limits: 첫 수정 후 실행은151통과/2실패였다. 새 등록 JSON 저장에도 기존 fsync/_write_new 시간 주입이 걸려 대상이 바뀌었으므로 raw 파일 존재와 manifest 경로로 원래 주입 경계를 특정했다. 한도·실패 기준은 완화하지 않았다. 최초·최종 pytest 임시 자료는 기존 build의 tar.gz에 보존하고624/620 파일 역해시를 대조했다. 수정 전 API control 등록 객체를 Git 코드로 재구성한 시도는 원 semantic hash와 달라 실패했다(task18b-legacy-reconstruction-failure.json). 원 객체에 있던 당시 df snapshot도 없으므로 정확 재현을 주장하지 않는다. 등록 파일 없는 legacy 형식은 읽기/exists를 바꾼 component 모델3검사로만 확인했고 실제 과거 정식 자료 재현과 구분한다. 새 collect의 등록 보존은 이 공백의 재발을 막는다.
- Preservation evidence: build/task18b-verification.json은 기존14837 artifact 항목과 Task18a seal2개, 새1793 실행 자료·시험 archive·현재 source/binary를 확인한다. main/origin-main6bb8b2e8와 다른 네 worktree HEAD를 보존했다. 원 Task15/17 registration/raw/manifest·이전 실패DB·분석·YCSB와 Task18a 원자료는 변경하지 않았다. 원격 이슈·의존·보드 변경, consumer/Vault 쓰기·다른 작업본 실행·새 clone/백그라운드·push/PR/merge/배포·데이터 변환은 없다.
- AAR: digest 검사는 자료 집합의 완전성이나 gzip 원본 의미를 대신하지 않는다. 새 helper가 기준 목록을 늘리면 과거 commit의 실제 파일 집합을 읽어야 하며 현재 집합을 그대로 과거에 강요하지 않는다. 소스만으로는 당시의 동적 환경과 저장하지 않은 등록 객체를 정확히 복원하지 못한다. 시험 주입은 같은 fsync 함수 전체가 아니라 의도한 저장 경계를 겨냥해야 한다.
- Remaining risk: checksum은 전체 장부 교체의 인증이 아니다. 과거 첫 driver 실패 binary 미고정과 중간 control 등록 객체 미보존의 재현 한계는 남긴다. 정식 pending 전체 격자·실제 caller·scale·독립 writer·WAL/buffer/MVCC/early locking·동등 buffered-COW의 정리/복구 총비용·stream·제품 통합은 미완료다. 현재 WAL 권고를 유지하며 goal은 active다.
- Next step: 이 task-boundary commit에서 멈춘다. 다음 continuation은 원래 Task19만 수행한다. 두 수리와 새 helper를 모두 담은 이 commit을 실제 plan/source 조상으로 새 등록한 뒤 정식 pending 수집·소비자 읽기 전용 감사를 진행한다. 이후 추가 checkpoint19와 모든 원래 후속 과업을 보존한다.
- Commit record: subject `fix(repo): 공통 비용 자료 해시와 보존 DB 검증 수정`; body marker `Goal-mode boundary: goal-1 task 18b: Require complete common-cost artifact coverage`. 이 기록을 포함하는 커밋의 작성자·marker와 clean 상태를 경계에서 확인한다.

## Task 19 — Measure pending queries and the real caller boundary

- Status: complete (고정 계획의 전체 격자 수집과 읽기 전용 감사 완료; actual caller replacement, whole-app solution and new backend remain incomplete)
- Scope/work: 이전 Task18b는 c0cb4216의 marker·설정된 작성자·clean 상태로 확인한 progress다. #265 OPEN·사용자 소유·4미체크를 확인하고 같은 checkout의 기존 feat/265-repo-pending-query-measurements를 e028684f에서 c0cb4216으로 fast-forward해 bind했다. 첫 미완료19만 수행했다. 제품/수집기/시험/계획 숫자·binary·기본값은 바꾸지 않았다. 기존 설계·빌드 안내·개발 보고에 범위가 제한된 판정과 반대 관측을 기록했다.
- Preregistration: `build/task19-pending-formal.registration.json`은 clean plan/source 조상 c0cb4216ed75d608b9595d3dd2e01da662483e15·source11개·runtime9d70c9b4의79개 Git 내용과 실제 OFF/ON/driver/vendor/interpreter hash를 고정했다. control=false, 등록 SHA256 86cc13127bdff470babc976aca21bd9f472ba993ebc21d16d507722c78a5bf42. 기존 backlog128/1024/10000·body256/4096·page16/100·네 과제/세 조건·대응10+사전1+진단1·seed26518·앞뒤5/5·percentile20000/95%/family192와 모든 gate를 유지했다. macOS26.5.2 arm64·CPython3.9.6·SQLite3.51.0·NumPy1.26.4·SciPy1.13.1의 등록 환경이며 body는 본문 길이이지 JSON 전체 bytes가 아니다.
- Collection evidence: `build/task19-collect.stdout/stderr`와 manifest는 예정1728/시도1728/완료1728, 실패case0/미실행0/실패목록빈값, 569.330437042초를 기록한다. 정식1440·사전144·진단144이며 fixture12·필수3591+부속18=artifact3609 hash를 확인했다. 실제 수집 자료158199862B와 RSS/time/storage/headroom gate를 보존했다. raw SHA256 b246393ca6352d4a00b524df6d2ac96c6947e51116f1e3ce530a9bca4faa64b1, manifest 2bb05cafa78369c5127556079de7ae35d94d414354a0872adf9e6c8eb40b6664. 새 prefix만 사용했으며 실패·기존 원자료를 덮지 않았다.
- Verification evidence: 실제 `build/check-env/bin/python -W error bench/pending_query.py verify --prefix build/task19-pending-formal`은1728/control=false를 확인한다. 같은 `analyze --prefix` 두 번은 byte 동일 SHA256 8a2cd02e4b3281373248e5617ee3b1423820ca18be1cc90f21c7422b66682fc3이며 모든 worker/probe/CLI stderr가 비었다. `build/task19-cli.json`에 정확한 argv/exit/hash가 있다. 수집기 분석 helper 없이 raw의 기대 결과 digest1728개·주 대응 percentile 구간16개·증가 기준12개를 별도 계산했고, 96cell의5/5교대도 일치했다(`task19-independent-audit.json`, `task19-disposition.json`). 이전 Q5 소규모65검사/공통 연동227과 native 할당 실패·수명 evidence는 source/시험/binary/hash가 같은 범위만 재사용했다. 새 suite·빌드·CI 또는 큰 입력의 Q5 실패 실행으로 세지 않는다.
- Measured disposition: `support_sampled_scope`. N10000 Q1/Q2의16개 wall/RSS gate와 Q1–Q3의12개 성장 gate가 모두 통과했다. 주 wall/RSS 비 상한의 최대0.458079/0.638291, native query/Python/RSS 증가 최대2976/331695/2555904B다. 모든 worker RSS최대93.48MiB, 진단 Pythonpeak최대49.25MiB이며 별도 Cprobe 요청query/shared최대68530/2550110B다. 이는 고정 유한 격자이며 Python CAPI 전체 cap·OS 강제 상한·물리 I/O·일반적 점근성 보장이 아니다.
- Contrary observations: 작은 Q1/Q2의 vendor 대비 wall 중앙값은 N128의 8cell 중 6개, N1024의 8cell 중 2개가 1보다 컸다. 최악 작은 cell은 10.065·조정 구간 9.063–11.573이다(`task19-small-backlog-audit.json`). N10000 Q3의 vendor 대비 중앙값은 약 3.0–13.6배이며 4개 구간 중 하나는 1을 포함한다. Q4 축적의 큰 본문 진단은 native Python peak/RSS 47.41/87.03MiB로 vendor 49.25/93.48·compat 47.47/87.27MiB와 가까웠다. 설명 지표를 본 뒤 사전 gate를 바꾸거나 성공 cell만 다시 수집하지 않았다. 전체 조회 C probe의 101page·색인 30101회·성공 pread 575700992B는 반환 41198894B와 다르다. 행별 note/created/payload 반복은 공통 SQL/조회 비용 후보다. 계측 ON을 OFF 시간 기여율이나 WAL 전환 시 제거될 비용으로 계산하지 않는다.
- Consumer audit: 보관된 Markchain checkout HEAD 4f1cdf8d의 runtime.py448–453 복구 loop와 종료/취소/재시도/cleanup을 읽었다(`task19-consumer-source-audit.json`). runtime/수명 시험은 HEAD와 같고 pyproject/uv.lock은 기존 dirty이며 persist-queue1.1.0 pin은 설치/실행 버전 증명이 아니다. 원 loop AST만 합성 list/가상 iterator producer에 적용한 8개 대조는 같은 비ACK 768건·4096B body 합계 3145728B를 producer 반환 뒤에도 유지했다(`task19-consumer-restore-control.json`). map을 비운 뒤 항목 0은 전체 참조/메모리 해제 측정이 아니다. 기본 256건·논리 payload64MiB는 enqueue admission이며 restore는 전체 복원 뒤 cleanup한다. vendor의 clear_acked_data 기본 max_delete1000은 잔여 ACK 이력도 가능하므로 256을 저장된 전체 행 수 상한으로 쓰지 않는다. 실제 소비자 프로세스/DB는 실행·수정하지 않았다.
- Preservation/closure: `build/task19-verification.json`은 기존 16630 artifact 항목·직전 seal4개·새 정식 artifact3609·source11/runtime79·고정 identity를 재검증한다. main/origin/main6bb8b2e8와 다른 네 worktree HEAD, 원 raw/YCSB/실패 표본을 보존했다. 소비자 4파일 hash와 추적 dirty8경로 상태를 재확인했다. 종료 때 미추적 7경로도 관측했으나 사전 목록이 없어 시간상 보존을 전수 확인했다고 쓰지 않는다. 첫 보존 검사에서 tracked-only 사전 상태를 기본 status와 비교한 오류를 고쳐 같은 옵션끼리 비교했고, 내용을 변경해 검사를 맞추지 않았다(`task19-preservation-check-repair.json`). 문서3개 check_doc·수치표16+4+작은4행·local file targets187·whitespace를 확인했다. Task6 복구·Task18a/18b·추가 checkpoint19 및 원래20–39를 보존한다. `task19-closure.json`은 최종 추적 파일과 자료 seal을 고정한다. 원격/Vault/consumer 쓰기·다른 checkout 실행·새 agent/clone·push/PR/merge/배포·전환은 없다.
- Remaining risk: 한 page 근거는 원 caller의 전체 request 보유 문제를 해결하지 않는다. 현재 공개 API는 유효한 paged 경로를 제공하지만 소비자 유지 정책 변경은 승인 범위 밖이며 이 요구는 Task39의 제품 검증에도 남는다. 작은 입력/전체 순차 시간과 축적 비용, shared-host/OS-cache/allocator 한계, 예전 첫 실패 driver binary 미고정과 중간 control 등록 객체 재구성 공백을 유지한다. 원 scale·실제 writer·WAL/buffer/MVCC/early-locking·동등 buffered-COW의 회수/checkpoint/복구 총비용·stream·제품 통합은 미완료다. WAL 권고는 변하지 않았고 goal은 active다.
- AAR: API의 page 결과 수명과 caller 사전의 payload 수명은 별개다. enqueue의 논리 admission을 복구 RSS 상한이나 전체 저장 행 수 상한으로 치환하면 안 된다. 큰 입력 page의 긍정 결과 옆에 작은 입력/전체 소비/전체 축적의 반대 근거를 보존하며 storage 선택의 증거로 혼용하지 않는다. static 소스·분리 AST 대조·정식 비계측 효과·ON 진단·실제 앱 실행을 구별한다. 보존 검사는 처음과 끝에 같은 status 옵션을 써야 한다.
- Next step: 이 task-boundary commit에서 멈춘다. 다음 continuation은 추가 checkpoint after19만 수행하며 그 뒤 원래 Task20 또는 근거 있는 수리로 이어간다. checkpoint/새 Task20/새 성능 표본/제품 변경을 이 경계에 실행하지 않는다.
- Commit record: subject `docs(docs): 목록 조회 실측과 소비자 축적 경계 기록 추가`; body marker `Goal-mode boundary: goal-1 task 19: Measure pending queries and the real caller boundary`. 이 기록을 포함하는 커밋의 설정된 작성자·marker와 clean 상태를 경계에서 확인한다.

## Additional checkpoint after Task 19

- Status: complete (범위·자료 완전성·재현·판정 감사; 새 구현·효과 표본이나 원 소비자 해결 완료와 구분)
- Work performed: 직전 turn은 progress다. d9a17284의 Task19 marker·c0cb4216 parent·설정된 작성자·정확한5파일 범위와 초기 clean 상태로 확인했다. Task18a/18b/19의 요구, 검증기와 분석/worker 소스, 소유권·실패·자원·보존, build/type/test 근거, rollback과 문서/이슈 상태를 대조했다. 새로운 수리 과업을 요구하는 결함은 이 검토에서 확인하지 못했다. UI는 내부 수집/분석 경로에 해당 없음이다.
- Preservation/source evidence: `build/checkpoint19-preservation.json`은 기존 장부20265항목(16630+Task19의3635), 직전 seal6개, 기준 source11/runtime79·고정 identity100개를 실제 파일/Git 내용에 대조한다. Task19 closure의5개 문서/추적 hash와 Continuity도 같다. main/origin/main/live main6bb8b2e8와 다른 네 worktree HEAD를 보존했다. 소비자4파일 hash·추적 dirty8경로·Task19 종료 때 관측한 미추적7경로 목록은 같다. 미추적 파일 내용 전체의 시간상 보존이나 실제 소비자 실행을 새로 주장하지 않는다.
- Reproduction/coverage evidence: 실제 `build/check-env/bin/python -W error bench/pending_query.py verify --prefix build/task19-pending-formal`은1728/control=false를 확인했고 `analyze --prefix`는 원 JSON과 byte 동일 SHA256 8a2cd02e4b3281373248e5617ee3b1423820ca18be1cc90f21c7422b66682fc3이다(`checkpoint19-cli.json`). 현재 두 검증기의 읽기 전용 대조9개는 누락 hash를 raw JSON 처리0회에서 모두 거절했다. 공통 과거 Task17 conditional48건도 명시 source df37ea61로 정상 검증했다(`checkpoint19-coverage-controls.json`). Task18b의 세 과거 정식 분석 재현·gzip 역검증과227+등록2+legacy3의 별도 시험, Ruff/format/mypy·native Q5 근거는 동일 source/binary/hash 범위만 재사용했다. 새 suite/build/CI나 정식 재수집으로 세지 않는다.
- Independent judgment: `checkpoint19-independent.json`에서 원자료만으로 정식1440/사전144/진단144·기대 결과 digest1728개·주 구간16개·성장12개·순서5/5의96개 비교를 다시 계산했다. 고정 기준의 표본 내 지지는 일치한다. 작은 backlog의 vendor 대비 느린 cell, Q3 전체 순차 wall 중앙값3.0–13.6배와1을 포함하는 구간, Q4 전체 축적, ledger/RSS/장치I/O 차이와 원 caller의 request 사전 보유는 그대로 한계다. 이 결과를 새 WAL/실제 writer 우위나 현재 소비자 효과로 확대하지 않는다. source의 전체 복원→상태 필터→cleanup과 논리 admission/전체 저장행/RSS 차이를 확인했다.
- Document/remote evidence: `checkpoint19-doc-check.json`의 문서3개 check_doc·main 대비 whitespace, local file targets187개, 주16/순차4/작은4행의 수치 대조가 통과했다. NumPy1.26.4/SciPy1.13.1도 등록 환경과 같다. 최신 #257/#265/#239/#158은 모두 OPEN이며 미체크2/4/4/1개다(`checkpoint19-issues.json`). 미발행 로컬 결과를 원격 통합/이슈 완료로 바꾸지 않았다. Task6·완료18a/18b/19와 원래20–39/모든 후속 checkpoint, input/plan/goal-current를 보존한다. 원격·consumer·Vault 쓰기, 새 checkout/agent, push/PR/merge/배포/전환은 없다.
- AAR: 첫 재분석 호출에서 venv 실행 경로에 resolve를 적용해 base interpreter를 직접 실행했고 scipy import가 실패했다. 실패 stdout/stderr를 보존하고 같은 venv 경로의 absolute 호출로 고쳐 정상 검증·byte 재현을 확인했다. binary bytes가 같아도 실행 경로의 pyvenv.cfg 선택은 다르므로 등록한 venv 경로를 유지한다. 이는 검사 orchestration 오류이며 원자료/제품 수정으로 통과시킨 것이 아니다. 자료 완전성·동일 hash·재현·요구 충족의 판단은 각각 별도로 확인한다.
- Remaining risk: 원 caller 전체 메모리 해결, 기본키 규모 실측, 실제 독립 writer·WAL/buffer/MVCC/early locking와 동등 buffered-COW의 복구/checkpoint/회수 총비용, stream·제품 통합은 미완료다. 기존 driver 첫 실패 binary와 중간 control 등록 객체의 재현 공백도 유지한다. 현재 WAL 권고와 COW 비교 후보/현행 제품 보존을 구분하며 goal은 active다.
- Next step: 이 tracking-only checkpoint commit에서 멈춘다. 다음 continuation은 원래 Task20의 #158 입력/수집기 감사·준비만 수행한다. Task21 규모 수집이나 Task22 backend 계약을 이 경계에서 시작하지 않는다. 원래 after21과 모든 후속 경계를 유지한다.
- Commit record: subject `docs(docs): 조회 실측과 자료 보존 검토 기록 갱신`; body marker `Goal-mode boundary: goal-1 checkpoint after task 19: complete`. 이 기록을 담은 tracking-only commit의 작성자·marker와 clean 상태를 경계에서 확인한다. 최종 근거는 `build/checkpoint19-verification.json`에 고정한다.

## Task 20 — Prepare the missing primary-key scale validation

- Status: complete (수집기·고정 계획·작은 대조 검증만 완료; Task21의 정식 규모 수집은 미완료)
- Scope/work: 직전29a54723의 checkpoint19 marker·설정된 작성자·clean 상태를 확인한 progress다. #158 OPEN·사용자 소유·실측1미체크와 같은 이슈 branch/열린 PR 부재를 확인하고, 같은 checkout의 feat/158-repo-primary-key-scale로 bind했다. 기존 입력 모듈/설정은 그대로 두고 새 수집기·계획·회귀와 driver의 내부 @scan 명령을 추가했다. 기존 최적화 OFF 라이브러리에 새 driver만 직접 연결했으며 제품 source·기본값·ABI·원 binary를 바꾸지 않았다. 기존 설계/빌드 문서·plan/Continuity를 갱신했다.
- Input/source audit: build/task20-source-issue-audit.json은 원 #137의120초/문장30초/seed137·세 규모3회, #178의 부분 적재, #179의180초 별도 성공, #158 재개 이유를 보존한다. 확인한 옛 checkout·build/bench·build/sqlite-performance와 보존 tar 목록에서 당시 성능 raw를 찾지 못했다. 전수 부재·삭제·복구·재해시 성공을 주장하지 않는다. 현재 native 기본키 경로/기존 scan 회귀를 소스로 대조했으며 새 제품 native suite 실행으로 세지 않는다. 전체 혼합·복구/YCSB 캠페인은 반복하지 않았다.
- Preregistration: 원 items/groups·NULL/한글/BLOB/9000B 본문·autocommit INSERT·여섯 조회의 예열1/반복20·정순/역순/정순을 유지한다. 요청1천/1만/10만×3의9case, 공유 적재120초·문장30초·worker 관측RSS2GiB/인코딩512MiB를 유지하고 전체1800초/자료8GiB/여유2GiB를 수집 가정으로 고정했다. 정식 등록은 이 계획을 담은 clean 조상 commit·source6·runtime9d70c9b4의C/H/Python78·driver/library/fingerprint/interpreter4identity·SQLite/플랫폼을 고정한다. runtime78은 queue.mk를 제외한 코드 목록이며 이전79개 전체와100identity도 별도로 보존 검사했다. driver 빌드는 이 경계의 C source 내용과 일치하지만 아직 없는 containing commit에서 clean build했다고 쓰지 않는다.
- Verification evidence: `build/check-env/bin/python -W error -m pytest -q -s tests/test_primary_key_scale.py`의 최종24검사가37.22초에 통과하고 stderr가 비었다(build/task20-pytest-closure.log/stderr). 누락 hash6유형·source/runtime/binary/일정/120→180변경·같은 잘못된 point 결과·조회/종료 누락·sync변경·꺼진 scan 반례를 거절한다. 한쪽 응답 timeout과 worker 경고는 DB·진행 중 응답·case/manifest를 보존한다. 가상 적재 clock의3/24행 부분 완료는 검증되지만 요청 규모 완료는 아니다. 이들은 실패 모델이며 실제 OS 응답 손실·전원 복구 시험이 아니다.
- CLI/evidence: build/task20-cli.json은 새 current-source24행 control의 register/collect/verify와 analyze2회 byte 동일 SHA25680b7c3b7bbd963532fc6915793235330fe08447996cf70a82f9c4ae24b3df512를 기록한다. 원자료에서 조회순서·독립 point/missing·재열기·INSERT/기본키scan0·비기본키24행 양성 대조와 nearest-rank를 별도로 계산했다. dirty 정식 등록은 파일 생성 전에 거절됐다. SQLite3.51.0/CPython3.9.6/macOS26.5.2 arm64이며 SQLite 직접 scan 행은 측정하지 않는다. Ruff/format120/mypy·문서2개·상대 파일106개·whitespace를 통과했다. build/task20-verification.json은 새133artifact와 실제 -O2/-Wall/-Wextra/-Werror build를 고정한다.
- Preservation/AAR: build/task20-preservation.json은 기존20277artifact항목·seal6개·원 입력/보존 tar·main/origin-main6bb8b2e8·다른 네 worktree HEAD를 대조했다. 초기/중간 control·timeout/경고 주입 자료도 유지한다. worker stderr 예외가 case 저장을 건너뛰던 경로를 고쳐 반례로 검증했다. 독립 감사의 p50_ns 기대를 실제 원 형식 p50_ms로 고쳤고, Makefile 비교 기준을 오래된 Task16이 아닌 실제 부모29a54723으로 고쳤다. 원자료/제품을 바꿔 감사를 통과시키거나 재수집하지 않았다. 원격/Vault/consumer 쓰기·새 checkout/agent·push/PR/merge/배포/전환은 없다.
- Remaining risk: 정식 세 규모3회는 아직 실행하지 않았다. 작은 대조·3회의 설명 통계·현재 source를 과거 대비 #158 인과 효과나 속도 채택으로 확대하지 않는다. SQLite/COW의 물리 전원 손실 동등성·OS cold·실제 writer·전체 allocator 상한은 입증하지 않는다. 과거 raw 발견 한계와 첫 실패 driver/legacy 등록 재현 공백, caller 전체 보유·WAL/buffer/MVCC/early locking·동등 buffered-COW의 회수/checkpoint/복구 총비용·stream·제품 통합은 남는다. 현재 WAL 권고를 유지하며 goal은 active다.
- Next step: 이 task-boundary commit에서 멈춘다. 다음 continuation은 원래 Task21만 수행하며 이 수집기/계획을 담은 실제 commit으로 새 정식 등록 후 원래 조건을 수집한다. partial/timeout은 요청 규모 완료가 아니며 실패하면 원인을 진단하고 필요한 수리를 추가한다. 이후 원래 checkpoint21과 모든 Task22–39/경계를 보존한다.
- Commit record: subject `feat(repo): 기본키 규모 수집기와 원래 입력 검증 추가`; body marker `Goal-mode boundary: goal-1 task 20: Prepare the missing primary-key scale validation`. 이 기록을 담은 커밋의 설정된 작성자·marker와 clean 상태를 경계에서 확인한다. 최종 추적/자료 seal은 build/task20-closure.json에 고정한다.

## Task 21 — Execute the retained primary-key scale validation

- Status: complete (원래 고정 규모9case의 로컬 정식 수집·검증 완료; 새 backend·제품 통합/발행과 구분)
- Scope: Collect the missing same-condition 1k/10k/100k three repeats, scan counts and p50/p95/p99 against the compatible SQLite input; preserve partial loads/timeouts separately. Issues: #158.
- Work performed: 직전ff3af84d의 Task20 marker·설정된 작성자·8파일 범위·초기 clean 상태로 progress를 확인했다. 기존 #158 branch와 bind를 재사용했고 열린 동일 PR은 없었다. source/계획·입력·binary·제품은 바꾸지 않고 새 build/task21-primary-key-formal 접두사에9case를 수집했다. 기존 설계에 반복별 INSERT/point/missing의 p50/p95/p99와 스캔·한계를 기록하고 빌드 안내/개발 보고·plan/Continuity를 갱신했다.
- Preregistration: registration은 clean plan/source 조상 ff3af84dd28db14db67f7cc95ce6eefdf230a50d·source6·runtime9d70c9b4의 코드78개·실제4identity와 환경을 첫 표본 전에 고정했다. control=false, SHA25612c8dde65077b1786a2b24c2d2c8d7e7b3f596c16db0f634e5f8bdbcda9f93bb다. 원 seed137·세 규모3회·공유 적재120초/문장30초·예열1/여섯 조회20회·정순/역순/정순과 모든 자원 gate를 유지했다. macOS26.5.2 arm64/CPython3.9.6/SQLite3.51.0이며 기존 OFF library를 재사용했다. 새 clean build-at-commit 실행으로 쓰지 않는다.
- Collection evidence: manifest는 예정/시도/완료9·실패0·미실행0,227.684619667초이며 CLI 전체228.6552235초다. 1천/1만/10만 행이 각3회 모두 요청 행 수를 채웠고 부분 적재0이다. 10만 행 적재44.148171/53.063050/68.333559초로 모두120초 이하였다. 엔진별 총333000행과 timed 조회1080쌍을 수행했다. 필수46+lock9의55artifact·68702742B를 보존했으며 manifest SHA256078801478269c8f9b8cddd4be21265a25274fce39dafe33b153f6309913bd6ff다. 모든 worker/CLI stderr는 비었다.
- Verification evidence: 실제 `build/check-env/bin/python -W error bench/primary_key_scale.py verify --prefix build/task21-primary-key-formal`은9/control=false를 확인했다. 같은 analyze 두 번은 byte 동일 SHA2563d9a318d4b18b46d0a9c473e5dbf83fcc890da9e993e863d001cbe9a720d6eea다(build/task21-cli.json). 별도 원자료 계산은 typed 기대 digest1440건·point/missing720건·일정1080쌍·분위수/QPS54계열을 대조했다(task21-independent-audit.json). 모든 INSERT의 heap 스캔0/색인 조회N, point/missing 각각20회 합계 스캔0/색인20/페이지계수60, 비기본키 양성 대조N과 재열기·종료를 확인했다. 이0은 heap 접근·물리 I/O0이 아니며 SQLite 직접 scan 행은 측정하지 않았다.
- Disposition/preservation: requested_scope_complete는 이 규모 측정 범위의 판정이다. task21-disposition.json에 모든 반복과 자원을 남겼고 native/SQLite worker 관측 고수위는63.25/29MiB다. build/task21-preservation.json은 기존20410artifact항목·직전/과거 seal·원 입력/실패/보존 tar·runtime79/identity100·main/origin-main6bb8b2e8·다른 네 worktree HEAD를 대조했다. 문서3개·상대 파일195개·INSERT9/조회18개 표 행과 whitespace를 검사했다. Task20의24회귀/실제 build/Ruff/format/mypy는 입력·binary·로그 hash가 같은 범위로 재사용했고 새 suite/build 실행으로 세지 않는다. task21-verification.json은 새76artifact를 고정한다. 최신 #158 본문/담당·OPEN/1미체크는 그대로이며 로컬 결과를 원격 완료로 올리지 않았다.
- Remaining risk/AAR: 원120초 실패·180초 별도 성공과 현재 실행을 구분한다. 과거 raw 발견 한계·첫 driver/legacy 등록 재현 공백은 그대로다. 3반복·조회별20개 p99=최대값은 설명 통계이며 과거 대비 인과 효과·일반 속도 채택을 검정하지 않는다. 세 번째 native INSERT p99는1.986ms로 반복 차이도 보존했다. 공유 호스트·warm OS cache·RSS 사후 guard와 allocator/앱 전체 cap·물리 I/O를 구별한다. 원 caller 보유·실제 writer·WAL/buffer/MVCC/early locking와 동등 buffered-COW의 회수/checkpoint/복구 총비용·stream·제품 통합은 미완료다. 이 성공은 현재 WAL 권고를 변경하지 않는다. 새 결함/수리 과업은 이 경계의 근거에서 확인하지 못했고 goal은 active다.
- Next step: 이 task-boundary commit에서 멈춘다. 다음 continuation은 원래 checkpoint after21만 수행하고 Task22의 backend/복구 계약은 그 뒤에 진행한다. Task6 복구와 Task20, 원래 after21/Task22–39의 모든 경계를 보존한다. 원격/consumer/Vault 쓰기·새 checkout/agent·push/PR/merge/배포/전환은 없다.
- Commit record: subject `docs(docs): 기본키 세 규모 실측과 검증 결과 추가`; body marker `Goal-mode boundary: goal-1 task 21: Execute the retained primary-key scale validation`. 이 기록을 담은 커밋의 설정된 작성자·marker와 clean 상태를 경계에서 확인한다. 최종 추적/자료 seal은 build/task21-closure.json에 고정한다.

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
