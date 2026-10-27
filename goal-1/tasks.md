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

사용자는 Task24 커밋 뒤 “미완료인데 왜 멈춘거야 진행해”라고 지시했고, 이어 “멈추지마”라고 명시했다. 이를 성공한 작업/체크포인트 경계마다 최종 응답으로 끝내지 말고, 같은 세션에서 원래 미완료 목표를 계속 수행하라는 최신 실행 지시로 적용한다. 위 Runtime Contract의 successful-boundary 즉시 stop과 client auto-advance 대기 조항보다 이 지시가 우선한다. 과업 순서·경계별 검증/기록/별도 commit·체크포인트의 tracking-only 수정·읽기 재개·dirty guard는 유지한다. checkpoint commit 뒤 현재 상태를 검증하고 다음 첫 미완료 단위로 진행하며 여러 단위를 한 commit에 섞지 않는다. 구현/검증 미완료·실패·외부 차단은 숨기지 않는다. 배포·원격 발행·파괴적 변경·Vault 직접 수정·새 worktree/백그라운드 대화 권한은 확장하지 않는다.

get_goal의 기존 등록은 역사적 blocked 상태이며 resume API는 없다. 중복 goal을 만들거나 전체 완료로 바꾸지 않고 기존 goal-current/goal-1의 active 파일 흐름을 실행 기준으로 계속한다. 코드 작성은 로컬 Claude 한 writer, parent는 검토·실행·증거·추적·commit을 맡는다. checkpoint24는29479895로 완료됐으며 현재 첫 미완료 과업24a를 같은 세션에서 진행한다.

## Audit preservation and current execution

Completed-entry prose is compacted during Task 16 after a full reread. The exact precompaction ledger is preserved in existing Git object `36529f6c57e08a04923df55f4a2c9765f7e85ff9:goal-1/tasks.md` (SHA-256 1813c082a787753bcc2215539f8352b3224430f05764ce6272bd411813915985). Use `git show 36529f6c:goal-1/tasks.md` and the unchanged section title for full work, commands, failures, limitations and historical next steps. This is a digest in the same approved tracking file, not a new context copy or rewritten history. At compaction, Task 6 source coverage/decisions/matrix and every incomplete entry remain verbatim; later execution updates only its own entry. The rejected earlier overall completion remains superseded. The Additional checkpoint after Task23 led to completed publication-counter and pinned-read repairs23s/23t. Task24 completes isolated bounded buffering/live checkpoint validation and its checkpoint is complete. Task24a1 completes isolated in-memory semantic transfer; Task24a2a completes isolated owning validation views; checkpoint24a2 is complete; R1/R2 and24a3a are complete; checkpoint24a3a and native25a are complete; next is remaining25, then writer-first26/27 before returning to required seeded log reuse24a. All remaining24a subunits and Tasks25–39 remain required. Completed candidate recovery/buffering/semantic transfer and bounded native25a do not activate product SQL/ACK or establish full MVCC/GC, SQL/process writers, log reuse, streams or maintenance/recovery-inclusive COW/WAL measurements. The whole goal stays active. Existing COW transaction behavior and original data evidence remain preserved; latest R1 explicitly replaces old namespace/format acceptance, while all writer/total-cost/product requirements remain.

Task 23d compacts only completed Task16–23c entries and their completed checkpoints after source review. Their exact full ledger remains in Git object `623e9d5c4da00a0adb842d3e8d4213d839ad6976:goal-1/tasks.md` (SHA-256 31dd489d6de49399434544cbfa2353500aa28e3cb458536bc2fbfe5a1d14d280). Task6 source coverage/requirement matrix and all incomplete entries remain verbatim; no source history, failure artifact or requirement is removed. This adds no context copy.

Task 23g compacts only the completed 23d/e/checkpoint23e/23f records against Git `f0219294c5fc47a8cf312ea7b728a8dceb0bbd17:goal-1/tasks.md` (SHA-256 c2b59381598f3c179d9d785cda63ec682df8de7149863009fc19486c2eec6223). Task6 and all existing incomplete sections remain byte-identical. This is tracking maintenance within the allocation implementation boundary; no new context file or removed requirement.

Task23n compacts completed23l/m only against Git `c7d5333023e93fedb11c3395c78fbf469e00677c:goal-1/tasks.md` (SHA-256 3bc228f973e7d25cf346aa5812bff8264cc3fea19e6a18bb7305df072a85b467); Task6 and all25 old incomplete sections stay byte-identical. Original failures and full records remain in Git and existing execution artifacts.

Task23r compacts61 completed task/checkpoint records against Git `92c7babf808a0e6a524093d1709357a1233ed10c:goal-1/tasks.md` (SHA-256 b625dcc6580e55900e356d1ba9fa4e9fcdebd6671c4795766e70f4117f7cf066). A direct section comparison confirms Task6 and all26 previously incomplete entries remain byte-identical before23r closure. Full records, raw data and failure samples remain available in their original Git/execution locations. Only the current23r entry changes at this task boundary; the25 other incomplete entries, including original23, stay preserved.

