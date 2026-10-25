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

Completed-entry prose is compacted during Task 16 after a full reread. The exact precompaction ledger is preserved in existing Git object `36529f6c57e08a04923df55f4a2c9765f7e85ff9:goal-1/tasks.md` (SHA-256 1813c082a787753bcc2215539f8352b3224430f05764ce6272bd411813915985). Use `git show 36529f6c:goal-1/tasks.md` and the unchanged section title for full work, commands, failures, limitations and historical next steps. This is a digest in the same approved tracking file, not a new context copy or rewritten history. At compaction, Task 6 source coverage/decisions/matrix and every incomplete entry remain verbatim; later execution updates only its own entry. The rejected earlier overall completion remains superseded. Current first incomplete boundary is the separate checkpoint after23k, after completed23i/23j/23k checkpoint/history/physical-redo prerequisites; original23 remains incomplete; general recovery remains required. Allocation metadata, in-memory transitions and opt-in WAL system witnesses remain components; durable system execution and general recovery are incomplete. Control file publication remains a component; durable outcome/system state and general recovery are incomplete. Bounded persisted metadata remains raw lookup only; all later requirements/checkpoints remain incomplete.

Task 23d compacts only completed Task16–23c entries and their completed checkpoints after source review. Their exact full ledger remains in Git object `623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md` (SHA-256 31dd489d6de49399434544cbfa2353500aa28e3cb458536bc2fbfe5a1d14d280). Task6 source coverage/requirement matrix and all incomplete entries remain verbatim; no source history, failure artifact or requirement is removed. This adds no context copy.

Task 23g compacts only the completed 23d/e/checkpoint23e/23f records against Git `f0219294c5fc47a8cf312ea7b728a8dceb0bbd17:goal-1/tasks.md` (SHA-256 c2b59381598f3c179d9d785cda63ec682df8de7149863009fc19486c2eec6223). Task6 and all existing incomplete sections remain byte-identical. This is tracking maintenance within the allocation implementation boundary; no new context file or removed requirement.

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
- Work performed: 조건부 claim/ACK·실패 readback과 empty UNION 경계 수리, 기본OFF 유지.
- Verification evidence: build/task16-verification.json; native10개 suite×OFF/ON·Python62×4·수집기87.
- Remaining risk: 더 적은 SQL과 더 큰 query/batch 비용이 함께 관측됐고 성능 채택은 별도다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `9d70c9b4e5747985a094eaa618f9653627992c14`, `feat(repo): 조건부 큐 갱신과 실패 상태 조회 추가`; `Goal-mode boundary: goal-1 task 16: Implement conditional queue updates`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Additional checkpoint after Task 16

- Status: complete (scope/source/evidence review; no new runtime implementation or formal measurement)
- Work performed: 15a/16p/16의 source·실패·자원·문서/범위 검토.
- Verification evidence: build/checkpoint16-verification.json; 기존8395artifact와74runtime source·4build 대조.
- Remaining risk: 등록 전 실제 Task16 commit sealing 필요였으며 Task17이 수행했다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `df37ea610e6db4226289179ae41006ca7680c414`, `docs(docs): 조건부 큐 갱신 검토와 실측 진입 조건 기록 갱신`; `Goal-mode boundary: goal-1 checkpoint after task 16: complete`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 17 — Measure conditional SQL and optional lifecycle

- Status: complete (fixed-plan formal collection, verified regeneration and hold disposition; no runtime/default adoption or backend implementation)
- Work performed: 사전 등록 lifecycle72/E3조건부48 전체 수집과 두 가설 보류 판정.
- Verification evidence: build/task17-verification.json; 두 분석 byte 재생성·2352gzip DB 역검증.
- Remaining risk: tail/CPU/주 효과 기준 실패를 유지하며 RSS 관측은 Python C API cap이 아니다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `ba4c47cea9d16e28a05542a388a7018b0bd98e38`, `docs(docs): 조건부 SQL과 선택형 수명 실측 보류 결과 기록 추가`; `Goal-mode boundary: goal-1 task 17: Measure conditional SQL and optional lifecycle`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Additional checkpoint after Task 17

- Status: complete (scope, provenance and measured-disposition audit; no new runtime implementation or formal samples)
- Work performed: 고정 표본·pairing·판정과 과거 source 재분석 검토.
- Verification evidence: build/checkpoint17-verification.json; 13366artifact·5cell·byte 동일 재분석.
- Remaining risk: 단일 항목 실측은 batch·writer·pending/consumer 전체 효과가 아니다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `18167df8fcf9ebcb3ba1ade5e1ca74672bb75a33`, `docs(docs): 수명·조건부 실측 검토와 조회 측정 경계 기록 갱신`; `Goal-mode boundary: goal-1 checkpoint after task 17: complete`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 18 — Build and preregister pending-query measurements

- Status: complete (로컬 수집기·사전 판정·정상/실패 대조만 완료; 정식 효과·소비자 교체·새 backend 미완료)
- Work performed: Q1–Q5 collector·고정 계획·실패/수명 control 구현.
- Verification evidence: build/task18-verification.json; control36·Python29/공통87·native malloc4/reserve366.
- Remaining risk: source 한도와 Python unlimited pool/RSS gate를 구분하고 정식 수집은19에 남겼다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `b9cfef4a1908bbafe3d289f8df9002b44bd75d33`, `feat(repo): 목록 조회 수집기와 메모리 사전 기준 추가`; `Goal-mode boundary: goal-1 task 18: Build and preregister pending-query measurements`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Checkpoint after Task 18

