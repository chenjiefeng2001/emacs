# ENCA SESSION HANDOFF — 2026-08-25

> 供下个 session 使用。当前 HEAD = `329b2453ac7`,工作树干净,
> 全部结论有 tag 与原始数据支撑。请先读完本文再动任何代码。

---

## 0. 项目身份(已三次重定位,勿走回头路)

```text
原名:Next-Gen Emacs Core / ENCA runtime 优化   ← 已终结
现名:ENCA Real Completion / Semantic Latency
```

项目性质已从"架构优化"转型为**语义延迟实验平台**。
最高工程政策(ARCHITECTURE.md §26):

> **No architecture change without millisecond-scale user-path
> attribution.**

内部指标(queue depth、MB/s、utilization)永远不构成改动理由。

---

## 1. 测量基线(全部实测,勿凭记忆引用)

| 路径段 | 延迟 | 状态 |
|---|---|---|
| ENCA capture/snapshot/scheduler/wakeup/transport 合计 | **<0.1ms** | FROZEN |
| completion UI(popup 安装+redisplay,tty)| ~0.3–2ms | MEASURED |
| LSP backend(clangd 19.1.0)candidate-ready | **78–92ms** | DOMINANT,不受我们控制 |

三路基准(P0,tag `enca-p0-baseline-closure`):
Vanilla ≡ ENCA-disabled ≡ ENCA-enabled(所有 cell 比值 0.89–1.09,
纯噪声带;disabled 构建与 vanilla 二进制字节同尺寸 2992936)。
→ **ENCA 自身零回归,已排除嫌疑。**

---

## 2. 阶段状态总表

| 阶段 | 结论 | 证据位置 |
|---|---|---|
| P0 三路基准 | 无回归 | REPORT §24 / results/p0_*.csv |
| P1-P3 core | FROZEN | ARCHITECTURE.md §25/§26 |
| EVS-1 vertical slice | CLOSED | tag enca-evs1-vertical-slice |
| EVS-2 incremental capture | **NO-GO**(capture 快 19000×但 E2E 不动)| REPORT §18 |
| EVS-3 wakeup | GO,20ms 地板消除(E1 p50 19.5ms→0.0008ms)| REPORT §19 |
| EVS-4.0-4.2 synthetic slice + storm | GO | REPORT §20 |
| EVS-4.3 real LSP transport | transport 占比 ~0.01% → simd-json/共享内存永久否证 | REPORT §21 |
| EVS-4.4 UI attribution | redisplay ~2ms,分支 B 不触发 → EVS-5 Render Snapshot 关闭 | REPORT §22 |
| EVS-5 backend attribution | D1 context 否证 / D2 无空间 / **D3 cache 提级** | results/evs5_backend_attribution.log |
| EVS-5.2 Stage A+B+C cache | false_hits=0,C8a=0.10µs,C8b=0.40µs | CACHE.md §7-8 |
| EVS-5.2.6 real UI | **HIT keypress→visible p50=0.58ms** / MISS 90.4ms | CACHE.md §8,REPORT §25 |
| **EVS-5.3 real typing (F1)** | retry 100% / growth avoided 34.4% / edit-interleaved 0%(by design)/ false_hits=0 | REPORT §26,results/evs53_real_typing.log |

---

## 3. 当前架构快照

```text
keypress
  └─ ENCA(<0.1ms:capture→snapshot→scheduler→wakeup)
       └─ Completion Engine
            ├─ Cache HIT(exact / Stage-B grow)<1ms → popup → visible
            └─ MISS → LSP(loopback 注入 90ms 或真 clangd)→ visible
```

关键组件位置:
- `src/enca/completion/cache.{h,c}` — 有界 LRU + prefix 字节存储 +
  `lookup_grow`(Stage-B)+ `on_edit` 保守失效 + lang_hash 绑定
- `src/enca/lsp/*` — session/transport/jsonrpc(手写最小 codec)+
  clangd spawn(exec_argv 支持)+ loopback shuffler + 可配 delay
- `src/enca-evs.c` — elisp 面:`enca-evs-complete PREFIX CURSOR`
  返回 (CANDIDATES ENGINE-MS SOURCE);另有 bump-revision/stats/
  wait-committed 等
- 测试:`test_ctcache.c`(Stage C 全矩阵)、`test_evs5.c`(归因)、
  `test_dstate.c`(adopt/torture)

