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

## User-directed continuous execution override — 2026-10-26

사용자는 Task24 커밋 뒤 “미완료인데 왜 멈춘거야 진행해”라고 지시했다. 이를 성공한 작업/체크포인트 경계마다 최종 응답으로 끝내지 말고, 같은 세션에서 원래 미완료 목표를 계속 수행하라는 최신 실행 지시로 적용한다. 위 Runtime Contract의 successful-boundary 즉시 stop과 client auto-advance 대기 조항보다 이 지시가 우선한다. 과업 순서·경계별 검증/기록/별도 commit·체크포인트의 tracking-only 수정·읽기 재개·dirty guard는 유지한다. checkpoint commit 뒤 현재 상태를 검증하고 다음 첫 미완료 단위로 진행하며 여러 단위를 한 commit에 섞지 않는다. 구현/검증 미완료·실패·외부 차단은 숨기지 않는다. 배포·원격 발행·파괴적 변경·Vault 직접 수정·새 worktree/백그라운드 대화 권한은 확장하지 않는다.

get_goal의 기존 등록은 역사적 blocked 상태이며 resume API는 없다. 중복 goal을 만들거나 전체 완료로 바꾸지 않고 기존 goal-current/goal-1의 active 파일 흐름을 실행 기준으로 계속한다. 코드 작성은 로컬 Claude 한 writer, parent는 검토·실행·증거·추적·commit을 맡는다. 현재 checkpoint24는 코드 변경 없이 감사 중이며 다음 구현은 감사 결과를 반영한 첫 미완료 과업이다.

## Audit preservation and current execution

Completed-entry prose is compacted during Task 16 after a full reread. The exact precompaction ledger is preserved in existing Git object `36529f6c57e08a04923df55f4a2c9765f7e85ff9:goal-1/tasks.md` (SHA-256 1813c082a787753bcc2215539f8352b3224430f05764ce6272bd411813915985). Use `git show 36529f6c:goal-1/tasks.md` and the unchanged section title for full work, commands, failures, limitations and historical next steps. This is a digest in the same approved tracking file, not a new context copy or rewritten history. At compaction, Task 6 source coverage/decisions/matrix and every incomplete entry remain verbatim; later execution updates only its own entry. The rejected earlier overall completion remains superseded. The Additional checkpoint after Task23 led to completed publication-counter and pinned-read repairs23s/23t. Task24 now completes isolated bounded buffering/live checkpoint validation. Checkpoint after Task24 is complete; the first incomplete task is required seeded log reuse24a, followed by Tasks25–39. Completed candidate recovery/buffering does not activate product SQL/ACK or establish MVCC/GC, actual independent writers, log reuse, streams or maintenance/recovery-inclusive COW/WAL measurements. The whole goal stays active; no next checkpoint or implementation is included in the Task24 commit. Existing runtime/default COW, data/ABI and all later requirements remain preserved.

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
- Work/evidence/risk/AAR/next/commit: 완료 상세는 고정 Git `69339a436d762c5631225b8e8067f9af92efccb7:goal-1/tasks.md`의 같은 제목에 보존한다. 원문 SHA-256은 `fab524425c7411c3dd22b430744895fee3996d49f0ac0b7b22a00dc00f6499c3`다. 실제 C·시험·판정기는 로컬 Claude가 작성했고 원 실패·동결 객체·독립 byte·음성 대조·사용자 commit과 각 postcommit 봉인은 그 기록을 따른다. 이는 해당 수리 완료이며 실제 writer·총비용·제품 완료가 아니다. 수리 뒤의 후보 구현과 모든 후속 요구는 각 해당 과업을 따른다.

## Task 23t — Retain malformed pinned-read failures across fallback

- Status: complete (CP23-3의 fail-closed 수리·로컬 검증; 실제 writer·총비용·제품은 미완료)
- Work/evidence/risk/AAR/next/commit: 완료 상세는 고정 Git `69339a436d762c5631225b8e8067f9af92efccb7:goal-1/tasks.md`의 같은 제목에 보존한다. 원문 SHA-256은 `fab524425c7411c3dd22b430744895fee3996d49f0ac0b7b22a00dc00f6499c3`다. 실제 C·시험·판정기는 로컬 Claude가 작성했고 원 실패·동결 객체·독립 byte·음성 대조·사용자 commit과 각 postcommit 봉인은 그 기록을 따른다. 이는 해당 수리 완료이며 실제 writer·총비용·제품 완료가 아니다. 수리 뒤의 후보 구현과 모든 후속 요구는 각 해당 과업을 따른다.