- Status: complete (원래 범위·구현/실측 근거 검토 완료; CP18-1/2 검증기 수리는 미완료로 추가)
- Work performed: 자료 hash 집합 완전성 결함 CP18-1/2를 실제 반례로 확인.
- Verification evidence: build/checkpoint18-verification.json; pending6/common3 읽기 전용 반례.
- Remaining risk: 당시 두 수리는18a/18b에서 해결했으며 원 첫 driver binary 재현 공백은 유지한다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `f31457fdaf73f03fed379323bfdcfb955d6ddafb`, `docs(docs): 조회 검토와 자료 해시 검증 수리 과업 추가`; `Goal-mode boundary: goal-1 checkpoint after task 18: complete`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 18a — Require complete pending-query artifact coverage

- Status: complete (로컬 pending 자료 coverage 수리와 회귀만 완료; 공통 수리·정식 효과·제품 전환 미완료)
- Work performed: pending 필수 자료·정규 경로·hash를 분석 전에 검증.
- Verification evidence: build/task18a-verification.json; 수정 전6실패/수정 후65검사·control36.
- Remaining risk: checksum은 전체 장부 교체의 인증이 아니며 정식 등록은18b 뒤에 수행했다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `e028684f07ea4da226b0cc74ee73670eccfbc15d`, `fix(repo): 목록 조회 자료 해시 누락 검증 수정`; `Goal-mode boundary: goal-1 task 18a: Require complete pending-query artifact coverage`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 18b — Require complete common-cost artifact coverage

- Status: complete (로컬 공통 자료 검증 수리·과거 재처리·연동 대조 완료; 새 정식 효과·제품 전환 미완료)
- Work performed: 공통 자료 coverage/gzip 역검증과 명시 과거 source 재현 수리.
- Verification evidence: build/task18b-verification.json; 227+등록2+legacy3 별도 실행·과거3분석 재현.
- Remaining risk: 중간 control 등록 객체·첫 실패 driver binary의 재현 한계를 보존한다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `c0cb4216ed75d608b9595d3dd2e01da662483e15`, `fix(repo): 공통 비용 자료 해시와 보존 DB 검증 수정`; `Goal-mode boundary: goal-1 task 18b: Require complete common-cost artifact coverage`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 19 — Measure pending queries and the real caller boundary

- Status: complete (고정 계획의 전체 격자 수집과 읽기 전용 감사 완료; actual caller replacement, whole-app solution and new backend remain incomplete)
- Work performed: 1728case 정식 pending 수집·독립 판정과 원 consumer 읽기 전용 감사.
- Verification evidence: build/task19-verification.json; 정식1440/사전144/진단144·주16/성장12·분석 byte 동일.
- Remaining risk: 고정 격자 page-memory 지지와 작은 입력/전체 순차/축적의 반대 결과를 보존하며 원 caller 사전 보유는 미해결이다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `d9a172841589f794d3c349a619a9affe3046740c`, `docs(docs): 목록 조회 실측과 소비자 축적 경계 기록 추가`; `Goal-mode boundary: goal-1 task 19: Measure pending queries and the real caller boundary`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Additional checkpoint after Task 19

- Status: complete (범위·자료 완전성·재현·판정 감사; 새 구현·효과 표본이나 원 소비자 해결 완료와 구분)
- Work performed: pending raw/판정·누락 거부·재현·consumer 경계 검토.
- Verification evidence: build/checkpoint19-verification.json; 20265기존artifact·독립digest1728·주16/성장12.
- Remaining risk: venv 실행 경로 오류를 바로잡았고 실제 consumer 효과나 새 backend 우위로 확대하지 않았다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `29a54723d67e502c6dbba1c73b5a07718a250d81`, `docs(docs): 조회 실측과 자료 보존 검토 기록 갱신`; `Goal-mode boundary: goal-1 checkpoint after task 19: complete`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 20 — Prepare the missing primary-key scale validation

- Status: complete (수집기·고정 계획·작은 대조 검증만 완료; Task21의 정식 규모 수집은 미완료)
- Work performed: 원 #158 seed137/120초 조건의 수집기·계획·작은 대조 구현.
- Verification evidence: build/task20-verification.json; 회귀24·24행 control·독립 point/scan/재열기.
- Remaining risk: 옛 raw는 확인한 보관 위치에서 찾지 못했으며 전수 부재·복구 성공은 주장하지 않았다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `ff3af84dd28db14db67f7cc95ce6eefdf230a50d`, `feat(repo): 기본키 규모 수집기와 원래 입력 검증 추가`; `Goal-mode boundary: goal-1 task 20: Prepare the missing primary-key scale validation`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 21 — Execute the retained primary-key scale validation

- Status: complete (원래 고정 규모9case의 로컬 정식 수집·검증 완료; 새 backend·제품 통합/발행과 구분)
- Work performed: 원 1k/10k/100k 각3회·120초 규모9case의 정식 수집 완료.
- Verification evidence: build/task21-verification.json; typed1440/point720/분위수54·PK scan0/비PK 양성 대조.
- Remaining risk: 현재 단일 writer COW의 설명 통계이며 WAL·실제 writer 우위나 과거 대비 인과 효과가 아니다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `e616166c488612a519be320ff3cc9edb6dd80c9a`, `docs(docs): 기본키 세 규모 실측과 검증 결과 추가`; `Goal-mode boundary: goal-1 task 21: Execute the retained primary-key scale validation`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Checkpoint after Task 21

- Status: complete (로컬 범위·source·자료·재현·판정 검토 완료; 새 효과/제품 구현·원격 상태 확인과 구분)
- Work performed: pending/scale 자료·재분석·누락 거부와 범위 검토.
- Verification evidence: build/checkpoint21-verification.json; 20486artifact·분석 두 JSON byte 동일.
- Remaining risk: 당시 GitHub/DNS 실패는 미검증으로 유지했고 Task22에서 새 연결 성공을 별도로 확인했다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `b947ce60a696c4b67463df34cfa548dd2e5b8f40`, `docs(docs): 기본키 실측 검토와 WAL 계약 진입 기록 갱신`; `Goal-mode boundary: goal-1 checkpoint after task 21: complete`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 22 — Specify WAL and version recovery contracts