Task23s는 반복 재개의 전체 읽기 비용을 줄이기 위해 완료 항목 65개의 상세 기록을 고정 Git `4fde14e8b87c7a6478e0cb9add511ade6a196ec0:goal-1/tasks.md`로 연결한다. 원문 SHA-256은 `7a4402ea2db19b00d974234cd3e460c092816bb59d8ecb04920fc9e1ef9fbfec`이다. Task6와 모든 기존 미완료 섹션은 byte 단위로 보존하며 원자료·실패·Git 이력을 삭제하지 않는다.

Task24a2 tracking compaction preserves the three completed Task24/checkpoint24/24a1 records in Git `db251a041aabc424f1f4ff0ac6a9d5b0e4f2a93b:goal-1/tasks.md` (SHA-256 `568a4e96efcbb1dc0b31a3c12f0d52b602e0ae22457f65ff608d8374576023c1`). Read the same section title there for full code, actual runtime, failures, AAR and commit details. Task6 and every incomplete section stay byte-identical; original artifacts and Git history remain. This is maintenance in the same approved tracking file, not a new context copy or goal initialization.

Task24a2 재개 중 완료24a2a의 상세 prose만 고정 Git `7255551b4d6dea14a755848f5a0bd7c2b4646a67:goal-1/tasks.md`로 연결한다. 원문 SHA-256은 `74566bdfe4243fd0a9dfa0cd8c0f317f404059c621ab6384effc6506db2c2742`다. 현재24a2·Task6와 모든 후속 미완료 요구는 그대로이며 원자료/실패/Git 이력을 삭제하지 않는다.

Task25a compacts completed historical plan prose against fixed Git `e8fa1718e7d16d3f18cc014fb349a480dbf643b9:goal-1/plan.md`, SHA-256 `f4a779f8ccda039690f74c257e9d120a3fe556e8a6a6a1f88f9099cfbd4b8d58`. Current authority, Task6 requirements and every incomplete task/checkpoint remain; current24a3/25a/writer-first choices stay verbatim. No source/raw/failure history or requirement is deleted.

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
- Work/evidence/risk/AAR/next/commit: db251a041aabc424f1f4ff0ac6a9d5b0e4f2a93b:goal-1/tasks.md; SHA-256 568a4e96efcbb1dc0b31a3c12f0d52b602e0ae22457f65ff608d8374576023c1.

## Checkpoint after Task 24

- Status: complete (source/contract/evidence integrity audit only; no new implementation or performance samples)
- Work/evidence/risk/AAR/next/commit: db251a041aabc424f1f4ff0ac6a9d5b0e4f2a93b:goal-1/tasks.md; SHA-256 568a4e96efcbb1dc0b31a3c12f0d52b602e0ae22457f65ff608d8374576023c1.

## Task 24a1 — Export semantic seeds and continue suffix replay

- Status: complete (isolated in-memory semantic transfer and local verification only; parent24a and the whole goal remain incomplete)
- Work/evidence/risk/AAR/next/commit: db251a041aabc424f1f4ff0ac6a9d5b0e4f2a93b:goal-1/tasks.md; SHA-256 568a4e96efcbb1dc0b31a3c12f0d52b602e0ae22457f65ff608d8374576023c1.

## Task 24a2a — Own anchored canonical validation views

- Status: complete (isolated owning view implementation/local validation only; parent24a2 and whole goal incomplete)
- Work/evidence/risk/AAR/next/commit: 검증·실패·독립 검토·자원·한계·다음 행동의 원문은 고정 Git `7255551b4d6dea14a755848f5a0bd7c2b4646a67:goal-1/tasks.md`의 같은 제목에 보존한다. 새15와 기존95가 통과했고 comment-only c4 재검증15는 coverage에 더하지 않는다. final-manifest-addendum SHA `967593483e43c1557f220ab5d857b959c4d127ecaa5b2a9bd0277f2f36864c31`와 task24a2a-postcommit.json이 source·실패/원자료·사용자 identity·15파일 범위·clean을 연결한다. 이는 국소 owning view 수리의 완료이며 pruning·실제 공간 회수·MVCC·writer·정리/복구 포함 COW/WAL 총비용·제품 완료가 아니다.

## Task 24a2 — Retain semantic closure and duplicate identity history