## Task 24 — Implement bounded buffering and checkpoint ordering

- Status: complete (isolated bounded buffering/live checkpoint implementation and local validation only; original log reuse remains Task24a and the whole goal remains active)
- Scope: Implement frame ownership/eviction, page LSN WAL-before-data, full-page torn recovery, bounded writeback and live checkpoint ordering under the Task 22 version contract. Issues: #263, #266. Original log reuse is preserved as Task24a below because actual full-history recovery lacks a reusable seed; this split does not complete that requirement.
- Work performed: how·두 architect·독립 심사로 선택한 공유 frame pool에 PageRef/lease·세대·dirty recLSN·기여 거래를 함께 소유시켰다. 완성 group 원자 수용, terminal NO-STEAL, WAL force 뒤 고정 사본 writeback, callback 동안 다른 page의 재변경 보존, FPI와 BEGIN 전체 cut → writeback/data sync → CHUNK/END force → 정본 증명 → 반대 control 게시를 구현했다. 종료 재진입·stale base·WAL-owned 수명 pin·canonical proof/control 사이 source 연결·BEGIN 뒤 BUSY 지속 차단·scan 오류와 독립 oracle를 수리했다. 실제 C·시험·판정기는 foreground nonpersistent 로컬 Claude가 작성했고 parent가 읽기 검토·명령/증거·기존 문서·추적·commit을 맡았다. 새 Make target은 선택형이며 기본 DB에는 연결하지 않았다.
- Verification evidence: 기존 `.local/runs/embedded-stream-renewal/`의 task24-final-manifest.json SHA-256 `ea6c1c9e938519bc87140394c35cf9340c7e72c9c4ff9168956f5a10d63442cd`이 최종 source/helper와 신규 산출물13558개의 hash를 고정한다. task24-parent-final-gates.json이 전체 판정을 연결한다. m1 default54와 별도 reduced-counter8의1사례, parent Make 대표17은 모두 통과했다. 새13단위는 Werror·fatal UBSan으로 컴파일했고 실제 native 환경은 UBSAN_OPTIONS=halt_on_error=1이다. Make17은54와 겹치는 대표 사례이며 합산하지 않는다. 독립 `python3 -I -O` default321/reduced2 raw set의 실패0, formal 음성46+9는 실제 child exit1·의도한 거절·양성0을 확인했다. checkpoint cut13·복구 후106·실제 고장 연결39는 별도 raw coverage다. scoped production/fixture/oracle/docs 검토의 남은 P1/P2는0이다. task24-review-a-production-final.json·task24-review-b-ready3.json·task24-review-b-final-artifact-audit.json·task24-parent-final-doc-review-binding.json에 범위를 남겼고179개 파일 링크의 오류0이다.
- Legacy regression and runner repair: 새 m1 WAL을 연결한 reads22/source12/counters10/publish11/gates8의63선택 사례가 통과했다. 마지막 segments는 실제46variants/23records, faults는227고장 반복/50reuse retry를 검증했다. task24-review-b-final-legacy-audit.json이 실제 code0·binary/source·환경·전체 assertion을 대조한다. 첫900초 segments timeout과 같은900초 중복 재시도의 parent SIGTERM은 그대로 보존한다. task24-code6-legacy-guard-prereg.json과 superseding addendum을 실행 전에 기록하고 동일 frozen binary·전체 검사로 fixed3600초 native/4000초 outer에서 다시 검증했다. segments2778.97초/faults1017.88초의 실제 종료는 correctness 결과이며 제품 성능·고객 SLO가 아니다. 다른 옛 recovery 객체와의 속도 비교·host load 단독 원인 주장은 철회했다.
- Before/after and failure taxonomy: task24-parent-before-after-classification.json은 이전14시험의9실패를 실제3경로·별도 hardening·metadata 양성·준비 실패로 구분한다. scan 미초기화는 정적 발견이며 종료 재진입을 관측 UAF crash로 부르지 않는다. task24-parent-source-before-after-final.json은 e14의2근본 공백/4경로에서 실제 잘못된 게시 성공/지속 fence 누락과 m1 수리 후 통과를 대조한다. callback fence는 메모리 상태 결함이며 byte 손상4개·관측 data corruption으로 세지 않는다. 약한 verifier와 open의 전제는 task24-parent-verifier-contract-adjudication.json을 따른다. 원 초안·오류·중단·고정 객체를 보존했고 code1–5의 stream error를 process0으로 성공 처리하지 않았다. code6는 최종 process0·last result success/is_error=false로 종료했다.
- Preservation and resources: task24-parent-end-preservation.json은 원69863산출물·보호tracked·ledger prefix·모든 기존 refs와5작업본의 변경/누락0을 확인한다. task24-parent-command-output-preservation-end.json의 고정 출력1972대조도 변경/누락0이다. task24-parent-final-resource.json은 seal 직전 전체 Task24의 logical1872556656B·stat blocks 합1586417664B·free19560513536B를 기록한다. allocated-sum1GiB 소프트 목표는 초과했지만8GiB-free hard stop은 도달하지 않았으며 정리하지 않았다. sparse 논리 크기·APFS 공유 extent를 포함할 수 있는 blocks 합·실제 free는 별개이고 global free 변화를 전부 이번 작업에 귀속하지 않는다.
- Remaining risk: caller 직렬화·고정 root의 격리 후보다. root 이동/type3 경계는25/27의 실제 SQL writer 통합 전에 해소해야 한다. MVCC/GC·독립 같은 프로세스/교차 프로세스 writer·bounded log reuse24a·제품·정리/복구 포함 COW/WAL 총비용은 미완료다. 반복 prefix scan/hash와 header 변경 IMAGE의 실제 비용은31/32에 포함한다. 실제 dependency를 검증하는 안정된 canonical control verifier는 caller 전제이며 open 자체가 전체 graph proof는 아니다. 외부 verifier/RSS와 내부 요청 예산을 구분하고 물리 power loss·2^64 전체 순회·측정된 backend 승리를 주장하지 않는다.
- Next step: 이24의 task commit에서 멈춘다. 다음 continuation은 Checkpoint after Task24만 수행하고 그 뒤24a →25–39를 기존 목표대로 이어간다. 현재 COW 기본 경로·기존 DB/package/ABI와 원자료를 보존하며 새 배포·원격 발행·Vault 반영을 하지 않는다.
- Commit record: subject `feat(repo): 후보 버퍼와 체크포인트 게시 순서 추가`; exact body marker `Goal-mode boundary: goal-1 task 24: Implement bounded buffering and checkpoint ordering`. 이 추적을 포함한 동일24경계의 로컬 commit을 사용자 Git identity로 기록하며 별도 checkpoint나 다음 구현을 포함하지 않는다. commit 뒤 log·blob·clean·main/다른 작업본 참조는 task24-postcommit.json으로 대조한다.
- AAR and assumption: self-consistent canonical proof가 pool이 의도한 source의 증명과 같지 않았고, syscall 시도와 성공·완료 순서도 별도 검증이 필요했다. 판정기 coverage/양성/의미 있는 음성을 먼저 고정해야 한다. 첫 호출의 범위 밖 `/tmp/placeholder` 삭제 명령과 code4 수동 tmp 출력의 사전 상태·효과는 미확인이며 해당 중단/보존 기록을 남긴다. 범용 Bash 허용만으로 쓰기 범위를 보장하지 못하므로 다음 코드 작성 호출은 Read/Grep/Glob/Write/Edit로 좁히고 parent가 검증 명령을 실행하는 기본을 사용한다. 전역 설정은 바꾸지 않는다. 내부 예산·시험 실행 제한은 고객 SLO가 아닌 가역 검증 설정으로 가정했다.