- Status: complete (격리된 후보의 계약·대안·정적/제한 모형 검증 완료; 실제 엔진 구현·중단 복구·성능 미완료)
- Work performed: 불변 version·논리 abort·NO-STEAL·G2영속 결과·checkpoint/소유권 후보 계약.
- Verification evidence: build/task22-verification.json; 제한24순서/136snapshot/48자기읽기·redo-only18/COW4반례.
- Remaining risk: 계약/제한 모형과 실제 codec·I/O·복구·writer·성능을 구분한다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `4647f1f537bb2ca8281b65596a16dc6e1a9740d6`, `docs(docs): WAL 버전 복구와 영속 결과 조회 계약 추가`; `Goal-mode boundary: goal-1 task 22: Specify WAL and version recovery contracts`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 23a — Implement segmented WAL framing and restart scan

- Status: complete (격리된 WAL transport 구현·실파일/byte 검증만 완료; 원래23의 거래/영속 결과 복구·제품 미완료)
- Work performed: 격리된 segmented WAL codec/append/force/scan/명시 tail 수리 구현.
- Verification evidence: build/task23a-verification.json; Make12batch/21893·360절단·독립 byte/CRC·clang 경고0.
- Remaining risk: 최초113fixture/native90초 실패와227artifact를 보존하며 transport는 거래 복구·APPLIED가 아니다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `dbdf21d8303f7ce3c29c1bc7a4d975823cc0a673`, `feat(repo): 분할 WAL 파일과 재시작 스캔 추가`; `Goal-mode boundary: goal-1 task 23a: Implement segmented WAL framing and restart scan`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 23b — Validate transaction and physical-group WAL witnesses

- Status: complete (격리된 WAL의 의미 분석·실파일/독립 byte 검증 완료; 영속 본문·redo·복구 활성화는 미완료)
- Work performed: 읽기 전용 거래 chain/physical group/terminal witness 분석 구현.
- Verification evidence: build/task23b-verification.json; Make10237·18prefix/34외부CRC·독립10720B17frame.
- Remaining risk: 참조 digest 대조는 본문 존재/복원·ALLOC/FREE·epoch/checkpoint·복구 활성화를 증명하지 않는다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `4bb7fc4733db4432185679a34608c5b92630d2ef`, `feat(repo): WAL 거래와 물리 묶음 검증 추가`; `Goal-mode boundary: goal-1 task 23b: Validate transaction and physical-group WAL witnesses`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Additional checkpoint after Task 23b

- Status: complete (22/23a/23b 검토와 실제 결함 재현 완료; CP23b-1 수리를 다음 과업으로 추가)
- Work performed: cwd 변경 뒤 다른 inode를 fsync하는 CP23b-1을 재현하고23c 추가.
- Verification evidence: build/checkpoint23b-verification.json; 실제 A동기화0/B1·cwd유지/절대 경로 양성 대조.
- Remaining risk: 결함은23c에서 수리했고 영속 결과·실제writer·총비용·제품/stream 의무는 계속 남긴다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `a6521098cca72bfe093838431f6442337e3395b8`, `docs(docs): WAL 후보 검토와 파일 소유권 수리 과업 추가`; `Goal-mode boundary: goal-1 checkpoint after task 23b: complete`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 23c — Bind WAL operations to the owned files

- Status: complete (후보 파일 소유권·barrier 결함 수리와 로컬 검증 완료; 거래 복구·제품 통합 미완료)
- Work performed: 디렉터리 fd와 segment inode/열린fd를 고정하고 누락·교체를 차단.
- Verification evidence: build/task23c-verification.json; 파일1789/cwd8·누락교체12, transport21893/360절단·witness10237.
- Remaining risk: 외부 변경은 미지원이며 fd/metadata 비용은 총비교에 포함한다. 일반 복구·영속 ID·실제writer는 미완료다.
- AAR: detailed in the sealed full record below.
- Next step: historical boundary completed; resume the first incomplete entry in this file.
- Commit record: `623e9d5c4da00a0adb842d3e8d4213d839ad6976`, `fix(repo): WAL 파일 소유권과 내구 동기화 대상 수정`; `Goal-mode boundary: goal-1 task 23c: Bind WAL operations to the owned files`.
- Full record: `git show 623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md`, same section title.

## Task 23d — Encode version pages and verify stored result bodies

- Status: complete (일반 page codec·소유 본문 검증·합성 WAL/page roundtrip 완료; ID 조회·일반 복구 미완료)
- Work/evidence: build/task23d-verification.json; codec34096/WAL866·독립 bytes/CRC/hashlib135·clang 검사 통과. 일반 page/소유 본문만 검증했다.
- Remaining risk: 원래23의 일반 복구·system/WAL/data/terminal 정합과24–39는 미완료다. 기존 backend·raw/실패·main/YCSB·Vault/consumer는 보존한다.
- AAR/next step: 전체 계약·근거·실패·범위/다음 행동은 아래 고정 Git 원문에 보존한다. 현재 첫 미완료 경계만 진행한다.
- Commit record: subject `feat(repo): 후보 page 형식과 결과 본문 검증 추가`; body marker `Goal-mode boundary: goal-1 task 23d: Encode version pages and verify stored result bodies`. 이 기록을 담은 커밋의 설정된 작성자·marker와 clean 상태를 경계에서 확인한다.
- Full record: `git show f0219294c5fc47a8cf312ea7b728a8dceb0bbd17:goal-1/tasks.md`, same section title.

## Task 23e — Store and look up bounded request outcome metadata