- Status: complete (local semantic closure/identity/suffix implementation and verified error repairs only; whole goal active)
- Work/evidence/risk/AAR/next/commit: 완료 상세는 고정 Git `d871adf67fc5a73fa6c9eb9cb4168931c26eda86:goal-1/tasks.md`의 같은 제목에 보존한다. 원문 SHA-256 `289d644775ae0ae639d7afbefcdb500fe9e6a46a50a66e2bc082b01770bb3761`. source와171사례·수리전후raw·원88405산출물 보존은 final-verification/preservation 및 addendum b4790b78e96bbf1105cacb333d6fe208b6362fce8797d391be8d7621363ef698, task24a2-postcommit.json을 따른다. 실제reuse/writer/총비용/제품은 계속 미완료이며 다음R1/R2를 진행한다.

## Additional checkpoint after Task 24a2

- Status: complete (국소 소스·검증·보존 감사이며 전체 목표는 미완료)
- Work performed: d871adf6의39파일 scope·사용자 identity·부모7255551b·clean을 확인했다. 원요구/Task6와 이후24a3–39, 실제writer·공정성·정리/복구 포함COW/WAL비교·제품 요구를 대조했고 누락은없다. native171의 최종검토와 주석37번역단위의 동일전처리 증거를 재사용했다. 추가 기능/검사 반복은없다.
- Verification evidence: task24a2-postcommit.json, final-verification/preservation와 final-manifest-addendum SHA b4790b78e96bbf1105cacb333d6fe208b6362fce8797d391be8d7621363ef698. 보호main/다른작업본과 원자료/실패를 보존했다. stale 이름/경로·사용하지 않는 raw seed API 후보는 이미 R1/R2의 독립 실행에 남는다.
- Remaining risk: seeded physical redo·내구bootstrap·reader lifetime·실제boundedreuse·MVCC/잠금/writer/총비용과 제품 통합은 미완료다. 현재후보의 국소통과를 제품품질/성능판정으로 확대하지않는다.
- AAR: 동작에 영향 없는 변경은 관련 입력 동일성으로 검증을 재사용하며, 의미가 다른 직접클라이언트 오류는 실제 전후 시험을 요구한다.
- Next step: 최신 사용자 우선순위 R1 이름 통일, R2 불필요 레거시 정리 후 기존 첫 미완료 요구로 계속한다.
- Commit record: subject `docs(repo): 복구 의미 이력 체크포인트 기록 갱신`; marker `Goal-mode boundary: goal-1 checkpoint after task 24a2: complete`. 이 tracking-only commit에서 tasks.md만 변경한다.

## Task R1 — Unify repository, local paths and distribution as StreamLite

- Status: complete (local namespace/format/build/install validation and repository/path rename; no publication, independent writers or total-cost result)
- Work/evidence/risk/AAR/next/commit: full completed record preserved at Git `191ccc95e1ef5e2859c3019db4057dc74b381857:goal-1/tasks.md` under this same title, SHA-256 eed6a31aacbfbd3d5930d0a0b94f15fb9606b567b53094b042d60e74aa8d4332. Actual task commit `9538da7ea7ff4a0fbda195828834c4b4fb56bb67` retains the exact validated scope. Naming/install/legacy removal is not writer/totalcost/product completion. Original raw, failure specimens, restoration incident and validation limits remain in the referenced record/artifacts.

## Task R2 — Remove unused legacy paths and redundant gates

- Status: complete (unused raw seed path deletion and affected local validation; no new runtime feature or whole-goal completion)
- Work/evidence/risk/AAR/next/commit: full completed record preserved at Git `191ccc95e1ef5e2859c3019db4057dc74b381857:goal-1/tasks.md` under this same title, SHA-256 eed6a31aacbfbd3d5930d0a0b94f15fb9606b567b53094b042d60e74aa8d4332. Actual task commit `191ccc95e1ef5e2859c3019db4057dc74b381857` retains the exact validated scope. Naming/install/legacy removal is not writer/totalcost/product completion. Original raw, failure specimens, restoration incident and validation limits remain in the referenced record/artifacts.

## Task 24a3a — Capture physical page authority and execute seeded redo