## Checkpoint after Task 24

- Status: complete (source/contract/evidence integrity audit only; no new implementation or performance samples)
- Work performed: HEAD66ad6470의 Task22–24 계약·공유 frame/lease·완성 group 원자 수용·terminal NO-STEAL·WAL force 선행·batch inflight/redirty·전체 BEGIN cut/CHUNK/END·정본 proof/control 반대 slot·WAL-owned pin/거부를 실제 source와 대조했다. 독립 읽기 검토 A/B와 parent 모두 현재 문서화된 유효 caller 계약 안에서 새 P1–P5를 확인하지 못했다. 공개 docs·목표·실제 지원/거절 경계와 원자료/보안/소유권·rollback/자원을 함께 검토했다. 구현 파일·plan/input·Vault는 수정하지 않았다.
- Verification evidence: `.local/runs/embedded-stream-renewal/checkpoint24-integrity.json`은 task24-final-manifest SHA-256 ea6c1c9e938519bc87140394c35cf9340c7e72c9c4ff9168956f5a10d63442cd와 봉인된13558산출물·source22/helper4의 변경/누락0을 현재 다시 확인한다. checkpoint24-review.json SHA-256 `d888f2847bea7726f393c35672c4eb19364e52c3d305dc1807b895311952d642`는 두 독립 검토와 parent 판정을 연결한다. 기존 실제54/default·1/reduced·17/Make(중복)·63/legacy·323/raw·55/negative와 fatal UBSan·고장/중단 근거를 같은 source/input hash로 재사용했다. 새 native/성능 실행으로 세지 않는다. task24-postcommit의24파일·사용자 identity·marker·clean·main/다른 작업본 보존을 실제 Git와 대조했다.
- Remaining risk: caller 직렬화·배타 파일 소유·안정된 실제 canonical verifier가 전제다. fixedroot/type3 경계는25/27 전 해소해야 하며 실제 bounded WAL 재사용24a·MVCC/GC·독립 writer·공정성·정리/복구 포함 COW/WAL 총비용·제품은 그대로 미완료다. 첫 tmp 삭제/수동 출력의 과거 영향은 미확인으로 유지한다. 기존 DB/COW 기본값·ABI·원 실패를 보존하며 승인 없는 배포/원격 발행/복구 삭제를 하지 않는다. 내부 요청 예산과 whole-app RSS, correctness guard와 성능 SLO를 구분한다.
- Next step: 최신 사용자 연속 실행 지시에 따라 이 tracking-only checkpoint를 별도 commit하고 상태를 검증한 뒤, 같은 세션에서 첫 미완료24a의 계약/설계·로컬 Claude 구현·검증을 순서대로 진행한다. code writer의 Bash는 제외하고 parent가 검사 명령을 실행한다. 실제 reuse seed/두 slot·reader·result 의존성의 증명 전 기존 로그를 삭제하지 않는다.
- Commit record: subject `docs(docs): 버퍼와 체크포인트 구현 감사 기록 갱신`; exact body marker `Goal-mode boundary: goal-1 checkpoint after task 24: complete`. 이 checkpoint의 단일 goal-1/tasks.md 변경만 포함한 사용자 Git identity commit이다. commit 뒤 marker·scope·clean·기존 refs 보존은 checkpoint24-postcommit.json으로 대조한다.
- AAR: 작업 경계의 검증/commit과 대화 중단은 별개다. 사용자의 미완료 연속 실행 지시를 Runtime Contract의 성공 경계 자동 stop보다 우선하도록 위 override에 기록했다. 과거 단일 writer 대기 메모로 최신 실제 writer/총비용 요구를 대체하지 않으며, 완료 분석과 고정 정상 시험을 반복하는 대신 다음 실제 미완료 구현으로 이어간다.