- Status: complete (bounded metadata codec·raw ID 조회와 합성 WAL/data 검증; 일반 복구·APPLIED 미완료)
- Work/evidence: build/task23e-verification.json; metadata50978/page34096/WAL892·실제133page/4group와4096record byte 대조. raw lookup은 APPLIED가 아니다.
- Remaining risk: 원래23의 일반 복구·system/WAL/data/terminal 정합과24–39는 미완료다. 기존 backend·raw/실패·main/YCSB·Vault/consumer는 보존한다.
- AAR/next step: 전체 계약·근거·실패·범위/다음 행동은 아래 고정 Git 원문에 보존한다. 현재 첫 미완료 경계만 진행한다.
- Commit record: subject `feat(repo): 요청 결과 metadata와 ID 조회 추가`; body marker `Goal-mode boundary: goal-1 task 23e: Store and look up bounded request outcome metadata`. 이 기록을 담은 커밋의 설정된 작성자·marker와 clean 상태를 경계에서 확인한다.
- Full record: `git show f0219294c5fc47a8cf312ea7b728a8dceb0bbd17:goal-1/tasks.md`, same section title.

## Additional checkpoint after Task 23e

- Status: complete (23c–23e의 코드·자료·계층 경계 검토; 원래23과 전체 목표는 미완료)
- Work/evidence: build/checkpoint23e-verification.json; 읽기 전용7조건43검사·27312artifact와9059tracked 보존. terminal 불일치여도 raw MATCH/body OK가 가능함을 확인했다.
- Remaining risk: 원래23의 일반 복구·system/WAL/data/terminal 정합과24–39는 미완료다. 기존 backend·raw/실패·main/YCSB·Vault/consumer는 보존한다.
- AAR/next step: 전체 계약·근거·실패·범위/다음 행동은 아래 고정 Git 원문에 보존한다. 현재 첫 미완료 경계만 진행한다.
- Commit record: subject `docs(docs): 후보 저장 계층 검토와 복구 잔여 조건 갱신`; body marker `Goal-mode boundary: goal-1 checkpoint after task 23e: complete`. 이 기록을 담은 tracking-only 커밋의 작성자·marker·단일파일과 clean 상태를 경계에서 확인한다.
- Full record: `git show f0219294c5fc47a8cf312ea7b728a8dceb0bbd17:goal-1/tasks.md`, same section title.

## Task 23f — Implement recovery control slots and durable anchor publication

- Status: complete (control codec·소유 파일·합성 의존성 anchor/내구 게시 검증; 원래23의 일반 복구는 미완료)
- Work/evidence: build/task23f-verification.json; control16978·독립16384B/16torn×5·실제종료/ACK후SIGKILL·정적검사. 옵션변경 이전source 실패도 보존했다.
- Remaining risk: 원래23의 일반 복구·system/WAL/data/terminal 정합과24–39는 미완료다. 기존 backend·raw/실패·main/YCSB·Vault/consumer는 보존한다.
- AAR/next step: 전체 계약·근거·실패·범위/다음 행동은 아래 고정 Git 원문에 보존한다. 현재 첫 미완료 경계만 진행한다.
- Commit record: subject `feat(repo): 복구 control 슬롯과 내구 게시 추가`; body marker `Goal-mode boundary: goal-1 task 23f: Implement recovery control slots and durable anchor publication`. 이 기록을 담은 commit의 설정된 작성자·marker·10파일 범위와 clean 상태를 경계에서 확인한다.
- Full record: `git show f0219294c5fc47a8cf312ea7b728a8dceb0bbd17:goal-1/tasks.md`, same section title.

## Task 23g — Encode allocation snapshots and validate generation transitions

- Status: complete (type5 snapshot·소유 표·메모리 세대 전이 검증; 내구 system/일반 복구는 미완료)
- Work/evidence: type5 lifetime snapshot/atomic RAM transitions: native236823, independent8files/16archives; no durable issuance/recovery.
- Remaining risk/next step: historical boundary completed; resume the current first incomplete item. Full scope, failures and limitations remain in the fixed ledger below.
- Commit record: subject `feat(repo): 할당 snapshot과 세대 전이 검증 추가`; body marker `Goal-mode boundary: goal-1 task 23g: Encode allocation snapshots and validate generation transitions`. 이 기록을 담은 commit의 사용자 작성자·marker·10파일 범위와 clean 상태를 경계에서 확인한다.
- Full record: `git show 2f44bb89614dd097f6d25d547034cf1ee2e3297a:goal-1/tasks.md`, same section title; whole-file SHA-256 2c38f96576465dc904057bba75cf41679bc512ea52c9d928910d15ef498a3a20.

## Task 23h — Validate WAL system groups and recovery state transitions

- Status: complete (opt-in system/할당·예약·epoch witness와 metadata 대조; 내구 발급·일반 복구는 미완료)
- Work/evidence: opt-in system witnesses: native22236, legacy10237/result892, independent31frames/37archives; exact old format object restored, no general recovery.
- Remaining risk/next step: historical boundary completed; resume the current first incomplete item. Full scope, failures and limitations remain in the fixed ledger below.
- Commit record: subject `feat(repo): WAL system 묶음과 상태 전이 검증 추가`; body marker `Goal-mode boundary: goal-1 task 23h: Validate WAL system groups and recovery state transitions`. 이 기록을 담은 commit의 사용자 작성자·marker·12파일 범위와 clean 상태를 경계에서 확인한다.
- Full record: `git show 2f44bb89614dd097f6d25d547034cf1ee2e3297a:goal-1/tasks.md`, same section title; whole-file SHA-256 2c38f96576465dc904057bba75cf41679bc512ea52c9d928910d15ef498a3a20.

## Additional checkpoint after Task 23h

