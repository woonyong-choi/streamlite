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

Completed-entry prose is compacted during Task 16 after a full reread. The exact precompaction ledger is preserved in existing Git object `36529f6c57e08a04923df55f4a2c9765f7e85ff9:goal-1/tasks.md` (SHA-256 1813c082a787753bcc2215539f8352b3224430f05764ce6272bd411813915985). Use `git show 36529f6c:goal-1/tasks.md` and the unchanged section title for full work, commands, failures, limitations and historical next steps. This is a digest in the same approved tracking file, not a new context copy or rewritten history. At compaction, Task 6 source coverage/decisions/matrix and every incomplete entry remain verbatim; later execution updates only its own entry. The rejected earlier overall completion remains superseded. The Additional checkpoint after Task23 records new publication-counter and pinned-read findings. Current first incomplete task is Task24; Task23s repairs the publication counters and Task23t retains malformed pinned-read failures across fallback. The candidate-file durable recovery integration is complete at Task23;23a–r remain its prerequisite evidence. This closes the isolated owning recovery/query path, not product SQL/ACK activation, actual writers, live buffering/MVCC/GC/log reuse, stream integration or maintenance-inclusive COW/WAL measurements. The whole goal stays active. The checkpoint is a separate continuation after the Task23 commit; its required repairs precede Task24. Existing runtime/default COW, data/ABI and all later requirements remain preserved.

Task 23d compacts only completed Task16–23c entries and their completed checkpoints after source review. Their exact full ledger remains in Git object `623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md` (SHA-256 31dd489d6de49399434544cbfa2353500aa28e3cb458536bc2fbfe5a1d14d280). Task6 source coverage/requirement matrix and all incomplete entries remain verbatim; no source history, failure artifact or requirement is removed. This adds no context copy.

Task 23g compacts only the completed 23d/e/checkpoint23e/23f records against Git `f0219294c5fc47a8cf312ea7b728a8dceb0bbd17:goal-1/tasks.md` (SHA-256 c2b59381598f3c179d9d785cda63ec682df8de7149863009fc19486c2eec6223). Task6 and all existing incomplete sections remain byte-identical. This is tracking maintenance within the allocation implementation boundary; no new context file or removed requirement.

Task23n compacts completed23l/m only against Git `c7d5333023e93fedb11c3395c78fbf469e00677c:goal-1/tasks.md` (SHA-256 3bc228f973e7d25cf346aa5812bff8264cc3fea19e6a18bb7305df072a85b467); Task6 and all25 old incomplete sections stay byte-identical. Original failures and full records remain in Git and existing execution artifacts.

Task23r compacts61 completed task/checkpoint records against Git `92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md` (SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066). A direct section comparison confirms Task6 and all26 previously incomplete entries remain byte-identical before23r closure. Full records, raw data and failure samples remain available in their original Git/execution locations. Only the current23r entry changes at this task boundary; the25 other incomplete entries, including original23, stay preserved.

Task23s는 반복 재개의 전체 읽기 비용을 줄이기 위해 완료 항목 65개의 상세 기록을 고정 Git `4fde14e8b87c7a6478e0cb9add511ade6a196ec0:goal-1/tasks.md`로 연결한다. 원문 SHA-256은 `7a4402ea2db19b00d974234cd3e460c092816bb59d8ecb04920fc9e1ef9fbfec`이다. Task6와 모든 기존 미완료 섹션은 byte 단위로 보존하며 원자료·실패·Git 이력을 삭제하지 않는다.

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
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 2 — Specify the proposed concurrency and resource contract

- Status: complete (contract proposal and static review only; no runtime implementation or approval of defaults)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 3 — Fix the separated comparison and adoption gates

- Status: complete (comparison/adoption design proposal and static validation only; no experiment collection or engine decision)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Checkpoint after Task 3

- Status: complete (review with one required documentation repair queued; no implementation validation)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 4 — Restore bounded pending-query validation

- Status: complete (CP3-1 design coverage repaired; no memory measurement or consumer implementation)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 5 — Connect bounded follow-ups and reconcile #257

- Status: complete (follow-up structure, issue reconciliation and documentation only; no approved contracts, engine implementation or new measurements)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Prior final review — superseded by user correction

- Status: complete (the registered decision/documentation objective only; contracts remain proposals and future engine work remains open)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

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
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 7 — Make the present storage architecture judgment