- Status: complete (isolated physical authority/seeded redo and causal callback/source repairs; whole goal remains active)
- Scope/work: canonical view owns exact page generation/previousPageLSN, allocation frontier, latest FPI and same-observation before-R/cut hash. B-only meaning and work-only bounded chronological R..B index reconstruct actual LIVE bytes; RESOLVED executes current cut, PREVIEW stays read-only. No raw seed wrapper or full DB image copy. R535536 is0.5107MiB, with positive R..B reads and zero denied-prefix reads;63LIVE/FREE/reallocation/expired-version bytes and source+next+WAL+work29,034,176B are checked as requested bytes, not RSS. Parent24a3b/24a and product remain incomplete. Issues: #263,#266.
- Actual repairs: independent static P1 callback-time old-cut overwrite and P2 early semantic CORRUPT persistent-reject bypass first fail in3 fixed native cases on frozen C1. Single Claude code3 repairs pre-write/final cut checks and source rejection. Independent review then finds a final physical-table release callback gap in canonical open/continue;2 additional fixed release cases both fail on frozen C3, then exact2 success-only post-settle cut checks repair them. Final five original failures become PASS with STALE/zero-output/no stale write or permanent POISONED as appropriate; healthy controls, source preservation and zero leaked bytes remain. Same fixed13 case bodies run by default.
- Verification: task24a3a-c2-before.json/c5-before.json preserve actual exit1 failures, binaries and source hashes; C1 source patch SHA8fba14bee8e886a48e5b05eccb3e0f1743b789e4f365ca6e972194d3f012711b preserves new source. task24a3a-c6-final-registration/freeze/final.json and final-verification.json record fresh -Werror/fatalUBSan, all89 actual invocations PASS/RC0/matchedtrue/stderr0, fixed13 bytes unchanged and286 inputs unchanged. Final view6/closure3/callback15/buffer17/publish18/WAL read2/checkpoint/state/files/basic gates cover changed paths. Source-causal C3 view14/closure11/boundary6/transport9/redo/identity/recovery evidence is reused where unchanged. Independent repair/execution review finds P1=0/P2=0 and confirms exact source/report hashes; reviewer did not independently execute binaries.
- Validation limits: initial broad recovery-wal default60s times out. The previously established900s runner guard completes the identical binary/assertions in60.843s; native90s guard is unchanged. Original timeout remains. Broad publisher segments is deliberately control-stopped SIGTERM15 after normal/source/read/counter/basic/selection/empty/resolved/sparse/boundary success; segments/faults and the20target batch are not claimed complete. task24a3a-c3-validation-scope-correction.json records narrowing to final source-causal gates rather than repeating the historical full campaign. No failed assertion is relaxed or counted as success.
- Source/quality: code1 authored source and was control-stopped143 for verification, not CLI success. Code2–6 report process0, stream success/is_error=false and no permission denials. Actual source/test code stays one local Claude writer; parent runs/reviews/records/commits. Comment review accepts2 redundant new header-comment removals, exact byte reversal proves comment-only change,15 ownership/failure contracts remain; no source authorship traces.
- Seal/preservation: task24a3a-final-manifest.json SHA e8a73af645b1b9cfb946539045e5d32496a85d4cdf2385d10667d82630db1874 binds20source,12evidence references and4708 new artifacts. final-preservation.json checks101278 original+additional artifacts with0 mismatches, protected tracked0, original ledger prefix and main/origin6bb8b2e8/four other worktree heads unchanged. New allocated sum1,136,414,720B exceeds the internal1GiB soft target; free31,280,541,696B exceeds8GiB hard reserve. Raw/failure artifacts are retained; soft excess is not hidden by deleting evidence.
- Remaining risk: first all-LIVE FPI retention can pin R on cold pages; actual clean-baseline ownership, seeded buffer/NO-STEAL/checkpoint transition, durable seed/bootstrap, reader/reclaim/reuse and full MVCC/locks/SQL writer/totalcost/product remain unfinished. No whole-campaign, power-loss, whole-app memory, performance winner or product quality claim. Initial capture/fullprefix hashing and reconstruction/index/maintenance costs stay in the future matched comparison.
- AAR: a small passing suite omitted callback boundaries and early error persistence. Add actual failing controls before repair and recheck the last callback before exposing success. Broad default historical campaigns can obscure the changed-path evidence; freeze inputs, record scope before final checks and retain every interrupted/failed result.
- Next step: separate tracking-only checkpoint after24a3a, then writer-first25/26/27 under the latest user priority. Resume retained24a3b/4–6 after27; all real cleanup/recovery/GC gates remain before30–32. Do not stop the active goal at this successful commit.
- Commit record: containing task-boundary commit uses subject `feat(repo): 물리 복구 권위와 제한 범위 재실행 추가` and marker `Goal-mode boundary: goal-1 task 24a3a: Capture physical page authority and execute seeded redo`. Verify parent191ccc95, configured identity, owned21-file scope, marker and clean status after commit.

## Additional checkpoint after Task 24a3a