- Status: complete (23f/g/h component/source/evidence review; original23 and the full goal remain incomplete)
- Work/evidence: tracking-only review: exact4096success/4097LIMIT, native19781 and independentbytes;28787prior entries/22seals preserved, original23 remains incomplete.
- Remaining risk/next step: historical boundary completed; resume the current first incomplete item. Full scope, failures and limitations remain in the fixed ledger below.
- Commit record: subject `docs(docs): 후보 복구 기반 검토와 다음 복구 조건 갱신`; body marker `Goal-mode boundary: goal-1 checkpoint after task 23h: complete`. 이 기록을 담은 tracking-only commit의 사용자 작성자·marker·tasks.md 단일파일 범위와 clean 상태를 경계에서 확인한다.
- Full record: `git show 2f44bb89614dd097f6d25d547034cf1ee2e3297a:goal-1/tasks.md`, same section title; whole-file SHA-256 2c38f96576465dc904057bba75cf41679bc512ea52c9d928910d15ef498a3a20.

## Task 23i — Read anchored checkpoint state and verify complete chunks

- Status: complete (실제 checkpoint 형식·control anchor 부분 검증·소유 snapshot; 일반 복구 미완료)
- Work performed: clean2f44bb89의 checkpoint marker·사용자 작성자·단일파일/source와 전체 goal을 확인한 progress에서 시작했다. records12–14가 실제로 미지원이고 control verifier가 합성이므로 원래23을 보존한 채 선행23i로 분리했다. SLCP/version1의 BEGIN160B·CHUNK32B+dirty24B/transaction56B·END64B와 SHA256 chain, control값/record 위치·owner/previousLSN 대조와 한 소유 snapshot을 구현했다. 일반 WAL은 중간 삽입을 허용하며 이 reader가 거래 의미를 대신 판정하지 않는다. 기존 witness는 checkpoint를 여전히 UNSUPPORTED로 둔다.
- Native evidence: `build/task23i-make-command-terminal.json`의 실제 Make와 모든 재사용 object/fingerprint pin, -Wall/-Wextra/-Werror·fatalUBSan·native90초/runner120초로 최종2731검사·exit0·빈stderr를 통과했다(`build/fault/task23i-checkpoint-terminal.json`). 최초2297·보강2621과 당시 source/binary는 별도 archive/report로 보존했다. 새 API의 component 검사이며 기존 제품의 회귀 실패로 표시하지 않는다. 신규 component의 terminal 반례는 수정 전후로 분리한다. 46개 잘못된 의미 입력·모든 작은 record 경계/END32절단, 전역 정렬/중복CSN·비순차commitCSN 양성 대조·65active거절과 별도 `build/task23i-active64.json`의 정확64성공,8192dirty/4096transaction·96chunk와 두 segment, 정확 길이/LSN/digest·입력 cap·OOM/해제·allocator의 anchor/memory 변경·END 뒤 IO의 NULL/부분 결과 차단을 검사했다.
- Failure/repair evidence: `build/fault/task23i-checkpoint-terminal-before.json`은 같은 보강 test source에서8B COMMIT을 잘못 OK로 반환한 mode46·exit1을 보존한다. `terminal-before` source/binary/명령도 유지한다. first/last 최소112B·terminal 첫 record 금지·ABORT 정확128B·COMMIT248B 이상/16B 간격을 추가하고 첫 양성 snapshot의 산술도 바로잡았다. 최종2731은 같은 source/test object에서 format module만 수리한 성공이며 actual terminal 존재/전체 이력 검증의 대용은 아니다.
- Control/independent evidence: 실제 checkpoint reader를 연결한 두 control slot에서 최신 digest 불량만 이전 slot으로 fallback하고 UNSUPPORTED·LIMIT·NOMEM은 한 번의 verifier 호출로 차단했다. 이것은 완전한 DB verifier가 아닌 checkpoint 부분 verifier다. `build/task23i-independent-terminal.py/.json`은 C helper 없이 struct·CRC32C·hashlib로 현재6fixture/11file과 이전9fixture·반례를 합쳐 총15fixture/28file의 전체 bytes·chain/위치·두slot과18archive를 검증했다. 초기 두 표본의 불가능한 terminal 길이는 역사적 gap으로 표시하며 현재 정상 입력으로 승격하지 않는다. 한 할당426320B와 해제 뒤0은 transport/stack·allocator/RSS와 별도다.
- Static/scope evidence: `build/task23i-static-terminal.json`의 두 module clang --analyze는 exit0·경고0이며 현재 source와 archive가 같다. 기존3문서 check_doc·whitespace·새 상대 링크와 기본 STORAGE/DATABASE object 목록의 불변을 확인했다(`build/task23i-closure-terminal.json`). 새 코드/target은 기본 library·SQL·ACK·기존 ABI/형식3/SNAPSHOT/default에 연결하지 않는다. 빌드는2f44bb89 위 미커밋 source이며 clean 부모 빌드가 아니다. #266 fresh read는 OPEN/5미체크이고 원격 쓰기/발행·PR·merge·배포·전환은 없다.
- Preservation/tracking: `build/task23i-preservation-terminal.json`이 이전28809장부항목/28774실파일·seal23·보호9065추적파일과 main/origin6bb8b2e8·다른4worktree, 원source/실패/YCSB/이전binary를 대조했다. baseline은 완료를 기다린 뒤 수정했고 continuity의 기존 prefix에 한 줄만 추가한다. 완료23g/h/checkpoint23h와 완료23a–h 계획만 고정2f44bb89 Git 원문/SHA에 연결해 압축했다. Task6 전체와 기존 미완료25섹션은 byte 그대로다(`task23i-tracking-compaction.json`). 새 자료/source의 최종 해시는 `build/task23i-final-verification.json`에 둔다. Vault/consumer·다른 checkout/agent는 변경하지 않았다.
- Remaining risk: 선언된 snapshot과 control값의 일치는 complete data/terminal 이력·root loading/할당/도달성/pin, 실제 예약 force/발급·page redo·결과 corroboration·배타 복구 활성화가 아니다. live fuzzy writeback/force·로그 회수는24에 남고 원래23의 일반 조정자와24–39의 MVCC/조기 잠금·실제 독립 writer·SQL/ACK/stream 제품·전체 앱 pending 경계·동등 COW 병합/재작성/회수와 WAL checkpoint/GC/복구 총비용은 미완료다. WAL 권고와 모든 원 요구를 유지하며 component 검증을 실측 우위·제품 채택으로 승격하지 않는다. Goal remains active.
- AAR/next step: count/CRC만으로 checkpoint seed나 full DB verifier를 구현했다고 쓰지 않는다. active 거래의 재생 시작과 frozen하지 않은 terminal 상태를 함께 보존해야 한다. checkpoint 중간 일반 record를 거절하는 안은 fuzzy writeback 동안 writer를 붙잡을 수 있어 채택하지 않았다. 원 payload 전체를 복제하는 digest buffer 대신 기존 one-shot hash의 유한 chain을 골랐다. 초기 component의 terminal 길이 누락을 같은 시험의 수정 전 실패/후 성공으로 보존했고 한도는 바꾸지 않았다. 원자료·pin 규칙을 유지한다. 이 task-boundary 뒤 멈추고 다음 continuation은 원래Task23만 수행한다. after23/24–39와 모든 checkpoint를 보존한다.
- Commit record: subject `feat(repo): checkpoint 상태 형식과 anchor 검증 추가`; body marker `Goal-mode boundary: goal-1 task 23i: Read anchored checkpoint state and verify complete chunks`. 이 기록을 담은 commit의 사용자 작성자·marker·11파일 범위와 clean 상태를 경계에서 확인한다.