---

## 4. 冻结清单(重开需新实验指认毫秒级收益)

多线程 UI、redisplay.c 重构、并行 GC、allocator 替换、NUMA、
work stealing、shared-memory LSP、SIMD JSON、Rust 化 core、自定义
GUI renderer、Range Snapshot、tree-sitter、cross-revision reuse
(Stage D,见下)。

硬门禁:**false_hit = 0**,任何阶段不可豁免。

---

## 5. 下一步(按优先级)

### 候选 1 — F2/cross-revision(Stage D)
已被 F1 数据推迟:edit-interleaved 类 0% 是真实痛点,但安全复用
需要 unrelatedness proof rule(尚不存在)。启动前置条件:
- 真实用户数据显示 edit-after-completion 场景占比足以证明复杂度
- 先写契约修订(CACHE.md §2 扩展),再实现
- 绝对门禁不变:false_hit = 0

### 候选 2 — C13/C8c 完整版
把 evs53-typing.el 的引擎侧数字接到 evs52-ui 的 popup/redisplay,
给出 hit/miss/edit 三类各自的完整 keypress→visible 分布
(p50/p95/p99/p99.9)。工作量小(~1 天),是 Stage D 决策的输入。

### 候选 3 — 真实 LSP 替换 fake server
WSL 无 clangd;Windows 侧 clangd 19.1 可用但 emacs 构建在 WSL。
若要在 WSL 打通真 clangd:`apt install clangd` 或用 Windows clangd
经管道桥接(未验证)。fake_lsp.py 已支持 FAKE_DELAY_MS/FAKE_ITEMS。

### 明确不做
继续优化 runtime/scheduler/snapshot/transport;为命中率堆缓存
复杂度;任何 §26 禁令清单内项目。

---

## 6. 环境备忘(踩过的坑)

- **构建**:WSL Ubuntu ~/enca-p11(fork,--enable-enca,已打
  wake/completion/cache/lsp OBJ 补丁)。同步脚本:
  `bench/enca/evs23_sync_build.sh`(幂等,逐组守卫)。
- **运行 tty 基准**:`script -qec "./src/emacs -nw -Q -l <file>"`,
  TERM=xterm-256color。echo-area message 会互相覆盖 → 结果必须
  写文件(见各 harness 的 --emit/log 模式)。
- **PowerShell 陷阱**:不要对源码做跨行正则替换(`` `n `` 与
  `\n` 字面量事故各发生一次);多行编辑一律用 Edit 工具。
- **clangd JSON**:didChange 曾缺一个闭括号(c1ddc7d 修复);
  新增 writer 后务必离线 json.loads 校验(check_json.py 模式)。
- **子进程句柄**:probe/harness 必须调用 session_destroy,否则
  clangd 继承句柄导致父管道 EOF 永不到来(假死)。
- **原生套件**:34201 checks / 0 failures;ASan clean;
  TSan(WSL gcc)0 warnings——回归门槛不得低于此。
- **TSan 脚本**:`bench/enca/evs23_tsan.sh`(gcc -std=gnu2x,
  含全部模块)。

---

## 7. 关键文档索引

```text
bench/enca/evs2/EVS2.md              契约+closure(§10-§12)
bench/enca/evs4/COMPLETION.md        completion 契约+outcome
bench/enca/evs4/EVS43.md             LSP transport 归因契约+结果
bench/enca/evs4/UI_ATTRIBUTION.md    T0-T12 + R 阶梯契约
bench/enca/evs5/EVS5.md              5.0 契约(C1-C10 矩阵)+归因 outcome
bench/enca/evs5/CACHE.md             缓存契约 §1-7 + outcome §8-10
src/enca/snapshot/EVS2-DECISION.md   存储决策(含 NO-GO 附录)
src/enca/ARCHITECTURE.md             §25 状态冻结 / §26 Performance Freeze
bench/REPORT.md                      §14-§26 全部 closure 叙事
```

原始数据:`bench/results/*.csv/*.log`。

---

## 8. 一句话给下一个 session

> 所有内部路径都已证明无罪并被冻结;唯一真实的杠杆是把
> edit-interleaved 的 0% 复用率提上去且永不返回错误候选——
> 在拿到真实用户场景占比数据之前,克制就是最好的工程。
