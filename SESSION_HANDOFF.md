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
| **EVS-5.3.1 C13/C8c full** | 四类完整分布落地;hit 全路径 p50=4.6ms(redisplay 地板 ~4.5ms 主导,引擎 <0.15ms);§25 的 0.58ms 口径已修正 | CACHE.md §11,REPORT §27,results/evs531_ui_typing.log |
| **EVS-5.4 real LSP** | 真 clangd 18.1.3 打通 elisp 用户路径;引擎 source 序列与 loopback 逐 op 一致(32/32);GROWTH 精确复现 F1;false_hit=0 | REAL_LSP.md §6,REPORT §28,results/evs54_real_lsp.log |
| **EVS-5.5 edit trace** | 测量仪器落地:EditRelation/H0-H4 分类 + 引擎真值校验;复用上限按行为双峰(打字链 0% vs 无关区编辑 91%);H0m 键碎片发现;Stage-D 决策公式两端仍缺真实数据 | TRACE.md §6,REPORT §29,results/evs55_trace.log |
| **P0-EIPB Phase 1** | 项目主线升级为全编辑器基准;T1 核心域四构建(A/B/C/D)全量落地:ENCA 影响≈噪声带;**GC 强制暂停 max 190–320ms(vanilla 同样存在)= 首个被数据点名的真实交互停顿源** | EIPB.md,MATRIX.md,REPORT §30,results/eipb_t1.log |

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

### 主线 — P0-EIPB(自 2026-08-25 起,见 bench/eipb/EIPB.md)
全编辑器系统基准,所有后续性能决策的依据。纪律:禁综合分、
百分位强制、A/B/C/D 四构建、tty/GUI 分离、§26 归因门禁不变。
- Phase 1(T1 核心交互)✅(REPORT §30 / MATRIX.md);
- Phase 2+2.1(T2 尾延迟归因)✅(REPORT §31/§32);
- Phase 3.0–3.3(T3 用户路径覆盖)✅ Atlas 冻结 v1(REPORT §33-36);
- Phase 4.1(SOAK-30M)✅ 2026-08-26(REPORT §37 /
  bench/eipb/phase4/report/PHASE4_1.md):G2 RSS 斜率、G3 退出
  挂死(p32 未复现,tty 域清除)、G4 停顿行稳定 全过;G1 尾放大
  A/D/B 过、C 2.15 边际不过→学说 7 判 UNRESOLVABLE(四构建绝对
  尾 p99 收敛于 451–642ms 带,C 只是头部基线最低)。**新发现:
  插入打字中位数 30 分钟 ~4× 增长,全构建含禁用 B 共有 → 饱和
  vs 无界是 SOAK-2H 的核心问题**;
- Phase 4.2(SOAK-2H)**已在跑**:2026-08-26 12:12 发射,乱序
  C-D-A-B,零 harness 改动;原始数据 eipb_t42.log +
  eipb_t42_rss.csv(不覆盖 t41);预计 ~20:20 完成。下个 session
  只需:确认 `EIPB4_RUN_DONE` 与四块 teardown|status|clean →
  `python3 bench/eipb/phase4/eipb_t41_analyze.py <t42.log>
  <t42_rss.csv>` → 填 bench/eipb/phase4/report/PHASE4_2.md(骨架
  已建,读数纪律已冻结)→ 更新 MATRIX/EIPB/REPORT → 提交。核心
  问题只有一个:打字漂移饱和 vs 无界(analyzer 的 TYPING DRIFT
  CURVE 段直接给判定);顺带看 C 头位重跑下 G1 是否换位重现;
- Phase 5(T5)外部 IDE 同 trace 对比(最后做)。
**T1 已点名 GC 停顿(max ~190–320ms,vanilla 同在)= 第一个真实
交互停顿候选;任何针对它的改动仍需 §26 归因流程。**

### 候选 — F2/cross-revision(Stage D)
前置条件已部分兑现:EVS-5.5 轨迹仪器落地(TRACE.md),给出逐行为
复用上限——打字链 0%、参数生长 80%、无关区编辑 91%、混合合成 15.4%,
以及 H0m 键碎片这一低成本替代线索。**仍缺的两端**:真实用户
edit-after-completion 行为占比(决定 P(H1) 的实际权重)与本机真实
项目后端延迟(决定乘数)。决策公式 = P(H1)×backend_latency − 复杂度
成本;任一端补齐前维持关闭。启动顺序不变:先契约修订(CACHE.md §2
扩展,纳入 H0m 键归一化议题),再实现;false_hit=0 绝对门禁不变。