## Task 23j — Corroborate checkpoint state with retained WAL history

- Status: complete (checkpoint/보존 WAL의 읽기 전용 의미 대조; 일반 복구·제품 활성화 미완료)
- Work performed: clean438f447e의 사용자 작성자·23i marker·11파일/source/evidence를 확인한 progress에서 시작했다. 원래23을 보존하고 필요한 선행23j만 분리했다. 명시적 analyze_checkpoint가 system 모드에서 checkpoint를 소유한 뒤 전체 보존 WAL을 재스캔한다. BEGIN cut에서 선언 거래의 ID/first/last/terminal-end/CSN/첫 owner/state와 실제 이력을 대조하고 phantom·누락 active·관측 highwater/epoch/floor·dirty PageRef/완성 pre-BEGIN group 위치 불일치를 거절한다. 중간 거래/system 갱신과 cut 사본을 구분하며 선택한 checkpoint bytes/chain을 두 번째 scan에서도 대조한다. 미사용 transaction0 checkpoint는 상태 변경 없는 metadata로 건너뛰고 그 본문 검증은 주장하지 않는다. legacy analyze의 구조체/할당·checkpoint 거절은 유지한다.
- Native evidence: `build/task23j-final-command.json`의 실제 Make·전 dependency/fingerprint pin·새 private witness object로 -Wall/-Wextra/-Werror·fatalUBSan·native90초/runner120초에서5368검사·exit0/빈stderr를 통과했다(`build/fault/task23j-checkpoint-final.json`). 32개 fixture는 모두 raw checkpoint reader에서 OK지만 위조/누락은 새 진입점이 CORRUPT로 거절한다. 모든 작은 complete-record prefix, 첫 owner7/복구 ABORT owner8, 두 scan 사이 CRC 재서명 BEGIN/CHUNK/END 변경과 END 뒤 EIO, 세 OOM·allocator의 options/anchor/memory 변경과 결과 NULL/해제를 검사한다. 별도 `task23j-capacity-command.json`/용량 source/binary archive의58864검사는 실제8192image·4096거래·96chunk·17segment, 정확64active 성공·65active/4097거래 LIMIT을 확인한다. root는 capacity용 선언이며 full DB가 아니다.
- Regression/failure evidence: `build/task23j-reviewed-regression-commands.json`은 현재 header에 맞는 보존23h 시험 object와 새 implementation을 연결해 기존 witness10237/system22236/result892를 새 private binary에서 통과했다. 최초 새 시험은 END exclusive 위치와 같은 incomplete-group 시작을 잘못 >로 비교해 실패했고 >=로 바로잡았다. 최초 회귀 연결은 pre23h options object를 잘못 골라 fatalUBSan bool-load로 실패했다. 옛 source/object/report를 보존하고 검증된23h 호환 object로 바로잡았으며 제품 코드 회귀 수정으로 꾸미지 않는다. `first-*`, `reviewed-*`, `capacity-*`, `final-*`와 실패 report는 그대로다. 새 API는 이전 제품에 없던 component이며 기존 제품의 실패→성공 주장으로 확대하지 않는다.
- Independent/resource evidence: `build/task23j-independent.py/.json`이 C helper 없이 struct/CRC32C/hashlib로32일반+4용량 fixture의52파일, selected snapshot chain·BEGIN cut/최종 거래 차이·owner·dirty group 참조와14입력 archive를 대조했다. 정상 cut의 Bactive가 chunk 사이 commit하고 D가 새 active가 되며 system HWM200/epoch3으로 바뀌어도 frozen cut은 유지한다. 세 소유 할당의 최고2,108,056B·반환 system witness1,681,736B·close 뒤0을 검사했다. stack/transport/allocator overhead/RSS는 별도다. `build/task23j-static.json`의 변경 두 module clang --analyze는 경고0/exit0다.
- Scope/preservation evidence: `build/task23j-closure.json`의3문서 check_doc·whitespace/새 링크·최종 source/입력 archive 대조와 기본 STORAGE/DATABASE 목록 불변을 확인했다. `build/task23j-preservation.json`은29246기존 artifact 항목/24seal·보호9068추적파일, main/origin6bb8b2e8와 다른4worktree·continuity 원prefix를 대조했다. Task6와 기존25미완료 섹션은 byte 동일하다. continuity에는 검증 뒤 한 줄만 추가하며 새 source/자료 hash는 `build/task23j-verification.json`에 둔다. 빌드는438f447e 위 미커밋 source이고 clean 부모 빌드가 아니다. #266 fresh read는 OPEN/5미체크다. 기존 DB/형식3/ABI/SNAPSHOT/default·YCSB/원자료/실패/기존binary·Vault/consumer를 보존하고 원격 쓰기·발행·PR/merge/배포/전환·다른 agent/checkout은 없다.
- Remaining risk: 처음부터 보존된 이력을 요구하며 회수 후 checkpoint seed는 미지원이다. dirty 위치의 group membership은 full allocation/liveness·root/data·미확정 CSN terminal 참조 완전성·pin·WAL-before-data/redo·내구 발급·결과 복원/APPLIED·배타 복구 활성화 증명이 아니다. terminal 생략은 이 단계에서 허용하고 full reference 검증은 조정자에 남긴다. 원래23/after23/24–39, 실제writer·제품/stream·원caller 전체 메모리 경계·동등 COW 병합/재작성/회수와 WAL checkpoint/GC/복구 총비용은 미완료다. WAL 권고를 유지하며 component 성공을 실측 우위로 승격하지 않는다. Goal remains active.
- AAR/next step: 수치상 유효한 snapshot을 실제 복구 seed로 믿지 않고 cut과 retained history를 먼저 대조한다. checkpoint를 읽는 두 pass 사이도 실패 경계이며 첫 pass 성공만으로 결과를 노출하지 않는다. 예전 object는 해시뿐 아니라 빌드 당시 header/옵션 ABI도 확인해야 한다. 이 task-boundary 뒤 멈추고 다음 continuation은 원래Task23만 수행한다. 모든 후속 checkpoint/요구를 보존한다.
- Commit record: subject `feat(repo): checkpoint와 WAL 이력 대조 추가`; body marker `Goal-mode boundary: goal-1 task 23j: Corroborate checkpoint state with retained WAL history`. 이 기록을 담은 commit의 사용자 작성자·marker·11파일 범위와 clean 상태를 경계에서 확인한다.