- Status: complete (present architecture recommendation and source/document verification; no new backend implementation, measured superiority or product cutover)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 8 — Reconcile existing design and rationale with the judgment

- Status: complete (existing public rationale reconciled; no backend implementation, new measurement or product cutover)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 9 — Reconcile actual unfinished work and issue gates

- Status: complete (issue/rationale reconciliation and restored execution queue only; all new execution tasks remain incomplete)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Checkpoint after Task 9

- Status: complete (scope/order/evidence review with a tracking clarification; no engine or measurement validation)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Execution closure rules after Task 9

This queue restores authorized work; it does not assert implementation, approved customer SLOs or measured benefit. Each task below is incomplete and must record its actual work, evidence, residual risk, next action and task-boundary commit at execution. Resolve its applicable contracts before changing behavior. Use reversible test defaults with reasons and falsifiers; do not wait indefinitely for an unnamed approval. A required failed test/measurement or unmet user outcome inserts a repair before dependent work, without replacing the original criterion or erasing failed samples. Candidate rejection can finish that experiment, not an unmet product requirement. Keep diagnostics and noninstrumented outcomes separate, and preregister collection before formal samples. Reuse existing approved docs and execution areas; no new context copies, consumer writes, production migration or publication is inferred.

GitHub's #266 → #263 edge is the comparison-result → product-integration gate. Tasks 22–30 implement and verify comparison candidates for #266 before that result, while Task 33 integrates the chosen product path. Relevant phase contracts and this prototype/product distinction must be reflected in actual issue readiness before code execution; an entire unpublished design issue is not an indefinite stop for authorized reversible local work. Do not claim remote issue closure while its PR/evidence conditions remain unmet.

## Task 10 — Fix the common execution and resource contract

- Status: complete (common contract selection, source/document verification and phase readiness; no cache/workspace/lifecycle implementation or new measurement)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 11 — Implement bounded SQL template reuse

- Status: complete (local internal comparison implementation and regression; no measured adoption, public prepared API or new storage backend)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 12 — Implement optional queue lifecycle

- Status: complete (local Python lifecycle implementation and regression; no noninstrumented cost/adoption result or stream implementation)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Checkpoint after Task 12

- Status: complete (common-contract/cache/lifecycle audit; documentation consistency repair queued, not engine or performance completion)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 12a — Reconcile cache admission and implementation status

- Status: complete (documentation and issue-summary repair only; no runtime/default change or new measured effect)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 13 — Implement transaction-private accumulated changes

- Status: complete (local default-OFF workspace implementation and regression; no noninstrumented effect/adoption, new backend or actual independent writers)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 14 — Build and preregister common-cost comparisons

- Status: complete (collector, preregistration and controls only; no formal performance collection, adoption or new backend)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Checkpoint after Task 14

- Status: complete (review with a required collector repair before formal collection; no engine or performance completion)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 14a — Repair common-cost campaign deadline and reporting

- Status: complete (collector deadline/reporting repair and controls; no formal effect collection, adoption or new backend)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 15 — Collect and judge cache and workspace effects

- Status: complete (preregistered E1/E2 collection and hold disposition; no default adoption, new backend or product integration)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Checkpoint after Task 15

- Status: complete (evidence/requirements review; bounded analysis-metadata repair queued, no new implementation or collection)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 15a — Repair sample classification and historical analysis regeneration

- Status: complete (sample metadata and offline reproduction repair; no new formal effect collection or product/default change)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 16p — Support indexed scalar-subquery equality

- Status: complete (general SQL prerequisite and regression only; original conditional queue integration and measurement remain incomplete)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 16 — Implement conditional queue updates

- Status: complete (local internal comparison implementation and regression; no formal effect collection, adoption or new backend)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 16

- Status: complete (scope/source/evidence review; no new runtime implementation or formal measurement)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 17 — Measure conditional SQL and optional lifecycle

- Status: complete (fixed-plan formal collection, verified regeneration and hold disposition; no runtime/default adoption or backend implementation)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 17

- Status: complete (scope, provenance and measured-disposition audit; no new runtime implementation or formal samples)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 18 — Build and preregister pending-query measurements