### 候选 2 — C13/C8c 完整版 ✅ 已完成(2026-08-25,EVS-5.3.1)
真实打字四类 × 真实 popup/redisplay 全分布已落地(CACHE.md §11 /
REPORT §27)。关键新事实:hit 类 keypress→visible p50=4.6ms,几乎
全是 tty redisplay 地板(引擎侧 <0.15ms);§25 的 0.58ms 是 buffer
未显示时的口径,引用需带修正说明。Stage-D 决策输入已就绪。

### 候选 3 — 真实 LSP 替换 fake server ✅ 已完成(2026-08-25,EVS-5.4)
WSL `apt install clangd` 即可(Ubuntu 18.1.3,免密 root)。enca-evs-start
已支持字符串 BACKEND 直 spawn 真 server;`enca-evs-lsp-sync` 推文档状态
(version==revision)。关键结论:后端替换对缓存层透明(source 序列逐 op
一致);合成小文档下真 miss 仅 ~5ms,但**真实项目仍以 §21 的 78–92ms 为
准**,后端主导性结论不变。顺带修复两个潜伏缺陷(帧边界丢失 + 缺原型),
详见 REAL_LSP.md §6.1 / REPORT §28.5。

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
- **WSL clangd/valgrind 已装**(apt,root 免密):clangd 18.1.3。
  B2 原生测试臂现会真跑,套件总时长变长属正常。
- **构建树单对象重编**:必须 `make -C src enca-evs.o`;在 ~/enca-p11
  根目录裸 `make src/enca-evs.o` 会走内建规则、缺 include 路径而失败
  (静默 `|| true` 会让人误读产物)。
- **新头文件纪律**:enca-evs.c 曾因缺 memory.h/jsonrpc.h 触发隐式
  int 声明(-Wimplicit-function-declaration 只警告不报错,-O2 下碰巧
  无害)。新增对 enca 内存/JSON API 的调用前先补显式 include,并
  `grep 'implicit declaration' build.log` 应为 0。

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
>
> (2026-08-25 补:EVS-5.3.1 已给出全分布;hit 类可见延迟已触到
> tty 绘画地板,缓存与引擎侧再无毫秒可榨。Stage-D 仍按 §5 候选 1
> 的前置条件保持关闭。)
>
> (2026-08-25 再补:EVS-5.4 用真 clangd 走通全路径并证明缓存语义
> 对后端替换完全透明;真实项目后端主导性不变。下一优先级仍是
> §5 候选 1 的前置条件——拿到真实用户 edit-after-completion 占比
> 数据之前,不做 Stage-D。)
>
> (2026-08-25 三补:EVS-5.5 把"等数据"变成了"有仪器地等"。复用
> 上限按行为双峰:无关区编辑 91% / 打字链 0%;另发现 H0m 键碎片
> 这一可能更便宜的替代方向。Stage-D 的 Go/No-Go 现在是一个乘法
> 公式,只差真实用户分布与本机真实项目延迟两个实测输入。)
>
> (2026-08-25 四补:主线升级为 P0-EIPB 全编辑器基准。Phase 1 已
> 用四构建数据证明 ENCA 在核心交互域≈零影响,并点名了第一个真正
> 的交互停顿源——GC 强制暂停 max ~190–320ms,vanilla 同样存在。
> 下一个 session 从 EIPB Phase 2(font-lock 链/文件 I/O/多窗口)
> 开始,别回头做 runtime。)
>
> (2026-08-26 五补:Phase 2/2.1/3.0–3.3 已收官,Atlas 冻结 v1;
> Phase 4.1 SOAK-30M 四构建落地——G1 尾放大 C 2.15 边际不过但
> 绝对尾收敛、按学说 7 判 UNRESOLVABLE;真正的新信号是**全构建
> 共有的打字中位数 ~4× 漂移**(含禁用 B),饱和 vs 无界交给
> SOAK-2H(`EIPB_SOAK_SECS=7200`,~8.5h,建议过夜)。p32 退出挂死
> 在 tty soak 下未复现。工具教训:pid 别用 %.4f 发射;TA 判读以
> 绝对 head/tail 带为准。别回头做 runtime,也别在 2H 之前谈归因。)