## Task 23k — Implement bounded physical WAL redo

- Status: complete (격리 target의 image/delta 복원·재실행 계층; 일반 복구 조정자·제품 활성화 미완료)
- Work performed: clean0d3c83b6의 사용자 작성자·23j marker·11파일/source/evidence를 검증한 progress에서 시작했다. 기존 replay_image가 fixture 복사이므로 원래23을 보존하고 선행23k만 분리했다. candidate_redo_execute는 system/checkpoint witness가 실제 읽은 record를 hash에 묶고 전체 physical preflight→WAL force→최대64page staging/쓰기→전체 apply chain 대조→target sync 순서로 실행한다. full image·body delta, previousLSN/generation·기존 pageLSN의 실제 group membership, typed page/CRC를 검사한다. missing/torn 복원과 이력으로 확인한 나중 page skip을 수행한다. ABORT/미완료 사용자 거래의 완성 구조 group을 보존하고 미완성 group은 쓰지 않는다. group의 쓰기 대상 전체를 먼저 형식 검증하지만 I/O 중단의 일부 page 쓰기는 재실행으로 복원하며 실패 out은0이다. callback target의 UUID/생성 내구성/배타 소유권은 조정자 책임이다.
- Native evidence: `build/task23k-final-command.json`의 실제 Make와 모든 재사용 dependency/fingerprint pin·새 private witness/helper로 -Wall/-Wextra/-Werror·fatalUBSan·native90초/runner120초에서6925검사/exit0·빈stderr를 통과했다(`build/fault/task23k-redo-final.json`). 14위치 torn·CRC 재서명 target UUID/형식/ID/generation/비소속pageLSN·잘못된 previous/세대·typed delta·base 없는 delta, 모든 작은 record prefix·불완전 tail과 실패 out을 검사했다. read·7개 부분 write 위치·sync·WAL force 실패 뒤 보존 target에서 재실행하고 source 의미/preflight/apply 변경도 차단한다. 쓰기 중3곳·data fsync 전후2곳에서 실제 child _exit 후 복원했다(각 child10초). 이는 SIGKILL·장치 power-cut이나 product ACK/UNKNOWN 해결 시험은 아니다. 복원한 catalog/allocation은 기존 owning reader로 읽었다. 최대 실행 입력은520page/9group·두 segment이며65536개 전체 채움은 검증하지 않았다.
- Counterexample/repair evidence: 최초 별도 fingerprint/witness scan 후보는 COMMIT count0을 의미 검증 pass에서만2로 바꿔 보이면 잘못된 원본 WAL을 statusOK·7write·1sync로 처리했다. `build/fault/task23k-redo-observer-before.json`과 source/binary/object를 보존했다. witness 내부 observer가 checkpoint를 포함해 의미 검증에 사용한 바로 그 record stream을 hash하도록 연결했다. 같은 시험 object를 Make old-file pin/실제 byte 대조로 재사용한 after 실행은 CORRUPT·0write·0sync다(`observer-after-command.json`, `task23k-closure.json`). 최종은 실제 의미 pass와 preflight 각각을 고쳐 보이는 두 경우도 검사한다. 초기새 API의 결함이며 배포 backend 회귀로 바꾸지 않는다. 초기 helper 이름 충돌·기본 IO fault 값/fixture 의존 참조 수리와 allocation reader 링크 누락, 독립 oracle의 exclusive-end 비교 오류도 원 source/실패 명령으로 보존했다. 마지막 오류는 <를 <=로 바로잡은 oracle 수리다.
- Regression/resource evidence: `build/task23k-regression-commands.json`은 변경 witness/helper를 새 binary에 연결해 legacy10237/system22236/result892와 checkpoint-aware5368/용량58864를 통과했다. 공개 옵션 구조·legacy 소유 할당은 바꾸지 않았다. 65536-ID 상한의131072-slot 이력 표·64page staging·1MiB hash scratch를 포함한 한 workspace7,866,584B가 실제 최고 소유 요청량이고 해제 뒤0이다. 모든 할당 지점 OOM/입력 options·memory·anchor 변경을 검사했다. hash/witness 단계는 그 뒤 workspace와 겹치지 않는다. transport/stack/allocator/RSS와 전체 앱 예산·공정성은 별도다. `build/task23k-static.json`의3module clang --analyze는 exit0/경고0다.
- Independent/scope evidence: `build/task23k-independent.py/.json`은 C helper 없이 struct/CRC32C/hashlib로3복원 fixture·반례의8파일, group/거래 chain·COMMIT 의존 참조·delta·재생성 pageLSN/CRC와 최종 data 전체 bytes,25입력 archive를 대조했다. `build/task23k-closure.json`의3문서 check_doc·whitespace/새 링크·최종 code/시험 archive 일치와 기본 STORAGE/DATABASE 목록 불변을 확인했다. 실제 빌드는0d3c83b6 위 미커밋 source이며 clean 부모 빌드가 아니다. #266 fresh read는 OPEN/5미체크이며 원격 쓰기·발행/PR·merge/배포/전환은 없다.
- Preservation/tracking: `build/task23k-preservation.json`이29662기존 artifact 항목/25seal·보호9070추적파일과 main/origin6bb8b2e8·다른4worktree·continuity 원prefix를 대조했다. Task6와 기존25미완료 섹션은 byte 동일하다. 원자료/YCSB/실패/이전binary·Vault/consumer를 보존하며 다른 agent/checkout은 사용하지 않았다. continuity는 검증 뒤 한 줄만 추가한다. 새 source/자료 해시는 `build/task23k-verification.json`에 둔다.23i/23j/23k 세 경계이므로 그 뒤 tracking-only checkpoint23k를 추가했고 원 after23/24–39와 모든 후속 checkpoint도 남긴다.
- Remaining risk: 전체 retained history가 필요하며 pruned checkpoint seed는 미지원이다. callback target 소유권·파일 생성/디렉터리 내구성, control/root 선택·전체 할당/liveness/도달성/pin·terminal/outcome/result 정합·내구 abort/번호 발급·APPLIED·일반 복구 활성화는 미완료다. invalid delta나 늦은 I/O/stream 변경은 앞선 물리 쓰기를 남길 수 있어 실패 target을 reader에 열지 않는다. 기존 DB/ABI/형식3/SNAPSHOT/default는 유지한다. 원래23·24–39의 MVCC/조기잠금·실제 독립writer·SQL/ACK/stream제품·원caller 전체 pending 경계·동등 COW 병합/재작성/회수와 WAL 반복scan/checkpoint/GC/복구 총비용은 미완료다. WAL 권고를 유지하며 component 성공은 실측 우위가 아니다. Goal remains active.
- AAR/next step: 서로 다른 성공 scan은 같은 의미 검증 view의 증거가 아니다. digest를 의미 verifier가 실제 본 bytes에 연결해야 한다. record 시작은 직전 exclusive 끝과 같을 수 있으므로 strict 비교를 경계 oracle에 쓰지 않는다. callback IO와 복구 membership/activation도 구분한다. 이 task-boundary에서 멈추며 다음 continuation은 새 checkpoint23k만 수행하고 그 뒤 원래23/필요 수리를 잇는다.
- Commit record: subject `feat(repo): 물리 WAL 복원과 재실행 검증 추가`; body marker `Goal-mode boundary: goal-1 task 23k: Implement bounded physical WAL redo`. 이 기록을 담은 commit의 사용자 작성자·marker·12파일 범위와 clean 상태를 경계에서 확인한다.