- Status: complete (source/evidence/requirements audit only; whole goal remains active)
- Work performed: verified818c6374 parent191ccc95, owned21files, configured user identity/marker and clean worktree. Reused final89 source-frozen Werror/fatalUBSan invocations and independent P1=0/P2=0 verdict; no new runtime feature or redundant execution. R1/R2 full records remain in191ccc95 with exact tracking SHA; earlier source/raw/failure history remains.
- Verification evidence: task24a3a-postcommit.json and final-manifest SHA e8a73af645b1b9cfb946539045e5d32496a85d4cdf2385d10667d82630db1874 bind current20source/12evidence/4708new artifacts. Preservation101278 original+additional mismatch0, protected tracked0, original ledger prefix and main/origin6bb8/four other worktrees unchanged. The initial60s timeout and controlled optional segment SIGTERM15 remain explicit, never passes. Requested allocation1,136,414,720B exceeds1GiB soft; free31,280,541,696B exceeds8GiB hard.
- Scope/data/security review: B semantic versus R physical authority, actual body restoration, zero output/partial effects, resource/source lifetimes and fail-closed poisoning agree with code and docs. No Vault/other-owner write, upload/push/merge/live conversion or secret/auth changes. First all-LIVE FPI retention and fullprefix costs are implementation limits, not WAL structural cost or measured superiority.
- Remaining risk: seeded buffer/checkpoint transition, durable seed/bootstrap, reader/reclaim/reuse, complete MVCC/GC/early locks, actual SQL/ACK/multiprocess writers, matched COW/totalcost and product/stream integration remain incomplete. No full historical campaign or whole-product quality claim.
- AAR: use the final callback boundary before exposing success, preserve source semantic CORRUPT and failures, and bind reused tests to actual unchanged paths rather than repeat every historical campaign.
- Next step: writer-first25 begins in this same session, split the actual integrated native overlap slice as25a before the rest of25. Preserve26/27/after27 and return to24a3b/4–6 before28–32; cleanup/GC/checkpoint/recovery cannot be omitted from total cost.
- Commit record: containing tracking-only commit uses subject `docs(repo): 물리 복구 검증 체크포인트 기록 갱신` and marker `Goal-mode boundary: goal-1 checkpoint after task 24a3a: complete`; tasks.md is the only changed file. Verify parent818c6374 and clean status after commit.

## Writer-first execution priority — 2026-10-27

사용자는 현재 수리를 완료하고 실제 다중 writer와 COW/WAL 총비용 비교를 빨리 수행하라고 다시 지시했다.24a3a 수리·검증·commit과 별도 checkpoint 뒤25 MVCC,26 조기 소유/충돌,검토,27 실제 같은 process writer 중첩을 먼저 구현한다. 원래25–27 항목과 검토를 이 위치로 옮겼으며 삭제·완료 처리는 하지 않았다. 기존 전체 거래 mutex를 푸는 것만으로 writer라고 보고하지 않는다. 전체 WAL 보존·명시 용량 거절의 격리된 사례에서는 cold seed/실제 회수가 동시 거래 정확성의 논리적 선행 조건이 아니므로 이 순서를 선택한다. 실제 append/group 짧은 임계구간과 거래 수명 직렬화는 구별한다.

27/checkpoint 뒤24a3b/24a3/24a4–6/24a를 재개하고,28 교차process·29 matchedCOW·30 양쪽 안전성·31 사전계획·32 실제 총비용 순서를 유지한다. 정리·회수·재사용·GC·checkpoint·복구를 빼거나 cap 도달 전 결과만으로 steady 비용을 판정하지 않는다. 실제 SQL/ACK 연결·same-leaf 갱신 손실·A가 열린 채 B commit/진전·실제 실패/재시작을 각 완료 기준대로 검증한다. 이 순서 조정 당시에는 writer 구현이나 비용 결과가 없었다. 현재의 bounded native 증거는25a에 기록하며 전체 SQL/process writer와 비용은 미완료다. 읽기 전용 writer_path_how/candidate_writer_how의 source 조사와 transaction.c writer_active, database work.active/file lock, session db->run, candidate_buffer 배타 caller 계약을 대조한 순서 조정이다.

## Task 25a — Integrate native overlapping MVCC transactions and durable outcomes