## Task 24a — Implement recovery seeds and actual bounded WAL reuse

- Status: incomplete
- Scope: Preserve the original Task24 log reuse obligation. Implement durable base/epoch transport and sufficient transaction/admission/result/page-generation/allocation recovery seeds, protect both control slots and reader/version dependencies, then reclaim and actually reuse bounded WAL space. Issues: #263, #266.
- Work performed: not started. Source-backed prerequisite split during24, not a completed retention-floor calculation.
- Verification evidence: required — Same committed data/results after pruned-history restart, torn page and retained full-image recovery, both-slot fallback, active/long-reader pressure, OOM/disk-full/delete/directory-sync interruptions, repeated cycles that recover actual capacity without LSN ABA or lost required history.
- Remaining risk: Current transport starts at segment0; existing checkpoint tuples and whole-prefix witnesses cannot safely open pruned history. No required segment may be deleted before complete seed/selection proof.
- Next step: execute after Task24 and its separate checkpoint, before25. Do not call Task24's original log-reuse requirement complete until this task passes.
- Commit status: no task commit attempted.

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

## Additional checkpoint after Task 26

- Status: incomplete
- Work performed: not started. Task24a/25/26의 세 경계 뒤 필요한 별도 checkpoint이며 기존 after27은 보존한다.
- Verification evidence: required — 원래 scope·실제 bounded buffering/seeded reuse·MVCC·조기 조정의 구현과 실패/자원·기록을 대조하는 tracking-only 검토.
- Remaining risk: 실제 writer27을 시작하기 전에 새 결함은 독립 수리 과업으로 남긴다.
- Next step:24a/25/26이 끝난 다음 continuation에서만 수행하고 tracking commit 뒤 멈춘다.
- Commit status: no checkpoint commit attempted.

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