## Additional checkpoint after Task 23k

- Status: incomplete
- Work performed: not started.
- Verification evidence: required —23i/23j/23k component contracts, physical bytes/durability/restart/failure scope, source/evidence preservation and original23–39 obligations; tracking-only review.
- Remaining risk: physical redo is not product activation, reader visibility, durable ID query or real writer proof.
- Next step: execute in its own continuation after23k boundary; then original23 or the smallest necessary repair.
- Commit status: no checkpoint commit attempted.

## Task 23 — Implement WAL records and durable commit recovery

- Status: incomplete
- Scope: Implement the candidate's log framing/checksum/order, transaction commit/outcome markers, append/barrier and restart scan without SQL/callback replay. Implement the selected G2 outcome-resolution storage path from Task 22, preserving its explicit unsupported/corroboration boundary if persistent lookup is not selected. Issues: #263, #266.
- Work performed: not started.
- Verification evidence: required — Truncate/short/torn/reordered I/O, checksum, crash before/after barrier and UNKNOWN outcome tests on isolated files; acknowledged commit preservation. Test the selected G2 restart/ID/content/retention behavior without treating missing or expired records as NOT_APPLIED or reexecuting callbacks.
- Remaining risk: A successful append is not a durable commit; keep old backend and files intact.
- Next step: execute after the preceding task and any required checkpoint; insert prerequisite repairs if evidence demands them.
- Commit status: no task commit attempted.

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