- Status: complete (isolated native fixed-page writer/result integration and verified repairs only; whole goal active)
- Scope/work: single owner retains actual recovery/WAL/control/data handles, shared latest-page immutable slots, per-operation io_gate and registered fixed snapshots. Real ID_RESERVE with both root images/force, ADMIT/BEGIN/RESULT_CHUNK/COMMIT/ABORT, nonempty affected_rows body and recovery_recover metadata/checkpoint/control publication are runtime code. Existing LIVE pages, small declared point plans and retained wholeWAL are explicit limits; no fixture authority substitutes for runtime. Source how/two candidates/judge selected shared pages over dedicated executor queue18/16 design points, not performance. Issues #263,#266.
- Actual overlap: two pthreads apply A70 and B71 to the same PageRef; B durably commits CSN7 while A stays ACTIVE, then A aborts logically. Old reader/read-own, bothcommit, B-response process termination and new-owner full recovery preserve old A/new B and B actual24B response. No transaction-lifetime gate or stale page undo. This is native storage concurrency, not SQL/ACK27 or cross-process28.
- Actual repairs: frozen C1/C4 before binaries reproduce10 failures: exhausted ID0 after64 valid ABORTs, nearMAX group-credit wrap on a valid reopened USER group, six reserve/commit/abort final-force reject/append cases, UNKNOWN success-body leakage and last-BEGIN pwrite rejection returning a live handle. Claude binds intended records/real LSN before I/O in the existing pool cut, verifies terminal force, fences pool/native, refuses exhausted counters before effects, separates resident group count from monotonic IDs and publishes body only after success. Last append access guard is exactly one extra check; old source reconstructed by exact reversal matches C4 hash.
- Calibration/quality: three CSN fixtures violate visible_csn<=transaction_highwater before runtime; independent CSN P1 was retracted using valid-ID capacity proof, without an unnecessary runtime guard. Initial noevent txn0 group is invalid; corrected complete USERgroup/ABORT opens OK before the real capacity failure. Preserve both failed calibrations. Padding bound adds next-segment64B conservatively; no integrated shortage was proved. Native CHECK/EXPECT accounting is atomic. No-comments removes redundant descriptions; a claimed newline-preserving edit actually deleted lines, parent detects it and Claude restores exact blanks before compiler token equivalence. Public contracts remain.
- Verification: task25a-c5-before/after(-freeze).json and c3-before/c3b-before preserve actual failures, source and binaries. Final C5 fresh Werror/fatalUBSan native9+fixedboundary12 all21 PASS/RC0/matchedtrue/stderr0. C4 buffer17/page1 add18 reused actual executions with independently checked43/6 linked objects and81/11 source/header dependencies; Make differs only by new native selector. task25a-c5-reused-c4.json and c6-comment-equivalence.json prove final source39-case equivalence, unchanged case bodies/lines and exact compiler-preprocessed tokens. final-verification.json binds all raw reports/current296 inputs; final-review.json records independent source/evidence A/B P1=0/P2=0, not independent execution.
- Execution limits: TSAN build succeeds, but overlap and no-DB invalid selector both SIGSEGV before output; no race-sanitizer pass or DB race conclusion. No physical power loss, whole-app memory/fairness/speed superiority or product quality claim. Fullprefix scan/hash/forced validation and repeated page decoding are actual implementation costs retained for later matched measurement.
- Preservation/seal: final-preservation-postcheck.json rehashes all105993 original artifacts/22,306,897,715B with0 mismatches, protected tracked0, original ledger prefix/main/origin6bb8/four other worktrees unchanged. final-manifest.json binds final owned sources, evidence and all newly generated artifacts; no raw/failure deletion. Resource totals and soft1GiB/hard8GiB-free checks are in that seal. Configured user identity is retained. All code/test writers complete process0, stream success/is_error=false/no denials; foreground nonpersistent worker uses5 rounds in this chat and closes before commit.
- Remaining risk: first slice keeps caller-declared fixed pages and bounded manifests. Persisted root/index, actual structural allocation/split/crosspage/overflow/GC remainder25, full key/range/wait fairness26, real SQL/ACK/E4 27, seeded reuse24a, crossprocess28, matchedCOW29, fullsafe30 and totalcost31/32/product/streams33–39 stay incomplete. No request to publish/deploy or write Vault is inferred.
- AAR: passing small suites and model review scores do not close source boundaries. Calibrate valid initial states before calling a failure a runtime defect, test the last callback before exposing success, separate ID values from resident counts, inspect raw bytes rather than writer summaries, and reuse unaffected executions only with concrete dependency/token equivalence.
- Next step: continue in this same session to the first remaining25 vertical slice, split as25b persisted index/real allocation using prepared source how and independent A15/B13 judgment. Preserve generic SQL/scalar/history/GC and later lifecycle gates; all actual maintenance must close before30–32. Do not stop at this task commit.
- Commit record: containing task-boundary commit uses subject `feat(repo): 중첩 거래와 내구 결과 저장 추가` and marker `Goal-mode boundary: goal-1 task 25a: Integrate native overlapping MVCC transactions and durable outcomes`. Owned18-file scope includes existing design/approved goal tracking; verify parent e8fa1718, user identity, marker and clean status after commit.

## Task 25 — Implement MVCC visibility and abort recovery

- Status: incomplete
- Scope: Implement row/transaction version visibility, fixed snapshots, logical abort, index/structural recovery and old-version GC, integrated with candidate WAL/buffering. Issues: #263, #266.
- Work performed: first integrated native slice is split as25a above; parent stays incomplete until structural/index/allocation abort, old-version GC and full visibility/differential requirements are verified. The completed native25a slice is not full25 completion.
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
- Work performed: not started. 최신 사용자 우선순위로25/26 뒤 실제 writer27 전에 수행하는 검토다. 아직 미완료인24a는 writer27의 국소 정확성 뒤 재개하며 after27도 보존한다.
- Verification evidence: required — 원래 scope·실제 bounded buffering/seeded reuse·MVCC·조기 조정의 구현과 실패/자원·기록을 대조하는 tracking-only 검토.
- Remaining risk: 실제 writer27을 시작하기 전에 새 결함은 독립 수리 과업으로 남긴다.
- Next step:25/26이 끝나면 별도 tracking commit으로 검토하고, 연속 실행 지시에 따라27로 진행한다. 실제 정리·회수24a를 통과하기 전 총비용 측정은 금지한다.
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