- Status: complete (로컬 수집기·사전 판정·정상/실패 대조만 완료; 정식 효과·소비자 교체·새 backend 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Checkpoint after Task 18

- Status: complete (원래 범위·구현/실측 근거 검토 완료; CP18-1/2 검증기 수리는 미완료로 추가)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 18a — Require complete pending-query artifact coverage

- Status: complete (로컬 pending 자료 coverage 수리와 회귀만 완료; 공통 수리·정식 효과·제품 전환 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 18b — Require complete common-cost artifact coverage

- Status: complete (로컬 공통 자료 검증 수리·과거 재처리·연동 대조 완료; 새 정식 효과·제품 전환 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 19 — Measure pending queries and the real caller boundary

- Status: complete (고정 계획의 전체 격자 수집과 읽기 전용 감사 완료; actual caller replacement, whole-app solution and new backend remain incomplete)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 19

- Status: complete (범위·자료 완전성·재현·판정 감사; 새 구현·효과 표본이나 원 소비자 해결 완료와 구분)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 20 — Prepare the missing primary-key scale validation

- Status: complete (수집기·고정 계획·작은 대조 검증만 완료; Task21의 정식 규모 수집은 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 21 — Execute the retained primary-key scale validation

- Status: complete (원래 고정 규모9case의 로컬 정식 수집·검증 완료; 새 backend·제품 통합/발행과 구분)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Checkpoint after Task 21

- Status: complete (로컬 범위·source·자료·재현·판정 검토 완료; 새 효과/제품 구현·원격 상태 확인과 구분)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 22 — Specify WAL and version recovery contracts

- Status: complete (격리된 후보의 계약·대안·정적/제한 모형 검증 완료; 실제 엔진 구현·중단 복구·성능 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23a — Implement segmented WAL framing and restart scan

- Status: complete (격리된 WAL transport 구현·실파일/byte 검증만 완료; 원래23의 거래/영속 결과 복구·제품 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23b — Validate transaction and physical-group WAL witnesses

- Status: complete (격리된 WAL의 의미 분석·실파일/독립 byte 검증 완료; 영속 본문·redo·복구 활성화는 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 23b

- Status: complete (22/23a/23b 검토와 실제 결함 재현 완료; CP23b-1 수리를 다음 과업으로 추가)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23c — Bind WAL operations to the owned files

- Status: complete (후보 파일 소유권·barrier 결함 수리와 로컬 검증 완료; 거래 복구·제품 통합 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23d — Encode version pages and verify stored result bodies

- Status: complete (일반 page codec·소유 본문 검증·합성 WAL/page roundtrip 완료; ID 조회·일반 복구 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23e — Store and look up bounded request outcome metadata

- Status: complete (bounded metadata codec·raw ID 조회와 합성 WAL/data 검증; 일반 복구·APPLIED 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 23e

- Status: complete (23c–23e의 코드·자료·계층 경계 검토; 원래23과 전체 목표는 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23f — Implement recovery control slots and durable anchor publication

- Status: complete (control codec·소유 파일·합성 의존성 anchor/내구 게시 검증; 원래23의 일반 복구는 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23g — Encode allocation snapshots and validate generation transitions

- Status: complete (type5 snapshot·소유 표·메모리 세대 전이 검증; 내구 system/일반 복구는 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23h — Validate WAL system groups and recovery state transitions

- Status: complete (opt-in system/할당·예약·epoch witness와 metadata 대조; 내구 발급·일반 복구는 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 23h

- Status: complete (23f/g/h component/source/evidence review; original23 and the full goal remain incomplete)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23i — Read anchored checkpoint state and verify complete chunks

- Status: complete (실제 checkpoint 형식·control anchor 부분 검증·소유 snapshot; 일반 복구 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23j — Corroborate checkpoint state with retained WAL history

- Status: complete (checkpoint/보존 WAL의 읽기 전용 의미 대조; 일반 복구 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23k — Implement bounded physical WAL redo

- Status: complete (격리 target의 image/delta 복원·재실행; 일반 복구 활성화 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 23k

- Status: complete (23i/j/k component/source/자료 검토; 일반 복구와 전체 목표 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23l — Corroborate persisted outcomes with WAL and result bodies

- Status: complete (component implementation/real-file validation only; general recovery and product remain incomplete)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23m — Coordinate exclusive recovery and validate reconstructed state

- Status: complete (component implementation/real-file validation only; general recovery and product remain incomplete)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23n — Durably resolve unfinished WAL transaction terminals

- Status: complete (WAL terminal 내구 정리·실파일/실제 중단 검증; 일반 복구 활성화와 전체 목표 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 23n

- Status: complete (23l/m/n 검토·실제 용량/정합 반례 재현; CP23n-1 수리를 다음 과업으로 추가)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23o — Reconcile aborted result fragments with persisted outcome semantics

- Status: complete (CP23n-1 결과 의미 수리·실파일/독립 검증; 일반 복구 활성화와 전체 목표 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23p — Durably repair incomplete WAL physical tails

- Status: complete (미완료 물리 WAL tail의 내구 정리·실파일 검증; 일반 복구 활성화와 전체 목표 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23q — Reconstruct complete retained request outcomes

- Status: complete (소유 결과표 재구성·미게시 encoding·실파일 검증; 일반 복구 활성화와 전체 목표 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 23q

- Status: complete (23o/p/q 계약·자료 검토와 실제 연결 반례 재현; CP23q-1 수리를 다음 과업으로 추가)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23r — Bind checkpoint-aware system history to the selected anchor

- Status: complete (CP23q-1 상태 연결 수리·실파일/독립 검증; 일반 복구 활성화와 전체 목표 미완료)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23 — Implement WAL records and durable commit recovery

- Status: complete (isolated candidate durable recovery integration and local validation; product integration, actual writers and total-cost measurements remain incomplete)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Additional checkpoint after Task 23

- Status: complete (current source/evidence review and native counterexample; required repairs23s/23t remain incomplete)
- Evidence/risk/AAR/next/commit: 위 Task23s 보존 기준 Git 원문의 같은 제목. 원래 완료 범위와 근거·한계는 그 기록을 따르며 현재 첫 미완료 작업을 재개한다.

## Task 23s — Preflight recovery publication counters

- Status: complete (CP23-1/2 후보 복구 게시 수리·로컬 검증; CP23-3·실제 writer·총비용·제품은 미완료)
- Scope: group ID와 control generation/selected 반대 slot parity의 잔여 발급량을 신규 metadata 효과 전에 검사한다. Issues: #266, #263. 기존 DB·공개 ABI·format·default COW와 Task23t/24–39는 보존한다.
- Work performed: clean4fde14e8에서 시작해 로컬 Claude가 C·시험·byte 검증기를 작성했다. private prepare_publication이 identity→highest 한도→selected 반대 slot/parity→codec→follows를 한 번만 정의한다. 내부 check와 실제 publish가 공유하며 실제 publish만 verifier·쓰기·fsync를 수행한다. 접근/소유 검사와 poison 계약은 유지한다. 복구의 중복 highest/follows는 제거했다. INVALID→CORRUPT wrapper는 열린 비공개 handle과 유효 next를 쓰는 현재 두 경로에서 codec 오류에만 도달함을 독립 검토했다. 실제 preflight groups를 UINT64_MAX-max_group_id와 비교하며 equality/zero need를 허용한다. late guard·토큰·persistent 형식은 추가하지 않았다.
- Before/after evidence: `.local/runs/embedded-stream-renewal/task23s-counters-final-{before,after}-run-*.json`은 동일 `task23s-counters-final-test.o` SHAfd2907d97d509bc0755a91596d387320d7c5487d3a2faf08b1ccee232772d6fa를 연결한다. 수리 전10조건 중4반례는 예상 assertion 실패이며 수리 후10조건 모두 native0/timeout없음/stderr0이다. gid MAX/MAX-1은 각각5WAL·2data/11WAL·5data 뒤CORRUPT에서 LIMIT/NULL·0writes·입력동일·재시도0writes로 바뀐다. gen MAX는14WAL·5data 뒤LIMIT에서0writes로, gen MAX-1 fallback은17WAL·6data 뒤LIMIT에서 기존 restore ABORT2개만 남긴 LIMIT로 바뀐다. 후자는 신규 group/checkpoint/data/control0이고 재시도0writes다. CP23-2도 실제 복구 경로에서 재현했다.
- Positive/frozen evidence: default·MAX-2의2group 발급, 중단 뒤 필요한 group0/1개, single MAX-1·fallback MAX-2의 generation MAX 게시, strict reopen·동일 owner 재진입0writes가 통과한다. 원 checkpoint23-gid3 object도 그대로 재연결한4실행이 성공했다. 이름정리 전 object a7f3c827…와 최종 fd2907d9…의 전후 실행을 구분해 보존했다. 최종 source에서 주석4개만 뺀 fresh reviewed object로 핵심2smoke도 통과했다. `task23s-reviewed-repair.json`은 exact comment-only hash 동등성을 기록한다.
- Byte/build/regression evidence: `task23s-bytes-final.json`과 부모 재실행 `task23s-parent-bytes-final.json`은 C helper 없이 고정 Python decoder319ea7bb…로18fixture의 framing/CRC·실제 group ID/slot/generation·checkpoint 유무를 대조한다. 전체 catalog/result 의미 검증으로 확대하지 않는다. 실제 `task23s-make-final-counters.json`의 target10조건, boundary14형태/29294검사, small faults227회/122710검사·114OOM·50예약재사용, control16978/checkpoint2731/recovery1051212/wal_checkpoint5368/terminal4260, 작은 publish4/source4가 모두 성공했다. 상세 argv·object·stdout/stderr는 first-phase95records의 `task23s-manifest.json`에 있다. 소스7개의 clang 분석과 이름정리 후 추가 분석, 최종 unit의 부모 clang 분석은 경고0이다. 변경 모듈·새 시험은 fatal UBSan/-Werror로 빌드했다. frozen 구형 control object만 recoverable UBSan이며 HEAD 재컴파일과325782B 역어셈블이 동일하다. 모든 old object가 fatal이라고 주장하지 않는다.
- Review/verification repair: 독립 검토 둘의 잔여 P1/P2/P3/MUST KILL은0이다. STALE가 exhausted LIMIT에 가려진 추가시험 P3를 수용했다. 정상 generation에서 check/publish를 각각 실행해 STALE13·codecINVALID1과0write/verify/sync, slot 보존 및 이후 정상 publish를 확인했다. 최종 control은17044검사/0위반이다. no-comments 수용5줄만 삭제하고 내부 API 부수효과 계약은 남겼다. reviewed smoke의 첫2시도는 case인자 누락으로 argc CHECK에서 끝났으며 보존하고 올바른 두 실행은0/stderr0이다. engine 반례로 세지 않는다. 부모 독립 fallback 실행과 초기/최종 byte 재실행도 성공했다. 최종 scope·보존·source/evidence는 `task23s-parent-final-verification.json`, commit identity/marker/scope/clean은 `task23s-postcommit.json`에서 확인한다.
- Preservation/AAR: `task23s-preservation.json`과 부모 보존 검사는61024원artifact 변경/누락0을 확인한다. 보호 tracked·ledger prefix·main/origin6bb8b2e8와 다른4worktree refs도 보존한다. 첫 manifest의 실행 중 log hash는 현재 code1 log의 첫2056646B와 정확히 같다. 최종 seal은 종료 후 full hash를 기록하며 원 manifest를 덮지 않는다. 첫 Make source/object/binary/report와 모든 실패를 보존했다. 완료65개 이력만 위 고정 Git으로 압축했고 Task6 및 다른24미완료 섹션은 byte동일하다. 작은 fault/boundary 원자료를 삭제하지 않고 산출물 소프트 목표만512MiB로 높였으며8GiB-free stop은 유지한다. 유한 group 개수는 잔여 ID가 아니며 오류분류 시험은 앞선 한도 오류에 가려지지 않아야 한다.
- Remaining risk: counter 부족은 신규 reservation/stage/checkpoint/control 앞에서 거절한다. 선행 terminal/tail/redo 효과와 추후 verifier/I/O 실패까지0이라는 뜻은 아니다. 과거 손상 파일을 치유하지 않는다. 현재 권고는 page/redo WAL·bounded buffer pool·MVCC·조기 조정이며 실측 우위가 아니다. CP23-3은23t에서 수리해야 하고 실제 writer27/28·동등 COW29·안전성30·총비용31/32·제품/stream은 미완료다. dense/quota/large-fault/segments·sparse/나머지 source·벤치마크를 이번에 재실행하지 않았다. 기존 근거는 원 cohort의 한계 안에서만 재사용한다. 물리 전원 손실·전체 제품 메모리·성능 개선·배포를 주장하지 않는다.
- Next step: 이 로컬 task-boundary commit과 검증 뒤 멈춘다. 다음 continuation은23t 하나이며 그 뒤 기존24–39·모든 checkpoint·최종 review를 이어간다. 별도 백그라운드 대화·Vault/consumer/원격쓰기·push/PR/merge/배포/전환은 없다. 파일 기반 goal은 active다. built-in의 기존 blocked 표시는 resume 수정 연산이 없는 도구 한계이며 새 목표를 초기화하지 않았다.
- Commit record: containing task-boundary commit subject `fix(repo): 복구 게시 번호 소진의 사전 검사 추가`; exact body marker `Goal-mode boundary: goal-1 task 23s: Preflight recovery publication counters`. 사용자 Git identity로 이 수리와 관련 tracking10파일만 기록한다. postcommit을 확인하고 이번 턴에23t나 checkpoint를 수행하지 않는다.

## Task 23t — Retain malformed pinned-read failures across fallback

- Status: complete (CP23-3의 fail-closed 수리·로컬 검증; 실제 writer·총비용·제품은 미완료)
- Scope: CP23-3의 pinned view와 malformed 임의 읽기/fallback 경계에서 확인된 실패가 control fallback에서 사라지지 않도록 수리한다. Issues: #266, #263. 실제 코딩은 로컬Claude가 맡는다.
- Work performed: clean ac9f9921에서 goal 전체·Task23s commit/identity/seal을 확인하고 원 readat2 객체를 현재23s 모듈에 연결해 반례를 재현했다. how·이력·두 architect와 독립 심사는 기존 WAL-owned reject를 선택했다. 오류 출처 타입을 control까지 전파하는 안은 상태 소유와 인터페이스를 중복하므로 기각했다. 로컬 Claude가 C·시험·검증기를 작성했고 부모는 실행·검토·승인된 기록과 commit을 맡았다. read-at의 header CORRUPT, frame의 valid_end 초과, decode CORRUPT만 reject로 연결했다. control fallback·다른 오류 분류·기존 LSN/digest 거부·공개 ABI/format/default는 보존한다.
- Before/after: `.local/runs/embedded-stream-renewal/task23t-reads2-{before,source-before,after}-run-*.json`은 같은 시험 객체 SHA `b9a4acd9f36a7aa42655dec6d13c30ebaefff45358b04f4f69810fe8da51b276`를 연결한다. 원 WAL 객체와 HEAD 새 컴파일은 byte동일하고 after는 WAL 하나만 교체한다. before/source-before는 각각22조건 중13개가 예상 assertion 실패이며 after22개는 모두0/timeout없음/stderr0이다. 두 유효 control 후보의 stableA calibration은 root4다. IMAGE/DELTA×header/CRC/끝 초과6반례는 실제 type4/5 hook·주입1회 후 stableA로 돌아온다. before의 OK/root3·15WAL/7또는8data/1control·sync11은 after의 CORRUPT/NULL·모든 효과0·파일동일로 바뀐다. 별도 stableA 복구는 root4다. 직접 WAL7반례의 후속 read/scan/append/force도 pinned CORRUPT, unpinned POISONED로 유지하며 reopen은 성공한다. 정상 dependency fallback root3와 INVALID/UNSUPPORTED/IO 분류도 통과한다. NOMEM은 preview 할당2626736B 실패·효과0이며 read-at 할당 실패나 hook 적중으로 주장하지 않는다.
- Parent proof/regression: 부모의 `task23t-parent-original-after-run.json`은 보존된 원 객체로 단일 CRC 손상 뒤 CORRUPT/효과0과 stable 재개방 root4를 직접 확인한다. source11조건, 작은 publish basic/selection/empty/resolved, control16978/checkpoint2731/recovery1051212/terminal4260/wal_checkpoint5368/redo6945/recovery_wal21835검사와 WAL12배치가 통과했다. 실제 Make1/2 및 최종 Make3의22조건도 성공했다. 변경 없는 dense/quota/large-fault/벤치마크는 반복하지 않았다. 변경 WAL·신규/재컴파일 시험은 fatal UBSan/-Werror이며 frozen 의존 객체는 원 계측 그대로다. 모든 구형 객체가 fatal이라고 확대하지 않는다.
- Byte/evidence repair: 최초 bytes2는 사실을 출력하기만 하므로 자동 판정 완료 근거에서 제외했다. 부모와 검토자 둘이 확인한 P2에 따라 상태·root·효과·원자료·frame·object·hash·종료 코드를 강제 판정하도록 수리했다. 부모가 직접 재현한 잘못된 --only의0건 성공도 membership·빈 선택/빈 cohort 거부로 수리했다. `task23t-bytes-selector-final.json`은2cohort×3연결×22조건132run/오류0이다. 부모 재실행 `task23t-parent-bytes-final.json`도 원본과 byte동일하며 SHA `45c76c384fed26d1ba39ea7608b524aad81036e42fb2df888ead0d467c662f82`다. 독립 decoder는 불변 Python helper319ea7bb…이고 C codec을 쓰지 않는다. `task23t-bytes-negative2.json`의107대조는83fact·9raw·15실제CLI, 예상과 다른 결과0이다. 실제 raw 변형과 기록/hash/객체/선택 오류를 거부하고 원본·정상 selector는 통과한다. 부모의 invalid-only 최종 probe도 child1/오류2/0run이며 이전 child0 반례는 보존한다.
- Review/source: 독립 검토 둘의 최종 P1/P2/P3은0이다. 중복 설명 주석2줄은 로컬 Claude가 제거했다. 최종 소스는 frozen2에서 이 두 줄만 제거한 것과 정확히 같으며 Make3 object와 새 `task23t-reads3.o` SHA28b933a4…가 일치한다. 마지막 clang 분석은 경고0이다. 첫 정적 분석 경고는 test pointer 조건을 명시해 수리한 reads2로 구분하며 첫 ba52e0dd… cohort도 보존했다. `task23t-manifest.json`, `task23t-reviewed-repair.json`, `task23t-selector-repair.json`은 각각 원 상태의 seal로 남기고 최종 부모 manifest가 source·review·명령·원자료를 연결한다.
- Preservation/AAR: `task23t-parent-preservation.json`은 기존63170artifact·보호9113tracked의 변경/누락0, ledger44613B prefix·main/origin6bb8b2e8·다른4worktree refs 보존, Task6와 다른23미완료 섹션의 byte동일을 확인한다. 초안의 지원되지 않는 system DELTA와 갱신 metadata 비교 오류, verifier v1 KeyError, 첫 clang 경고, reporter·빈 selector 반례를 engine 실패와 구분해 모두 보존한다. draft 소스 별도 사본은 없지만 object·실패 입력/출력·도구 로그는 남아 있고 완료 증거는 동결 cohort만 쓴다. 검증기가 사실을 출력하는 것과 기대값을 강제하는 것은 다르며 정상 입력·음성 대조·검사 건수가 모두 필요하다. 원 v1/v2/v3·negative helper 별칭은 SHA로 연결한다. 산출물 소프트 목표만512MiB로 높였고8GiB-free stop·DB quota·SLO는 바꾸지 않았다.
- Remaining risk: source의 일시적인 메모리 응답 손상을 주입한 fail-closed 검증이며 업무 데이터 손실·물리 전원 손실·전체 RSS·성능 개선 증거가 아니다. 선택 preview6조건의 효과0을 모든 복구 실패의 rollback으로 확대하지 않는다. 과거 손상 파일을 치유하지 않는다. 판정기는 고정 fixture/FAIL 줄/효과/NOMEM 크기의 감사 도구이며 시험 변경 때 기대값도 다시 검토해야 한다. 현재 권고는 page/redo WAL·bounded buffer pool·MVCC·조기 조정이고 실측 우위는 아니다. 실제 writer27/28·동등COW29·양쪽 안전성30·정리/복구 포함 총비용31/32·제품/stream은 미완료다.
- Next step: 이 로컬 commit과 postcommit 검증 뒤 멈춘다. 다음 continuation은24의 bounded buffering/checkpoint ordering이며 그 뒤 after24와 기존25–39·모든 checkpoint·최종 review를 보존한다. goal은 active다. 별도 백그라운드 대화·Vault/consumer/원격쓰기·push/PR/merge/배포/전환은 없다. built-in의 기존 blocked 표시는 resume 수정 연산이 없는 도구 한계로 기록하며 새 목표를 초기화하지 않았다.
- Commit record: containing task-boundary commit subject `fix(repo): 고정 WAL 읽기 손상의 복구 거부 유지 수정`; exact body marker `Goal-mode boundary: goal-1 task 23t: Retain malformed pinned-read failures across fallback`. 사용자 Git identity로 관련5파일만 기록한다. postcommit은 `task23t-postcommit.json`으로 확인하고 이번 턴에24나 checkpoint를 수행하지 않는다.

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