## Task 24a3b — Initialize seeded buffering and connect terminal force authority

- Status: incomplete
- Scope: actual seeded buffer directory/frame/FPI/recLSN/contributor init and admit/flush/proof use canonical page authority and B-only meaning. Retired structural terminal authority requires exact-started and resident absence plus actual B force; LIVE versions keep real transaction/CSN. Explicit checkpoint/anchor transition must replace whole-prefix proof before parent24a3 completion. Issues: #263,#266.
- Work performed: not started; source how and architecture explain current fullscan and checkpoint-suffix UNSUPPORTED gaps.
- Verification evidence: required — all LIVE/FREE directory and dirty frame state, frame-pressure LIMIT, retired structure B force-before-write versus unfinished/missing-version refusal, no-read-before-R through open/pin/admit/flush/checkpoint proof, exact recLSN/redirty/FPI behavior, actual source/new aggregate budget and resource lifetime.
- Remaining risk: 24a3a all-LIVE FPI retention may pin R on cold pages. Actual durable clean-baseline ownership plus write-before-overwrite FPI must be assessed in24a3b/4/6 under the same COW/WAL fault model; whole cold-data rewrite cannot be hidden or treated as inherent WAL cost. open_seeded alone is insufficient if later operations call fullscan or proof silently falls back. No durable seed/bootstrap/reader/reclaim/MVCC/writer/product activation in this unit.
- Next step: implement only after24a3a and its checkpoint, preserving full parent scope.
- Commit status: no task commit attempted.

## Task 24a3 — Seed page authority and physical redo state

- Status: incomplete
- Scope: LIVE/FREE page의 generation/pageLSN·allocation 마지막 전이·검증된 clean baseline·필요 dirty/FPI를 streaming seed와 seeded redo/buffer 초기 상태에 연결한다. semantic B와 physical R을 분리한다. Issues: #263, #266.
- Work performed: actual source how and two architectures plus independent judgment complete. The scope is split into24a3a physical authority/seededredo and24a3b actualbuffer/force/checkpoint proof; this parent stays incomplete until both verified. Source and design are not implementation or measurement.
- Verification evidence: required — suffix에 안 나오는 기존 page, expired request의 LIVE version, FREE→재할당, torn page/delta/FPI, 실제 data/result bytes·ref/LSN/상태 차등, prefix-read 차단, OOM·전체 예산. task24a2-structural-closure-source-note.json의dirty/구조기여자NO-STEAL 근거도명시연결한다. 검증view의exact started집합과resident부재가종결을증명하는경우에도B force와수명·page권위를실제buffer에대조하며임의COMMIT/CSN을만들지않는다.
- Remaining risk: completing this unit does not complete parent Task24a or actual writers/fullcost/product.
- Next step: verify and commit this unit, then continue the next incomplete unit in this session under the latest user override.
- Commit status: no task commit attempted.

## Checkpoint after Task 24a3

- Status: incomplete
- Work performed: not started; tracking-only review after three new units.
- Verification evidence: required — original scope, source/runtime evidence, safety/data/resources/compatibility and remaining actualreuse/fullgoal gates.
- Remaining risk: insert required repair before dependent work; do not mark original24a/fullgoal complete from checkpoint.
- Next step: separate tracking commit then continue the next unit under latest user override.
- Commit status: no checkpoint commit attempted.

## Task 24a4 — Publish versioned recovery seeds and select both slots

- Status: incomplete
- Scope: fresh opt-in 후보 형식에서 stable lock·비선택 구조 판독·seed/suffix 검증·두 slot 선택 bootstrap과 내구 seed 게시를 구현한다. 기존 기본 backend의 동작과 현재 StreamLite 형식4 경계를 보존한다. 새 후보 형식에는 옛형식 reader·별칭을 추가하지 않는다. Issues: #263, #266.
- Work performed: not started.
- Verification evidence: required — 두 slot 독립 복구/fallback·stale seed/epoch·mixed format 거절·게시 전후 중단/IO/directory sync·headroom·불확정 poison, seed checksum만으로 authority 승격 금지.
- Remaining risk: completing this unit does not complete parent Task24a or actual writers/fullcost/product.
- Next step: verify and commit this unit, then continue the next incomplete unit in this session under the latest user override.
- Commit status: no task commit attempted.

## Task 24a5 — Protect reader and consumer view lifetimes

- Status: incomplete
- Scope: 실제 읽기 객체의 snapshot/view 등록·종료와 version/result 의존성, buffer/witness quiescence를 회수 authority에 연결한다. 외부 생성 불가 permit과 같은 owner를 사용한다. Issues: #263, #266.
- Work performed: not started.
- Verification evidence: required — 장기 reader 실제 읽기·압력·해제 후 진행, 영구 pin 무단 reset 거절·stale view/owner, frame lease와 snapshot reader 구별. 제품 MVCC 활성화 완료는 아니다.
- Remaining risk: completing this unit does not complete parent Task24a or actual writers/fullcost/product.
- Next step: verify and commit this unit, then continue the next incomplete unit in this session under the latest user override.
- Commit status: no task commit attempted.

## Task 24a6 — Reclaim and reuse bounded WAL capacity

- Status: incomplete
- Scope: 증명된 양 slot·reader·consumer 의존성 밖 whole segment만 내구 retirement 뒤 회수한다. 절대 LSN과 bounded live file mapping을 분리하고 실제 용량 반환·재사용을 연결한다. Issues: #263, #266.
- Work performed: not started.
- Verification evidence: required — 작은 physical cap을 절대 LSN이 여러 차례 넘는 append/restart cycle, 결과/본문 보존·LSN ABA 거절·파일/fd/메모리/공간 상한, OOM/disk-full/delete/directory sync 중단·멱등 복구. 회수거절/계산만으로 완료 금지.
- Remaining risk: completing this unit does not complete parent Task24a or actual writers/fullcost/product.
- Next step: verify and commit this unit, then continue the next incomplete unit in this session under the latest user override.
- Commit status: no task commit attempted.

## Checkpoint after Task 24a6

- Status: incomplete
- Work performed: not started; tracking-only review after three new units.
- Verification evidence: required — original scope, source/runtime evidence, safety/data/resources/compatibility and remaining actualreuse/fullgoal gates.
- Remaining risk: insert required repair before dependent work; do not mark original24a/fullgoal complete from checkpoint.
- Next step: separate tracking commit then continue the next unit under latest user override.
- Commit status: no checkpoint commit attempted.

## Task 24a — Implement recovery seeds and actual bounded WAL reuse

- Status: incomplete
- Scope: Preserve the original Task24 log reuse obligation. Implement durable base/epoch transport and sufficient transaction/admission/result/page-generation/allocation recovery seeds, protect both control slots and reader/version dependencies, then reclaim and actually reuse bounded WAL space. Issues: #263, #266.
- Work performed: clean29479895와 Task24/checkpoint 증거를 확인하고 how·두 설계·독립 심사로 B의 증가 logical filename/bounded live mapping을 선택했다. baseline82548과 보충882의 합83430산출물·9135tracked·refs/5작업본·ledger prefix를 보존 기준으로 쓴다. Task24a1–6으로 분해했고 첫 메모리 의미 상태 단위의 코드·수리·실행은 위24a1에 기록한다. segment0 lock inode, 절대 segment-index 용량 상한, pin 수명과 충분한 page/allocation/redo seed·두 slot/reader 의존성은 실제 회수 전에 해소해야 한다. 원래 실제 공간 재사용 의무는 미완료다.
- Verification evidence: required — Same committed data/results after pruned-history restart, torn page and retained full-image recovery, both-slot fallback, active/long-reader pressure, OOM/disk-full/delete/directory-sync interruptions, repeated cycles that recover actual capacity without LSN ABA or lost required history.
- Remaining risk: Current transport starts at segment0; existing checkpoint tuples and whole-prefix witnesses cannot safely open pruned history. No required segment may be deleted before complete seed/selection proof.
- Next step:24a2의 closure/역사 ID·nonce 보존부터 같은 세션에서 순서대로 진행한다. 각 단위는 별도 검증·commit하며 원래24a는 실제 회수/반복 재사용·복구까지 통과하기 전 미완료다. 일반 segment suffix cursor는24a4/6의 transport 전제에 포함한다. 코드 writer의 Bash를 제외하고 parent가 실행한다. 전역 설정·현재DB/default·Vault는 변경하지 않는다.
- Commit status: no task commit attempted.

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
- Scope: Integrate the selected candidate into a reviewable product path after Task 32. Use the explicitly approved StreamLite namespace/single new format and preserve existing COW/default behavior until selected integration; reject old formats without mutation. No old namespace/format reader or live conversion. Issues: #263.
- Work performed: not started.
- Verification evidence: required — Full supported SQL/ACK/transaction/C/Python regression, supported StreamLite-file reopen/backup-restore and foreign-format rejection/nonmutation tests on copies, independent writer and failure evidence. Verify the selected G2 resolution path through product/native/Python entry points, result-body lifetime and restart/expiry/unsupported behavior; same-open ring success is not restart lookup evidence.
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
- Scope: Run integrated SQL/ACK/stream/native/Python, memory/lifecycle, concurrency/recovery, supported StreamLite-file behavior and foreign-format rejection on the selected product; reconcile existing design/report/issues with actual evidence. Issues: #257, #260, #238, #239, #261, #262, #263, #264, #265, #266, #267, #268, #269, #158.
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
