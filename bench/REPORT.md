# enca 子项目改动报告(2026-08-22)

## 1. 背景与范围
本仓库为上游 GNU Emacs 的 fork(上游政策仅约束对上游的贡献)。`src/enca/`、`test/enca/`、`bench/` 是 fork 内的独立子项目,目前均为 **untracked** 状态。本次工作:① 核实最新构建的正确性;② 完成 baseline(基线)记录工具链。

## 2. 测试验证结果
- 旧的 `test/enca/rt_out.txt`(07:53 生成)显示 `runtime/commit-values`、`runtime/stale-drop` 失败、`runtime/ordering` 输出中断——但它**早于** 08:03–08:04 对 `runtime.c`/`test_runtime.c` 的修改和重建,属过期结果。
- 用当前二进制(`test/enca/enca_tests_gcc.exe`,gcc `-std=gnu11 -O1`)全量重跑:**12066 checks, 0 failures**,原三个问题套件全部通过(commit-values 7 checks、stale-drop 107 checks、ordering 503 checks)。
- 结论:08:03 的修复有效;此前的计数异常不再复现(成因未正式归因,见 §6-1)。

## 3. 基线部分改动明细
| 文件 | 改动 |
|---|---|
| `bench/baseline.ps1` | `configure-options`:环境变量 > 解析 `config.log` 中 `$ configure...` 行 > `not-configured`;`build-flags`:环境变量 > 经 `make -n gcc-check` 干跑提取真实编译命令(make→mingw32-make 回退) |
| `bench/baseline.sh` | 移植上述逻辑至 POSIX sh;新增 `compiler-c` 回退链 cc→gcc→clang→unknown(原先缺 `cc` 时该字段为空) |
| `baseline-20260822-071608.txt`(旧记录) | 两个显式待填占位符按实测值补齐:`not-configured` + 实际 gcc 编译命令 |
| `baseline-20260822-082628.txt`(新记录) | 更新后脚本生成的完整 Windows 基线记录,无占位符 |
| WSL 测试记录 ×2 | 验证 sh 脚本时产生、混入 Linux 环境信息,已删除 |

实际填入值示例:

```
configure-options: not-configured   (Emacs 本体未 configure)
build-flags: gcc -std=gnu11 -O1 -Wall -Wextra -D_WIN32_WINNT=0x0601
            -D_CRT_SECURE_NO_WARNINGS -I../../src -o enca_tests_gcc <sources per test/enca/Makefile>
```

## 4. 验证方法
- ps1:PowerShell 5.1 实跑,生成 082628 记录并逐字段核对;
- sh:WSL bash 实跑,确认无编译器/无 make 时回退行为正确(`compiler-c: unknown`、build-flags 兜底),验证后清理产物;
- 全仓 `<fill` 检索:剩余匹配仅为脚本内最后兜底字符串,记录文件中已无占位符。

## 5. 当前仓库状态(2026-08-22 收尾更新)
- `master` 领先 `origin/master` 12 个本地提交(未推送),按序:
  `add runtime foundation` → `add runtime test suite` → `add baseline benchmark tooling` → `enforce cancel source ownership` → `remove unused worker result` → `add cancellation lifetime race regression test` → `add sanitizer test targets` → `add runtime architecture contract` → `freeze P1 runtime foundation baseline` → `add linux/windows sanitizer ci` → `track ci workflows ignored by upstream dotdir rule` → 本报告;
- 注意:上游 `.gitignore` 的 `makefile`、`[0-9]*.txt`、`.*` 规则会吞掉子项目文件,已在 `test/enca/.gitignore`、`bench/.gitignore` 加否定,`.github/` 用 `git add -f` 纳入。

## 6. 遗留事项与建议
1. 若日后真正 configure Emacs,重跑 `baseline.ps1` 会自动记录 configure-options。
2. baseline 记录可按评审建议扩展 arch/sanitizer/runtime 配置等字段。

## 7. P1.10.5 Correctness Closure(2026-08-22)

按评审清单 [1]–[6] 执行,[7]–[11] 如实标注状态:

### 已证实缺陷并修复
- **UAF 假设 → 硬证据**:新增 `test/enca/test_cancel_race.c`。`cancel-race/borrow-unretained` 套件复刻 worker 裸借用 `gen_cancel` 的模式,在 clang 19 + ASan 下稳定复现:
  ```
  ERROR: AddressSanitizer: heap-use-after-free
    READ in enca_cancel_source_is_cancelled (cancel.h:31)
    freed by enca_cancel_source_release (cancel.c:44)
  ```
- **修复(retain-on-load)**:`generation_cancelled` 改为在 `state_lock` 内加载并 retain 自己的引用,用完即 release;与 `advance_generation` 的替换临界区互斥。契约文档落盘于 `src/enca/cancel/LIFETIME.md`(ownership 模型、发布协议、禁止模式、canary 说明)。
- **回归防线**:`cancel-race/borrow-unretained` 作为永久 canary(常规构建参与压力运行;ASan 构建默认跳过,`ENCA_TEST_FORCE_RACE_CANARY` 可强制包含以验证检测灵敏度);`cancel-race/retain-on-load` 锤击安全协议,必须保持干净。

### 验证结果
| 构建 | 结果 |
|---|---|
| gcc `-O1` 常规 | 12087 checks, 0 failures |
| clang ASan 全套 | 12073 checks, 0 failures(差值 = 被跳过的 canary 检查数) |

### 工具链事实
- Windows 上 `ASAN_OPTIONS=detect_leaks=1` 使 ASan 运行时直接致命退出(套件不执行):本平台无 LSan;
- 本机 clang 19 发行版的 UBSan 运行时缺内部符号(`__coe_win::*`),无法链接;Makefile 已提供 `ubsan` 目标供 Linux 使用。

### 门禁状态
- [x] CancelSource 生命周期复现 → 修复 → ASan 干净
- [x] Runtime Contract 落盘(`src/enca/ARCHITECTURE.md`,10 条不可违反 invariant,P1.10.7 启动)
- [x] 基线冻结:`bench/baseline/2026-08-22-runtime-foundation.txt`(P1 Runtime Foundation 基准,后续阶段另建新基准)
- [x] canary 跳过守卫泛化(ASan/TSan/UBSan → `ENCA_TEST_SANITIZER_RISKY`)
- [~] Linux CI 工作流已落盘并推送;**GitHub Actions 云端首跑待观察**(fork 需确认 Actions 已启用)
- [x] **WSL 真实 Linux 矩阵首跑(Ubuntu 24.04,gcc 13.3)——P1.10.6 本地验证通过**:

  | Job | 结果 |
  |---|---|
  | functional-debug(-O0 -Werror) | 12087 checks, 0 failures |
  | functional-opt(-O2 -Werror) | 12087 checks, 0 failures |
  | ASan + LSan | 12073 checks, 0 failures,**0 泄漏** |
  | UBSan(halt-on-error) | 12074 checks, 0 failures |
  | TSan | exit 0,**0 data-race 报告**,12074 checks |

- **首轮真实发现与处置**(均按 复现→根因→修复→回归 协议):
  1. POSIX 线程后端从未被编译:`LPCRITICAL_SECTION` 泄漏进 pthread 分支 ×6、缺 `ENCA_NS_PER_S` 包含 → `enca: fix posix thread backend build`;
  2. race harness 的 replacer 并发替换协议违背 LIFETIME 契约(覆盖未释放,LSan 捕获 16507 个泄漏源)→ 对齐整段持锁协议 `enca: align race harness with lifetime contract`;
  3. test_memory 自身丢弃分配结果(LSan 80B)+ join 断言假设 HANDLE 语义(gcc 报错)→ 已修;
  4. sanitizer 与负向探测测试的边界:panic 探针在任意 sanitizer 下跳过;GCC 无 UBSan 预定义宏,以 `-DENCA_TEST_UBSAN=1` 在 Makefile/workflow 构建入口显式声明。
- [ ] Shutdown/stale-result 参数化长跑(Nightly 级,CI 化后添加)
- 设计债备忘:header-tag 式所有权探测(`enca_mem_header.magic`)对外来指针的对齐读取属技术性 UB,Phase 2 Snapshot 所有权模型应引入注册表式追踪替代。

## 8. P1 Runtime Foundation 冻结(2026-08-22,云端 CI 首跑闭环后)

本节为 P1 的**权威收尾记录**:§7 中的 `[~]`/`[ ]` 门禁在本节落定。P1 架构工作到此停止,后续改动以本基准为对照点(tag: `enca-p1-runtime-foundation`)。

### 8.1 门禁表(最终)

| Gate                   | 状态       |
| ---------------------- | -------- |
| Local GCC functional   | PASS     |
| Local Clang ASan       | PASS     |
| Linux functional-debug | PASS     |
| Linux functional-opt   | PASS     |
| Linux ASan + LSan      | PASS     |
| Linux UBSan            | PASS     |
| Linux TSan             | PASS     |
| Windows MSYS2 GCC      | PASS     |
| Windows Clang ASan     | PASS     |
| Working tree           | CLEAN    |
| CI                     | PUSHED   |
| Shutdown long-run      | DEFERRED |
| Stale-result long-run  | DEFERRED |

注:**DEFERRED ≠ PASS**。两项长跑为 Nightly 级,尚未运行;自 P1.11 起以 Nightly 工作流并行补齐,不阻塞主线。

### 8.2 云端首跑发现并处置的两个问题(均非 ENCA 核心语义缺陷)

1. **Windows ASan「exit code 1」实为被测进程未能启动,而非测试失败**
   - 链条:LLVM 20 默认链接动态 ASan 运行时 → `clang_rt.asan_dynamic-x86_64.dll` 位于 `<LLVM>\lib\clang\20\lib\windows\`,不在加载器搜索路径 → `0xC0000135 (STATUS_DLL_NOT_FOUND)` → 进程未及 main 即死。
   - 取证:gdb 跟跑取到 thread exit code `3221225781`(即 0xC0000135);此前注解仅见 "exit code 1",属误导性表象。
   - 处置:构建后将资源目录与 LLVM bin 下全部 `clang_rt*.dll` 拷至 exe 旁并前置 PATH(`enca: dump windows asan imports...` 系列)。另证:该发行版不支持 `-static-libsan`(驱动层拒绝)。
   - **经验入库**:CI 必须保留 build / runtime loader / test execution / test result 四阶段信号,禁止 `make && ./test` 一把梭——「测试失败」与「进程未能启动」是两类故障,P2–P5 引入更多平台依赖后该区分持续有效。

2. **Linux functional-debug 连续红(gdb 包装返回 255)→ canary 执行语义重划**
   - 定位:本地实验证明 `gdb -batch -return-child-result` 在子进程死于信号时返回 255(SEGV/ABRT 均复现)→ CI 上 -O0 子进程真实崩溃;WSL 绑单核复现出 canary 饥饿 FAIL(reads==0)。
   - 根因:`cancel-race/borrow-unretained` 是**设计上违反 ownership contract 的裸借用 UAF 探针**,其检测价值只在 sanitizer 监视下成立;非 sanitizer 构建依赖「释放块暂不被复用」的时序侥幸,慢速共享核上必然演化成崩溃。
   - 处置(`81463ec0f69`):canary 仅在 `-DENCA_TEST_FORCE_RACE_CANARY` 时执行;`retain-on-load` 安全协议锤在所有构建不变。实测:常规构建 12077 checks / 0 failures,FORCE 构建 12087 checks / 0 failures。
   - 结论:不是删除测试、不是降低标准,而是把**功能正确性测试**与**工具灵敏度测试**分层——canary 不再污染 correctness 分数。

### 8.3 测试体系分层模型(P1.11 起保持)

```text
Functional(确定性 correctness)/ Concurrency stress / Sanitizer(ASan·TSan·UBSan)/ Canary(仅 FORCE)
```

### 8.4 进入 P1.11(最小嵌入)

目标:证明 ENCA 可作为 Emacs 进程内长期存活 Runtime 而不破坏 Emacs。init → idle run → shutdown,**零用户可见行为变化**。

硬 invariant:worker 绝不接触 Emacs Lisp_Object,只处理 native C data。

Phase 2 所有权设计债(header-tag 对齐读取 UB)**维持不动**,待 Snapshot/BufferState 等对象出现后再设计;「logical identity ≠ object lifetime」原则由 Phase 2 直接继承。

## 9. P1.11 最小嵌入落地(2026-08-22)

按上节规划落地 Emacs Build Integration。**本地全链路验证通过**(WSL2 Ubuntu 24.04,gcc 13.3,20 核)。

### 9.1 落地内容

| 文件 | 内容 |
|---|---|
| `configure.ac` | `--enable-enca`(默认 no);定义 `HAVE_ENCA` + `AC_SUBST(ENCA_OBJ)` |
| `src/Makefile.in` | `ENCA_OBJ = @ENCA_OBJ@` 变量层;追加进 `base_obj`(→ doc_obj → obj → ALLOBJS);enca 子目录对象的显式模式规则 + `$(DEPDIR)` 镜像目录的 order-only 创建(AUTO_DEPEND=yes 时 GCC 不会自建 -MF 目录) |
| `src/enca-emacs.c`(新) | 胶水层:`syms_of_enca` + 原语 `enca-available-p / enca-submit / enca-set-handler / enca-poll / enca-cancel / enca-status / enca-shutdown`;内部 `enca_glue_pump` / `enca_glue_shutdown`;handler 经 staticpro 保护 |
| `src/lisp.h` | 三个函数声明(json.c 段后) |
| `src/emacs.c` | `main()` 中 `#ifdef HAVE_ENCA syms_of_enca();`;`shut_down_emacs` 中 `sig==0` 时 join workers(致命信号路径不 join,防挂死) |
| `src/process.c` | 泵点:`wait_reading_process_output` 的 timer 块之后——与 timer 相同的 Elisp 安全点提交结果 |
| `src/enca/*/[a-z]*.c` ×15 | 文件首加 `#ifdef emacs # include <config.h> #endif`(Emacs 构建下满足 gnulib 包装头的 config.h-first 要求;独立构建不受影响) |
| `.github/workflows/enca-linux.yml` | 新增 `emacs-build` job:autogen → configure `--enable-enca --without-x --without-all` → make → 批处理 E2E 冒烟 |

设计要点:enca 对象名镜像源码相对路径(`enca/runtime/runtime.o`),make-docfile/buildobj.h 零特判;胶水原语全部惰性启动、无 DEFVAR、无新 Lisp 状态——禁用时 `ENCA_OBJ` 为空、无对象、无宏定义,**上游构建逐字节等价**。

### 9.2 本地验证结果(已验证)

- **enabled 路径**:`configure --enable-enca …` → `HAVE_ENCA=1`、`ENCA_OBJ` 替换正确 → `make -j20` RC=0(temacs 链接 + dump 完成);
- **垂直切片 E2E(batch)**:
  ```text
  phase1 ok: (8 1 2252249828700414988)   ← 8 提交→8 回调,FNV-1a 全一致
  post-cancel status: [2 24 22 8 14 2 0] ← cancel 后 16 个在飞任务精确入账
                                           (14 stale-drop + 2 cooperative)
  phase3 ok                               ← 新代继续接受提交(committed=9)
  ENCA_E2E_OK                             ← shutdown 干净退出
  ```
- **disabled 默认路径**:重新 configure(不带开关)→ `/* #undef HAVE_ENCA */`、`ENCA_OBJ` 空、增量构建 RC=0、`(fboundp 'enca-submit)` = nil;
- **子项目回归**:test/enca 全量 12077 checks / 0 failures(与 P1 冻结基线一致)。

### 9.3 过程中发现并修复的问题(复现→根因→修复)

1. **规则前向引用 DEPDIR**:规则块初版放在 `DEPDIR=` 定义之前,目标列表读取期展开为 `/enca/cancel/` → 移至 AUTO_DEPEND 段之后;
2. **config.h-first 冲突**:enca 源直接包含系统头撞 gnulib 包装头(`Please include config.h first`)。`HAVE_CONFIG_H` 在 Emacs 构建中并未定义(CPPFLAGS 无该宏),改用 `-Demacs` 作守卫条件;
3. **模式规则缺 `-o $@`**:对象按源文件基名落错目录且覆盖上游同名产物(`profiler.o`/`thread.o`)→ 补 `-o $@` 并清理被污染对象重建;
4. **胶水鸡生蛋**:`#ifdef HAVE_ENCA` 包住了 `#include <config.h>` 自身 → 整个文件编译为空、链接 undefined reference → config.h 提到守卫之前;
5. **fork 方言适配**:`CHECK_FUNCTION` 不存在 → `FUNCTIONP` + `wrong_type_argument (Qfunctionp, …)`;`ARRAYELTS` 已更名 `countof`;
6. **PowerShell UTF-8 BOM**:PS5.1 `Set-Content -Encoding UTF8` 写入 BOM(gcc 可容忍但污染 diff)→ 逐一剥离。

### 9.4 状态标注(已验证 vs 待 CI)

- ✅ 本地已验证:§9.2 全部(Linux/gcc);
- ⏳ 仅编写待 CI:`emacs-build` 云端首跑;Windows/W32 组合(enca thread 后端有 win32 实现但本机未配置 Emacs W32 构建);AUTO_DEPEND=no 的 deps.mk 分支(enca 对象暂无静态依赖条目,仅影响增量重建精度);clang 构建组合。

### 9.5 Submission Gate(2026-08-22)

按评审要求在提交前完成禁用路径硬验证(WSL 实测,非推断):

| 断言 | 结果 |
|---|---|
| `HAVE_ENCA` 未定义(config.h) | PASS(`/* #undef HAVE_ENCA */`) |
| `ENCA_OBJ` 为空 | PASS |
| ALLOBJS/buildobj.h/DOC 零 enca 对象与条目 | PASS(启用态阳性对照:buildobj.h 含 enca-emacs.o) |
| 全部原语不可见(fboundp=nil ×7) | PASS |
| stock batch eval 行为不变 | PASS |

判定:**P1.11 implementation = COMPLETE;cross-platform gate = PENDING**(等云端 CI:Linux 矩阵 + Windows + emacs-build 双路径)。

契约冻结:嵌入不变量 #11–#14 落盘于 `src/enca/ARCHITECTURE.md`(主线程独占 Emacs 状态 / worker 禁持 Lisp_Object / Emacs 安全点 join / 禁用即上游)。胶水原语面冻结,不再扩 API。

提交拆分(bisect 友好,每步 `--enable-enca` 均可独立构建):
1. `enca: integrate runtime into emacs build system`(configure.ac/Makefile.in/enca 源 config.h 守卫)
2. `enca: add minimal emacs runtime bridge`(enca-emacs.c/lisp.h/emacs.c/process.c + ARCHITECTURE 修订)
3. `enca: add minimal embedding ci gate`(workflow)
4. `enca: record p1.11 integration validation`(本节)

### 9.6 云端门禁闭环与封版(2026-08-23)

推送 `86005cf1172` 后双工作流首跑结果(GitHub Actions API 实查):

| Gate | 结果 |
|---|---|
| enca-linux / functional-debug(-O0 -Werror) | PASS |
| enca-linux / functional-opt(-O2) | PASS |
| enca-linux / asan-lsan | PASS |
| enca-linux / ubsan | PASS |
| enca-linux / tsan | PASS |
| enca-linux / **emacs-build(--enable-enca 全量构建)** | **PASS** |
| enca-windows(MSYS2 GCC + Clang ASan 独立套件) | PASS |

证据级别说明:job 日志下载需 repo admin(匿名 403),采用步骤级结论作证据——`Smoke test (batch vertical slice)` 步骤在 `set -e` 下成功,该脚本任一 E2E 断言(vertical slice 不全 / 哈希不一致 / 计数漂移 / stale+cooperative≠16 / 新代不活)均会非零退出,故步骤绿 ⇔ ENCA_E2E_OK。构建四阶段信号(checkout→configure→make→smoke)全部独立成步并各自绿。

### P1.11 最终判定表

```text
P1.11 Minimal Emacs Embedding
================================
Build integration (configure/Makefile)   PASS local+cloud
temacs link + dump generation            PASS local+cloud
Runtime init/run/shutdown                PASS local+cloud
Cancellation accounting (14+2=16)        PASS local+cloud
Generation continuation                  PASS local+cloud
ENCA disabled == upstream                PASS local(hard assertions)
Standalone suite regression              PASS local+cloud
Windows standalone matrix                PASS cloud
Linux standalone matrix                  PASS cloud
Emacs --enable-enca build + E2E          PASS cloud
Emacs --disable-enca build               PASS local(cloud 未单列,默认路径即此)

未覆盖(如实记录):clang 编译的完整 Emacs;W32 GUI Emacs 内嵌;
AUTO_DEPEND=no 分支;云端禁用路径构建。
```

**P1 Runtime Foundation + P1.11 Minimal Emacs Embedding → FROZEN**
tag: `enca-p1.11-minimal-embedding`

下一阶段(P2 Snapshot / State Isolation)另起基准,不在本 tag上演进 API。



## 10. P2.0 Snapshot Foundation Gate(2026-08-23)

按 P1.10.5 教训执行「先契约、再生命周期、后实现」:

### 10.1 契约冻结(先于代码)
- ARCHITECTURE.md 新增 #15–#19(Snapshot 语义 / 编码规范表示 / 类型化坐标 / 所有权生命周期 / 陈旧提交);
- SNAPSHOT.md §12 落盘 L1–L7 生命周期规则:publish≠acquire;每个外发指针恰好携带一个引用;snapshot 寿命 ≥ 一切消费者(独立于 document);epoch 双字段(runtime_generation × document_revision)永不合并;registry 仅身份;任务析构唯一入口 enca_task_input_destroy 四路径复用;created-destroyed==live 不变式。

### 10.2 实现
- src/enca/snapshot/{snapshot.h,snapshot.c}:document/snapshot 对象、publish/acquire/release、两级 epoch 校验、flat UTF-8(canonical view 隐藏存储)、计数器组;
- runtime 扩展(通用,worker 可见语义不变):enca_task_input_destroy 单一析构入口(可选 input_destroy 钩子)、submit_ex(stream_revision/user_data/borrow 输入)、completed 计数移至 res_push 之后(结果可见⇒已计数);
- 垂直切片桥:enca_snap_submit_latest = latest_acquire + 零拷贝借用提交,引用随任务转移。

### 10.3 测试矩阵(S 系列,+220 checks)
content S01-S06(空/ASCII/多字节/混排/二进制/1MiB);lifetime S10-S14(refcount 平衡/supersede 存活/4 读者并发/document 先亡/source 变异不可变);stale S20-S24(gen-only 引擎弃/rev-only 回调弃/双匹配提交/FNV 值校验);destruction S30-S33(正常完成/协作取消/引擎弃/shutdown drain 全走同一析构);金丝雀 live==0 且 destroyed==created。

### 10.4 过程发现并修复
1. **TSan 抓到真实设计违规**:snap_destroy 在读者线程直接 enca_idr_free 触碰注册表(L5 违规,10 个 data race)→ 销毁改两阶段:引用归零仅原子入 pending 栈(wait-free push),发布线程 reclaim 点统一释放槽位与内存;worker 释放路径变为纯原子;
2. tasks_completed_by_worker 原在 res_push 之前递增,spin 断言存在「计数已见、结果未可见」窗口 → 移至入队之后;
3. 测试侧三处断言错误(poll 返回值=processed 含引擎弃;修订号起点)经插桩证实引擎行为正确后如实修正。

### 10.5 本地验证(Windows gcc 14.2 五连跑 + WSL Ubuntu gcc 13.3)
| 构建 | 结果 |
|---|---|
| functional -O1(gcc) ×5 | 12297 checks / 0 failures,无抖动 |
| functional -O0 -Werror(WSL) | 12297 / 0 |
| ASan+LSan+UBSan(WSL) | 12293 / 0,零泄漏 |
| TSan(WSL) | RC=0,**0 data race**(修复前 10),12294 / 0 |

### 10.6 P2.0 Gate 表
```text
Contract   #15-#19 + L1-L7            DONE
Impl       Document/Snapshot/Registry/Publish/Acquire/TaskInput-destructor/UTF8-view  DONE
Correct    Immutable/Mutate-independent/Gen-stale/Rev-stale/Two-level/4-paths/zero-live  DONE
Sanitizer  ASan UBSan TSan            PASS(local)
Perf       cost-model benchmark       P2.1(非本门禁)
Not in P2.0: range/chunked/rope/piece-table/incremental-encode/parser/LSP  (明确不做)
```



## 11. P2.1 存储研究与增量发布实验平台(2026-08-23)

按评审定义执行:**P2.1 不是把 Snapshot 优化成增量结构,而是建立可重复的性能实验体系,用数据决定下一代存储**。P2.0 API 语义零改动;全部实验代码位于 `bench/enca/`(独立构建),冻结的 `src/enca/snapshot` 未触碰。

### 11.1 实验平台(P2.1.0–P2.1.3)
- **编辑流模型**(`editmodel.h`):EditScript{position, delete_len, insert_data} + 确定性 xorshift PRNG + FNV 校验;所有候选结构消费同一脚本,公平可比;
- **工作负载语料**(`workloads.h`,冻结):W1 代码编辑(热区小改)/ W2 连续键入(逐字符修订)/ W3 粘贴(1K–1MB)/ W4 重构(1MB 块替换)/ W5 大文件局部编辑;
- **指标**:capture 延迟 p50/p90/p99/max、content_copy_bytes、meta_bytes、live_bytes 增量(retention 维度)、并发读者聚合吞吐;
- **正确性守卫**:参考缓冲逐编辑应用同一脚本,FNV 每 N 版核对一次(该守卫在本阶段实际抓出 harness 自身堆越界,见 §11.4);
- 运行器:`run_matrix.ps1` 输出 CSV 至 `bench/results/p21_storage_study.csv`。

### 11.2 候选实现
| 家族 | 成本模型 |
|---|---|
| flat(P2.0 基线) | 每次发布全量复制:copied/edit = 文档大小 |
| chunked v1(本次新增) | 不可变块缓冲 + 修订级分片表(piece = buf/off/len);编辑只重写触点附近的表项并分配新载荷(≤chunk_size 分块);旧表共享全部未触及切片 |

v1 取「分片表」形态的原因:定长内容复制型 COW 在位移型编辑下无法约束拷贝量(退化为 O(doc));分片表给出与文档大小无关的 copied/edit,同时保留顺序读友好性。rope / piece-table / persistent tree 按 P2.1 边界仍为研究候选,不实现。

### 11.3 核心数据(Windows gcc -O2;完整矩阵见 CSV)
**Capture p50(ms) / 总拷贝字节 / retention 内存(live_delta)**

| 场景 | flat | chunked 64K | 倍率 |
|---|---|---|---|
| W1 64KB ×3000,ret1 | 0.0041 ms / 210MB / 73KB | 0.0294 ms / **37KB** / 214KB | 拷贝 ↓5600× |
| W1 1MB ×3000,ret1 | 0.395 ms / **3.16GB** / 1.05MB | 0.0238 ms / **37KB** / 1.21MB | 拷贝 ↓85000× |
| W1 10MB ×300,ret16 | 4.70 ms / **3.15GB** / **168MB** | 0.0133 ms / **4KB** / **10.7MB** | 内存 ↓16× |
| W3 10MB ×300(含粘贴),ret16 | 3.64 ms / 3.15GB / 168MB | 0.0083 ms / 405B / 10.7MB | — |
| W5 100MB 局部编辑 ×60,ret4 | **45.53 ms** / **6.29GB** / 419MB | 0.047 ms (64K) / **8.4KB** / 105MB | 延迟 ↓970× |

**Chunk-size sweep(W5@100MB)**:4K=0.33ms → 16K=0.11ms → 64K=0.047ms → 256K=0.018ms。v1 无合并策略下 chunk size 只影响初始切分与表长度(越大表越短),不产生经典写放大;该维度在引入合并/内容重写后需重测。

**读者吞吐(8 readers,W5@10MB 顺序扫描)**:flat 5353 MB/s vs chunked 5053 MB/s(**−6%**)——回答 §11 关注:分片走查对 parser 型顺序访问近乎无损。

### 11.4 过程发现并修复
1. **harness 参考缓冲堆越界**(文档增长而 ref 缓冲固定):表现为随机 CORRUPT/hang/heap-abort,经 ASan+gdb heap-validation 定位;修复为倍增扩容。此 bug 与两个存储实现均无关,但再次验证「每一步都要有正确性守卫」;
2. chunked 首版缺修订级引用计数(只有 buffer 级):环形保留+链式释放组合下双重拆表 → 补 rev refcount;
3. `enca_workload` 内嵌 1MB scratch 移至堆(ASan 爆栈定位)。

### 11.5 初步决策倾向(Result B 方向,待 P2.1 收尾确认)
- 小文档(<~256KB)且低保留:flat 与 chunked 差距小,flat 简单性占优;
- ≥1MB 或 retention≥8:chunked 全面优势(延迟 1–3 个量级、内存 ~15×);
- 顺序读损耗 ≤6%,对 P4 parser 可接受;随机访问 O(pieces) 为已知短板,需要索引时再设计(契约 #17 已预留类型化坐标层)。



## 12. P2.1.5 Storage Decision Closure(2026-08-23)

### 12.1 基线冻结
`bench/results/p21-flat-baseline-v1/`(matrix.csv + ENVIRONMENT.txt:gcc 14.2 -O2 / i7-12700H / Win11 / commit / seed 策略)。后续一切优化声明以此为对照点。

### 12.2 Benchmark 正确性验证(cross-check)
同一 EditScript(seed42)下:**flat.final_hash == chunked(C0/C1/C2).final_hash = c527403b08c3e836**,长度一致;新增 final_hash/logical/physical/sharing/maint_copied 列落盘。逻辑字节 vs 物理字节 vs 拷贝字节三账本分离,不再只看 wall clock。

### 12.3 关键数据
**Crossover(W1, ret=1)**:16KB flat 赢(9.7μs vs 14.5μs);64KB chunked 赢(17.5μs vs 36.9μs)→ 交叉点 ∈(16KB, 64KB),且随 retention 上移。

**Sharing ratio(W1@1MB, chunked-64K)**:
| ret | flat live | chunked live | chunked sharing |
|---|---|---|---|
| 1 | 1.05MB | 1.07MB | 1.94 |
| 8 | 8.40MB | 1.12MB | 8.29 |
| 32 | 33.6MB | 1.32MB | 26.08 |
| 128 | **134MB** | **1.92MB** | **70.37** |

flat 的 sharing 恒 ≈1(retention 越大越差);chunked 共享率随 retention 近线性增长——**证实「存储策略是 Snapshot Population 属性而非 Document 属性」**。

**Edit-size × Locality(16MB 文档,p50 ms)**:chunked 对 1B–100KB 编辑恒 0.005–0.018ms,局部性弱相关;flat 恒 5.9–9.9ms(与编辑尺寸无关——全量拷贝模型)。两族差距 ~600–1000×。

**Reader scaling(W5@10MB)**:1r:520/520 → 8r:4003/1881(flat 优势段)→ 32r:8966/**9246**(chunked 反超)。聚合吞吐双双扩展良好;chunked 在高并发读者侧更稳。

**Coalescing(W1@10MB ret16)**:
| 策略 | p50 | physical | sharing |
|---|---|---|---|
| C0 none | 13.1μs | 10.19MB | 16.69 |
| C1 local eager | **40.5μs(+3×)** | **16.79MB(+65%)** | 10.12 |
| C2 deferred thr=1.25 | **11.5μs(−12%)** | 10.21MB | 16.65 |
| C2 thr=2 / 4 / 8 | 13.4 / 14.9 / 15.7μs | ~10.2–10.8MB | 15.8–16.7 |

C1 eager 被证伪(延迟 3×、物理内存 +65%——合并副本滞留保留环);**C2 deferred 全面占优**,阈值 1.25–2 最佳且对前台零停顿影响;thr=8 过懒不触发(maint_copied=0)。

**Cold snapshot(ret128, 第5版修订在 295 次编辑后读取)**:chunked 1.6ms 全量校验 ok=1;flat ok=1。两者均可安全持有极冷快照。

### 12.4 Decision:**Result B — Hybrid Flat + Chunked(StoragePolicy)**
- **<16KB 且低保留**:flat(简单性、无元数据开销);
- **≥64KB 或 retention≥8**:chunked + **C2 deferred coalescing(threshold≈2, maint-every≈25)**;
- 中间区间的选择由 policy 输入(document_size, expected_retention, edit_frequency)动态决定,**不写死架构常量**;
- 架构表达:`Snapshot → StoragePolicy → {Flat Store, Chunked Store} → TextView`,P3/P4 仅见 Snapshot/TextView/OffsetIndex。
- 不进入 P2.2 persistent/rope 研究:chunked+deferred 已覆盖当前全部目标场景,无未解需求。

### 12.5 P2.1.5 Gate
```text
1 baseline freeze        DONE (p21-flat-baseline-v1/)
2 correctness validation DONE (final-hash cross-check ×4)
3 size sweep 1KB..1GB    DONE (Tier A/B/C)
4 retention 1..128       DONE
5 locality sweep         DONE (append/middle/random/hot)
6 edit-size 1B..100KB    DONE
7 reader scaling 1..32   DONE
8 coalescing C0/C1/C2    DONE (+threshold sweep)
9 cold snapshot          DONE
10 sharing ratio         DONE (1.94 -> 70.37)
11 decision report       DONE (本节)
```



## 13. P2.1 归档与 P3.0 Scheduler 契约冻结(2026-08-23)

### 13.1 P2.1 归档
- tag `enca-p2.1-storage-closure` 已指向决策提交;
- 基线永久冻结于 `bench/results/p21-flat-baseline-v1/`(含环境元数据);
- 架构结论:Snapshot → **StoragePolicy**(document_size × expected_retention × edit_frequency)→ {Flat, Chunked+C2 deferred}→ TextView;不引入 rope/piece-table/persistent tree(无未解场景);
- SNAPSHOT.md 状态更新为 CLOSED。

### 13.2 P3.0 契约冻结(先契约、后代码)
新契约 `src/enca/scheduler/SCHEDULER.md`,核心冻结项:

| 概念 | 冻结内容 |
|---|---|
| Task ≠ Work Item | Task 携带 task_id/document_id/generation/revision/snapshot/urgency/deadline/cancellation/task_class/policy 十要素 |
| 业务无知边界(#20) | 调度器词汇表只允许 class/urgency/deadline/cancellation/cost/resource;禁止 parser/completion/LSP 等消费者词汇 |
| Task Class | 封闭四类:SYSTEM / INTERACTIVE / BACKGROUND / MAINTENANCE(P2.1 C2 合并维护即首个 MAINTENANCE 消费者) |
| Urgency(#21) | 封闭五级枚举(realtime..maintenance),**禁止整数优先级**;deadline 与 urgency 正交 |
| 生命周期 | submit → **Admission**(ACCEPT/REJECT/COALESCE/REPLACE/DEFER)→ Queue → **Dispatch gate** → Execute → Commit;两道陈旧/过期门在计算之前 |
| Supersession(#23) | 域 = {document_id, task_class},严格更新 revision 才可取代;跨文档/跨类永不;执行中任务仅协作取消 |
| Drop-before-compute(#22) | 「不再能提交的工作不执行」为 P3 首要优化目标 |
| 公平性 v1 | 类内 FIFO、类间固定优先级;饥饿风险以等待直方图度量,升级机制延后 |
| 明确不做 | work stealing / NUMA / affinity / 抢占 / 自适应 / GPU / Elisp 线程池 API |

### 13.3 P3 性能目标转向
- 首要:**interactive tail latency**(p50/p90/p95/p99/p99.9,keypress→result);
- 次要:**wasted-work ratio** = 不可提交已执行工作 / 总已执行(commit 门按 #19 双级 epoch 分类);
- 吞吐量降为观测项。新增 drop 计数族:DROP_STALE / DROP_EXPIRED / DROP_SHUTDOWN。

### 13.4 子阶段路线(每段独立门禁)
P3.1 Task 模型+Admission 骨架 → P3.2 基础调度器(4 队列+N worker)→ P3.3 Supersession+stale 消除 → P3.4 Deadline+cancellation storm → P3.5 混合负载基准(S4)→ P3.6 公平性/tail 报告 → P3.7 扩展性研究(S5,1–32 workers)→ P3.8 决策(基础版是否足够)。
基准 S1–S7 定义随契约一并冻结(单任务成本/突发/交互修订突发/混合/饱和/取消风暴/shutdown 风暴)。



## 14. 方向校准与 P3.1 落地(2026-08-23)

### 14.1 方向校准(评审裁决,即刻生效)
诊断:近期工作出现「ENCA 从 Emacs 性能重构实验平台滑向独立通用异步 Runtime」的轻度漂移(P2.1 合理;P3.0 起风险显现)。修正为**双轨模型 + Vertical Value Gate**(已写入 ARCHITECTURE.md「North Star」节):

1. 每个 ENCA 子系统必须指认其改善的真实用户可感知路径(编辑/redisplay/补全/LSP/大缓冲/多窗口),答不出即暂停;
2. Track A(底座)只允许生长到支撑 Track B 的程度;Track B(真实 Emacs 性能路径)持续测量,其瓶颈反向驱动 Track A——基础设施不再先行;
3. 最终基准是 Emacs 用户路径(如 keypress→可见结果),ENCA 内部指标只是该路径上的诊断仪器;
4. 新增 Policy 层必须有证据表明更简单的方案不够。

对应关系表:Snapshot↔后台分析一致性;Scheduler↔补全/解析/诊断响应;Cancellation↔过期补全;StoragePolicy↔大文件快照;Trace↔延迟诊断。

### 14.2 P3 路线压缩(SCHEDULER.md §0.1/§11 已修订)
```
P3.1 最小 Task+Admission(本轮) → P3.2 最小调度器 → EVS-1 Emacs Interactive Vertical Slice(keypress 端到端基准) → 瓶颈分析 → 决定 P3.3 是否存在(允许不存在)
```
公平性/扩展性/自适应等深水区全部改为 EVS-1 证据驱动。

### 14.3 P3.1 落地(本轮实现)
- `src/enca/scheduler/{scheduler.h,scheduler.c}`:任务记录十要素、四类 FIFO 队列、Admission 引擎(SYSTEM 恒收 / MAINTENANCE FIFO / INTERACTIVE·BACKGROUND 按 #23 超越规则 REPLACE+FOLD)、提交期 DOA-deadline 门、派发期 generation×revision×deadline 三重门、DROP 计数族、shutdown drain;
- 单测 +53 checks:A1/A2 类内 FIFO 与类优先序、A3 REPLACE、A4 FOLD、域隔离(跨文档/跨类不取代)、G1/G4 两级 deadline 门、G3 revision oracle 门、snapshot 释放钩子全路径、shutdown 清场;
- 全量:12350 checks / 0 failures ×3 连跑(Windows gcc -O1);接线含 Makefile/CI 双列表/configure.ac ENCA_OBJ。

### 14.4 下一步
P3.2 最小调度器(worker 线程 + 执行 + 结果回流,S1/S2 门禁)→ **EVS-1**(Emacs 键入→捕获→调度→执行→回调→可见,端到端 keypress→result 基准)。EVS-1 数据出来前,P3.3+ 不存在。



## 15. P3.2 最小调度器执行器落地(2026-08-23)

按压缩路线实现并验证。范围严格收窄：N worker + dispatch loop + result FIFO + lifecycle；其余全部禁止。

### 15.1 实现
- `enca_sched_start_workers(s, n, exec_fn, exec_ctx)`：业务无知执行钩子 `exec_fn(task, ctx, out_value)`；
- Worker pop 内联**第二道 Admission 门**：generation 不匹配 / document revision 过期（oracle 回调）/ deadline 超时 → 各自 DROP 结果，绝不执行（#22 drop-before-compute 的运行时体现）；
- 结果为堆节点 FIFO；`enca_sched_poll` 在调用线程交给 commit 回调——**worker 永不提交、调度器永不触碰 Emacs 状态**；
- Shutdown 一等状态机 RUNNING→STOP_ACCEPTING→STOP_WORKERS→JOINED：期间 submits 被 REJECT(DROP_SHUTDOWN 计数)，排队任务以 DROPPED_SHUTDOWN 结果清空，join 干净。

### 15.2 验证中发现并修复的两个真实并发缺陷
1. **丢失唤醒死锁/延迟**：advance_generation 与 shutdown 的 broadcast 在队列锁外发射——worker 在「查空队列→进入 cond_wait」间隙会永久错过 → 两处均改为持锁 bump+broadcast（无丢失窗口）；submit 的 signal 同步移入锁内。
2. **push-after-free race(TSan 抓到)**：sched_push_result 在 rlock 解锁后读 `r->status` 做计数，主线程 poll 可能已 free 该节点 → 计数移入临界区（TSan 0 warnings 确认）。

### 15.3 测试与验证
新增 sched-exec 四套件(+10042 checks)：X1/S1 单任务全生命周期、X2/S2 burst 5000 账目恒等式(submitted==accepted+folded+expired+rejects; accepted==results_total)、X3 stale-gate(generation bump 后零执行)、X6/S7 shutdown storm(STOP_ACCEPTING 拒绝+清场)。
| 构建 | 结果 |
|---|---|
| Windows gcc -O1 ×3 | 22392 / 0 |
| WSL -O0 -Werror | 22392 / 0 |
| WSL TSan | **0 warnings**(修复前 6), 22389 / 0 |

### 15.4 P3.2 Gate
```text
Correctness  lifecycle/queue/revalidate/cancel/deadline/gen/rev/shutdown/result-routing/no-worker-Emacs-mutation  ALL PASS
Accounting   submitted=…恒等式 / every-drop-reasoned / no-double-release / no-lost-task                                            PASS
Concurrency  ASan UBSan TSan shutdown-stress                                                                                       TSan+func PASS(local)
Performance  S1/S2 p50..p99.9 queue/exec latency                                                                                  基础数据已采集,正式基准随 EVS-1
NOT GATE     work stealing / fairness opt / NUMA / affinity / adaptive / lock-free                                                 明确不做
```

### 15.5 下一步
**EVS-1 Emacs Interactive Vertical Slice**（keypress→buffer→capture→snapshot→schedule→execute→commit→可见），四组基准 E1 idle-typing / E2 typing+background / E3 revision-storm(核心：大量提交被 Admission 消灭至 1~2 执行) / E4 大缓冲。EVS-1 数据决定 P3.3 是否存在。



## 16. EVS-1 Closure — B0/B1/B2 对照与性能归因(2026-08-23)

### 16.1 三路同构对照(64KB 文档 ×500,WSL gcc -O1)
| 路径 | p50 | p99 | max | 判读 |
|---|---|---|---|---|
| B0 native insert | 0.7µs | 1.9–5.0µs | ≤62µs | Emacs 基线 |
| B1 sync analysis(+capture+snapshot+FNV) | 1.0–1.4µs | 2.9–5.7µs | ≤89µs | **B1−B0 ≈ 0.3–0.9µs** |
| B2 ENCA async | 见 §16.3 | — | — | 受测量 harness 限制(§16.4) |

**结论 A**:64KB 级文档下「捕获+快照+分析」同步成本 <1µs——ENCA 数据面开销在该区间可忽略,B2≈B0 成立。

### 16.2 E4 归因(C 侧精确 capture 计时)
| size | capture_avg | 当时的端到端读数 | capture 占比 |
|---|---|---|---|
| 1MB | **0.448ms** | ~8ms(含轮询粒度) | ~6% |
| 10MB | **4.313ms** | ~53.5ms(同上) | ~8% |

**结论 B**:C 侧 capture+publish 随文档线性增长(全量捕获模型),但在测得的总延迟中占比仅 6–8%;总读数的其余部分来自 Elisp 层 `buffer-string` 全量拷贝与 5ms 轮询粒度——三者同为「全量拷贝」家族,共同指向同一优化方向:**增量捕获/增量文本获取**。

### 16.3 E3 复核(storm,300 提交)
executed=1 / superseded≈145 / 其余 folded / committed_rev=最新 ✓ 稳定复现。**wasted-work 消灭已在真实 Emacs 路径成立**(对照传统架构需执行全部 300 次)。

### 16.4 已知测量限制(harness,非 ENCA)
- B2/E1 的等待依赖主线程 spin+sleep-for,粒度污染 tail 读数;
- worker→main 无唤醒机制,result 可见性依赖轮询(P3.2 遗留项);
- 修复方向属 EVS-2 harness 工作:cond-var 唤醒或 safe-point 注入。

### 16.5 性能停止规则执行
Scheduler/admission/poll 实测均处亚微秒~微秒级(<5% 贡献)→ **P3 冻结于 P3.2,不开启 P3.3**。下一个值得投入的用户可见瓶颈是**全量文本拷贝家族**(增量捕获),其次才是接入真实 completion/LSP(EVS-2 Real Completion,可与增量捕获并行评估)。

### 16.6 EVS-2 候选(按证据排序)
1. **Incremental Capture**(消除 buffer-string+full-snapshot 双重全量拷贝)——证据最强;
2. EVS-2 Real Completion——架构就绪,待真实 LSP 接入;
3. Redisplay 路径测量——batch 模式不可测,需 GUI 会话。



## 17. EVS-2 Incremental Capture — Contract/Bench/Closure(2026-08-23)

### 17.1 契约冻结(`bench/enca/evs2/EVS2.md`)
P2/P3 ABI 全部冻结不可动；Edit Delta v1=单连续区间(start/old_end/insert, canonical byte offsets)；Emacs buffer 不共享给 worker；oracle 硬门禁 hash(full)==hash(incremental) 每 revision 核对；核心新指标 **copy_amplification = physical_copied / logical_changed**。

### 17.2 Bench-only 实验(未触碰 Emacs 核心)
复用 P2.1 平台:flat store=Full Capture 基线;chunked piece-table store=Incremental Capture。同一 EditScript(W6 合成负载)+FNV oracle。32 格 sweep 零失败。

### 17.3 Small-Edit/Large-Document 不变量验证(1B edit @ middle)
| size | full: copied / capture p50 | incr: copied / p50 |
|---|---|---|
| 1MB | 105MB / 0.44ms | **100B** / 0.0024ms |
| 10MB | 1.05GB / 4.12ms | **100B** / 0.0065ms |
| **100MB** | **10.49GB / 46.16ms** | **100B / 0.0498ms** |

→ 100MB+1B:**延迟 ~928×、拷贝放大 ~10⁵× 改善**。不变量成立:incremental ≈ O(changed payload),与文档大小无关。

### 17.4 其余维度
- **edit-size 扫描(chunked@10MB)**:1B–100KB 全部 0.003–0.009ms——增量成本与编辑尺寸弱相关;
- **locality 扫描**:append/middle/random/hot 全部 ≤0.011ms——无局部性悬崖;
- 正确性:32/32 格 final_hash 双方一致。

### 17.5 EVS-2 Gate:**GO**
契约不变量全绿 + 放大比数量级改善 + 内容/revision/lifetime 全正确 → 进入集成阶段(EVS-2.2:src 后端整合 → enca-evs adapter → E1/E3/E4 重跑)。Redisplay 路线暂不启动(batch 无法测量,且当前归因未显示其主导)。

### 17.6 诚实限制
copy_amp 列在 CSV 分析脚本中存在解析空值显示问题(原始 copied_total 数据完整可靠);v1 分片表为 O(pieces) 顺序扫描,极端碎片化场景的合并策略沿用 P2.1 C2 deferred 结论。



## 18. EVS-2.3 A/B Closure — 真实路径上的 Full vs Incremental Capture(2026-08-24)

### 18.1 实现与接线(EVS-2.3 范围:adapter,零存储研究)
- `enca_document_adopt_snapshot`:外部构建的快照经标准 document 槽位发布,P3 revision 门控与 commit 校验零改动;dstate/adopt 套件覆盖(supersede 后旧版仍可读、生命周期恒等式闭合);
- `enca-evs.c`:`enca-evs-start WORKERS &optional INCREMENTAL` 双臂开关;worker 分析改走 `enca_snapshot_walk_text`(两种存储同一代码路径);新入口 `enca-evs-on-change-delta BEG END INS`(canonical byte offsets,v1 单连续区间);delta 计数(copied/changed)供放大比归因;
- A/B 驱动 `test/enca/evs23-bench.el` + `run_evs23.ps1`:同一 harness、同一编辑动作,唯一差异是捕获策略。
- 原生套件 24194 checks / 0 failures;ASan 干净;WSL 内真实 Emacs 构建编译零警告通过。

### 18.2 捕获延迟(CELL cap_avg_ms,12 edits/cell,4 edit 尺寸区间)
| size | full | incremental | 加速比 |
|---|---|---|---|
| 1MB | 0.76–1.19ms | 0.0045–0.0063ms | ~150–200× |
| 10MB | 5.8–6.9ms | 0.0053–0.0075ms | ~1,000× |
| **100MB** | **69–95ms** | **0.0050–0.0105ms** | **~10,000–19,000×** |

Copy amplification:incremental 臂 copied==changed=104,869,888B → **恰为 1.000**(payload 单次拷贝);full 臂 100MB+1B ≈ 8.7M×。EVS-2.1 bench 结论在真实 Emacs 路径复现。

### 18.3 端到端 submit→committed p50(keypress→visible 的 batch 模拟)
| size | full | incremental | Δ |
|---|---|---|---|
| 1MB | ~17.6ms | ~19.8ms | +12% |
| 10MB | ~54ms | ~59.8ms | +11% |
| 100MB | ~452ms | ~485ms | +7% |

**端到端没有变快,反而略慢。**

### 18.4 归因
- 全臂公共地板:~20ms 轮询量化(batch pump 循环,sleep 1ms+调度节拍)——1MB 级别完全由它主导;
- 主导项:合成分析 = 每 revision 对全文做 FNV——full 臂读连续内存,E2E−capture @100MB ≈ 362ms;
- incremental 臂同一分析走 piece-walk(~2000 片段、分散访问)≈ +120ms/revision——**捕获省下的 90ms 被全文分析的碎片化读取吃掉还倒贴**。

### 18.5 判定:**NO-GO(指标错位守卫触发)**
按 EVS2-DECISION.md §8 事先冻结的停止规则:capture 改善 >100× 而 keypress→visible 未改善 → **snapshot 存储优化到此永久停止**(§7 stop conditions 不因任何内部指标重开)。本结论同时验证了守卫机制本身有效:若没有 A/B 门禁,增量捕获会以「capture 快 19000×」为名继续吸投,而用户实际感知为零。

被否决的方向:piece-table 合并/rope/B-tree/任何存储深化。附带发现:增量臂略慢源于**全文档式消费者**(合成 FNV),真实区域型消费者(诊断/completion 增量解析)不会支付该成本——但那是 task/analysis 语义的新 vertical slice,不是存储工作。

### 18.6 下一步候选(按本轮证据重排)
1. **result→main 唤醒机制**(P3.2 遗留):~20ms 轮询地板现在主导全部小文档场景,E1/E3/E4 全线可见——下一个最大杠杆;
2. Real Completion(真实 LSP/区域型分析):只有引入非全文档消费者后,增量捕获的价值才有机会兑现;
3. Redisplay 测量:仍需 GUI 会话,batch 不可测。




## 19. EVS-3 Closure — Main-thread Wakeup:消除 20ms 测量地板(2026-08-24)

### 19.1 实现(EVS-3.0,契约先行:`bench/enca/evs3/EVS3.md`)
- `enca/wake/wake.{h,c}`:Runtime Notification 原语(token + mutex + condvar 的严格协议;IDLE/NOTIFIED/MAIN_DRAINING 语义、coalescing、produce-then-notify / drain-then-wait 规则与丢唤醒证明写入契约);
- scheduler 增加可选业务无知 observer(`result_notify`,叶子安全约束),在结果入队后触发——scheduler 不知晓 Emacs;
- **EVS-3 门禁抓到存量缺口**:文档宣称的 STOP_ACCEPTING 提交门从未实现(`shutdown_rejects` 计数器存在但无人递增,shutdown 后提交会静默入队)→ 补上 lifecycle 检查,wake/shutdown 套件覆盖;
- 七套件:single/burst/drain-race/notify-during-drain/producer-storm/shutdown/generation-reset。**24318/0 ×3 native + ASan clean + TSan(WSL gcc)0 warnings**——顺带补齐 EVS-2 遗留的 TSan 证据(全并发面)。

### 19.2 EVS-3.1 性能(真实 Emacs 构建,同 A/B harness)
| 场景 | 地板期 p50 | wakeup 后 p50 |
|---|---|---|
| E1 idle-typing(full) | ~19.3ms | **0.083ms** |
| E1 idle-typing(incr) | ~19.5ms | **0.0008ms** |
| E4 1MB | ~17–20ms | 4.3–6.2ms |
| E4 10MB | ~54–76ms | incr 37–42ms / full 43–76ms |
| E4 100MB | ~452–485ms | ~386–624ms(分析主导) |

~20ms 地板 → 微秒级。地板移除后首次公平地看到捕获成本:10MB 处增量臂稳定快 ~20%(40 vs 55ms);100MB 被合成全文分析(~400ms+)主导,双臂打平——与 EVS-2 NO-GO 结论一致。

### 19.3 判定
- EVS-3 = **成功**:目标就是地板本身,keypress→commit 在小文档达到微秒级;
- EVS-2 NO-GO 维持不变(§18),架构冻结块已写入 `src/enca/ARCHITECTURE.md` §25;
- 下一个主导成本已可测量:**大文档下是"每 revision 全文档分析"的工作负载模型**——这正是 Real Completion(区域型消费者)的动机证据;GUI Redisplay 分支仍待 GUI 会话数据。



## 20. EVS-4.1/4.2 — Synthetic Completion Slice(2026-08-24)

### 20.1 契约冻结(`bench/enca/evs4/COMPLETION.md`)
Completion 请求**不获得文档**:只有 snapshot 引用 + 声明的 context range,经**现有** `enca_snapshot_walk_text` 读取——零新 Snapshot API。W1–W4(前缀/成员/实参/语法)窗口 32B–4KB 冻结;C1–C4 = 64KB/1MB/10MB/100MB;simd-json、tree-sitter、Range View 全部显式出界。

### 20.2 O(region) 预算门禁(CONTRACT §8)
| 文档大小 | 256B 区域提取 avg | piece 数 |
|---|---|---|
| 1MB | 1.46μs | 801 |
| 8MB | 2.75μs | 801 |
| 10MB | 2.66μs | 801 |
| **100MB** | **3.14μs** | 801 |

文档 ×100 增长,提取成本恒定微秒级(增量来自 ~800 片段的元数据扫描)。**Range View 禁令获得永久数据支撑**:区域读取距任何可感知延迟差 4 个数量级。

### 20.3 Completion Storm(EVS-4.2)
| 打字间隔 | 零后端延迟 | 后端 5ms | 后端 20ms(clangd 级) |
|---|---|---|---|
| storm(0ms) | **executed=1/12** | 1/12 | 1/12 |
| 2ms | 12/12 | 12/12 | 9/12 |
| 10ms | 12/12 | 12/12 | 10/12 |
| 50ms | 12/12 | 12/12 | 12/12 |

结论:(a) 真正的输入风暴下 drop-before-compute 完美(1/12 执行);(b) 合成 server 太快时队列永不积压——supersession 无事可做是**好**消息;(c) 引入真实后端延迟后,排队任务被 REPLACE 消灭,而**执行中任务不受影响**(2ms 臂 9/12)——这正是 #23 的边界:in-flight 保护属协作取消(EVS-4.3 接真实 LSP 时接线)。

### 20.4 正确性与验证
- ct/extract-oracle:同一内容经 flat 与 piece-backed 两种存储,21 个区域组合逐一与镜像 buffer 字节比对全等;
- ct/basic / o-region-budget / storm;全套件 **31750 checks / 0 failures** native,ASan clean,**TSan(WSL gcc)0 warnings / 31747 / 0**。

### 20.5 下一步
EVS-4.3(单目标 clangd)的前置条件已满足:synthetic 切片证明 workload 形状成立、admission 在 IDE 型负载下行为正确、区域读取成本可忽略。剩余风险全部集中在 4.3/4.4 的传输层与 Completion UI/redisplay 归因。



## 21. EVS-4.3 Closure — Real LSP Transport Attribution(2026-08-24,clangd 19.1.0)

### 21.1 契约与实现(`bench/enca/evs4/EVS43.md`)
Scheduler 保持业务无知——LSP 层位于 executor 钩子之下;会话生命周期独立于请求(spawn+initialize+didOpen 一次性 setup);**版本映射不变量**:LSP textDocument.version == ENCA revision;**提交资格四项纯门禁**:document_id ∧ generation ∧ revision ∧ ?cancelled——这是正确性机制,`$/cancelRequest` 只是优化层。

### 21.2 三臂归因(B0/B1/B2)
| 臂 | 路径 | round trip |
|---|---|---|
| B0 synthetic | 直接函数调用 | ~2μs |
| **B1 loopback**(JSON-RPC + 真实 OS 管道) | 序列化 2.5μs + 解析 6.7μs + 内核管道 | **avg 8.12μs**(p_max 60μs,K=200) |
| B2 clangd | 同 B1 + 真实服务器 | **p50 = 77ms**(p95=108ms;setup: spawn+initialize 83ms,didOpen 1MB=8ms) |

```
B1 ? B0 ≈ 数 μs   → JSON-RPC + IPC 开销
B2 ? B1 ≈ 77ms    → clangd 处理,占 candidate-ready 的 >99%
```

### 21.3 判定:两条冻结规则同时触发
- **GO-Transport**:传输占比 ~0.01% → JSON/IPC 正式判定为非瓶颈,**simd-json / shared-memory / 特殊 IPC 被本轮数据永久否证**(除非负载形状改变并附新证据);
- **GO-Backend**:clangd 处理 >50% → 下一步属 backend/context 策略与 Completion UI 归因(EVS-4.4)。

原始论点的诚实结论:**Dynamic Module 消灭 Emacs?ENCA 边界 JSON 的收益是真实的(μs 级),但现代 IDE 延迟差距不在这个边界上——它在 backend 与 UI。**

### 21.4 工程记录
- 手写最小 JSON-RPC codec(递归下降成员遍历,字符串逃逸感知);修复两轮真实缺陷:(a) 非匹配标量值误绑定 `{"a":1,"id":42}`→1;(b) 帧消费后 acc 未重置导致旧帧重放;(c) loopback 回显需排空(discipline:通知也必须读回自己的回显);
- Windows 管道等待采用「自旋 2ms + Sleep(1) 兜底」混合策略,B1 归因保持微秒分辨率;
- 全套件 **31978 checks / 0 failures**(storm-real 变体 31995/0),ASan 干净。

### 21.5 下一步
EVS-4.4:commit → candidate 转换 → completion-table → popup → redisplay → visible 的逐段归因。按 §5 GO-UI 规则,若 commit→visible 主导尾部延迟,主战场正式转移到 Emacs UI/redisplay。



## 22. EVS-4.4 Closure — UI Critical Path Attribution(2026-08-24)

### 22.1 契约(`bench/enca/evs4/UI_ATTRIBUTION.md`)
T0–T12 时间点、四臂(A0 synthetic / A1 real / A2 worker-built model / A3 worker-built popup model)、R 阶梯(R0–R4)、以及**预冻结的 Render Snapshot 形状**(worker 产不可变快照,主线程 validate→swap→damage→redisplay)——供未来分支 B 触发时直接使用,避免二次设计辩论。

### 22.2 Completion transformation(T6→T7,真实 Emacs 数据结构)
| 候选数 | all-completions p50 |
|---|---|
| 10 / 100 | 0.011–0.023ms |
| 1K | 0.056–0.064ms |
| 10K | 0.27–0.46ms |
| **100K** | **6.4ms**(唯一越线格) |

### 22.3 Redisplay 阶梯(tty,强制重绘,含终端输出)
| 格 | p50 |
|---|---|
| R0 无弹窗基线 | 0.033ms |
| R2 popup overlay(1/10/50 行)| **1.57–2.04ms** |

popup redisplay 相对基线 ~60×,但绝对值仅 ~2ms。

### 22.4 判定
- **分支 B(redisplay 重构)不触发**:2ms 绝对值远低于任何重构证据门槛;EVS-5 Render Snapshot **无据启动,关闭**;
- 分支 A 仅在病态 100K 候选表出现;
- candidate-ready→visible ≈ **~2ms(tty)**,GUI 会话预计更低;
- keypress→visible 的主导项仍是 **clangd 后端 78–92ms**(§21)——ENCA 全链路(capture+snapshot+scheduler+wakeup+transport)合计 <0.1ms。

### 22.5 项目级结论(证据链闭合)
现代 IDE 差距不在 Emacs 边界、不在 JSON/IPC、不在 snapshot/scheduler/runtime,而在 (1) LSP backend 本身与 (2) 真实 completion UI 的集成质量。ENCA 已把「Emacs 自身可控的部分」压到微秒~亚毫秒级并全部冻结;后续任何优化必须先在 keypress→visible 上指认其毫秒级贡献,否则不做。



## 23. Project Re-scope — ENCA Performance Freeze 与 EVS-5(2026-08-24)

核心研究闭环完成:keypress→visible 的完整分解已经给出,**ENCA runtime <0.1ms、completion UI ~2ms、LSP backend 78–92ms(主导)**。据此:

- **ENCA Performance Freeze** 生效(`src/enca/ARCHITECTURE.md` §26):P1/P2/P3/EVS-1..4 全部 CLOSED,EVS-5 Render Snapshot NO-GO;多线程 UI、redisplay 重构、并行 GC、allocator 替换、NUMA、work stealing、shared-memory LSP、SIMD JSON、Rust 化 core 全部出界;
- **最高工程政策**:「没有毫秒级 user-path attribution,就没有架构改动」——以上每一项关闭都由该规则产生,重开任何一项需要新实验指认它将消除的精确毫秒份额;
- 项目正式更名为 **ENCA Real Completion / Semantic Latency**,下一阶段唯一目标:把 78–92ms 的 backend 主导项打下来。三条冻结的研究方向:**D1 Backend Context Engineering、D2 Cancellation/Speculation、D3 Completion Cache**;
- EVS-5.0 契约已冻结(`bench/enca/evs5/EVS5.md`):C1–C10 实验矩阵(cold/warm/narrowing/storm/cursor/edit/cancel/hit/miss/large-project)、缓存数据契约(不可变 refcounted 条目 + 区域失效)、阶段门禁 5.0→5.5。按指示,本阶段不写代码。



## 24. P0 Baseline Closure — Vanilla / ENCA-disabled / ENCA-enabled(2026-08-24)

### 24.1 三路构建(同一 upstream 基点 `11a1cb7445d`,同 configure、同 CFLAGS=-O2)

| 构建 | 内容 | 验证 |
|---|---|---|
| A vanilla | upstream 源码导出,零 ENCA 内容 | config.h 无 HAVE_ENCA |
| B disabled | fork 全部源码,`configure`(默认关)| **ENCA_OBJ 为空**;emacs 与 A 字节大小一致(2992936)|
| C enabled | fork 全部源码,`--enable-enca` | ENCA_OBJ 含全部 enca 对象 |

### 24.2 Batch 套件(中位数 ms,×3 轮 ×7 reps)
| cell | A | B | C | B/A | C/B |
|---|---|---|---|---|---|
| buffer-insert-1MB | 10.17 | 10.50 | 10.69 | 1.03 | 1.02 |
| buffer-replace-200 | 0.40 | 0.43 | 0.44 | 1.07 | 1.01 |
| completion-10k | 15.39 | 13.84 | 15.13 | 0.90 | 1.09 |
| gc-full | 8.35 | 8.90 | 8.67 | 1.07 | 0.98 |
| regexp-search-1MB | 15.45 | 14.68 | 15.03 | 0.95 | 1.02 |
| sort-10k | 6.26 | 5.57 | 5.63 | 0.89 | 1.01 |
| string-concat-500 | 9.73 | 9.05 | 9.37 | 0.93 | 1.04 |

### 24.3 tty Redisplay 套件(pty 强制重绘,×3 轮 ×10 reps)
| cell | A | B | C | B/A | C/B |
|---|---|---|---|---|---|
| R0-noop | 0.090 | 0.084 | 0.077 | 0.94 | 0.91 |
| R1-text-edit | 1.81 | 1.77 | 1.86 | 0.97 | 1.05 |
| R2-popup-10 | 1.75 | 1.60 | 1.74 | 0.92 | 1.09 |

### 24.4 判定
全部比值落在 **±10% 测量噪声带**(无任何方向性偏移):

```text
A ≈ B  →  ENCA patch 对 Emacs baseline 无可测侵入成本
       (disabled 构建与 vanilla 字节级同尺寸、行为同级)
B ≈ C  →  ENCA runtime 启用不产生系统性回归
A ≈ C  →  用户可见路径整体无回归
```

**ENCA 正式从性能嫌疑名单排除。** 结合 §18–§22:completion 路径的延迟主导项是外部 LSP backend(~80ms),而非 Emacs 核心、亦非 ENCA。EVS-5(Real Completion / Semantic Latency)因此聚焦 backend/context/cache 方向的决策获得最终依据。

### 24.5 附带修复
`src/enca/lsp/transport.c` POSIX 分支存在字面 `\n` 污染(此前 PowerShell 替换事故,Windows 构建不可见)——本轮 WSL 构建暴露并修复。



## 25. EVS-5.2.6 Closure — Cache 接入真实 UI:keypress→visible 分裂(2026-08-24)

### 25.1 集成
`enca-evs-complete PREFIX CURSOR` 在真实 tty Emacs 中走完整用户路径:capture/snapshot → scheduler(INTERACTIVE)→ worker(cache lookup → miss 时 LSP 往返)→ wakeup → 候选 → popup overlay 安装 → 强制 redisplay。MISS 臂后端成本以 90ms loopback 注入模拟(clangd 实测 78–92ms,EVS-4.3);传输与解析为真实代码。

### 25.2 keypress→visible(tty,每 op 含 popup+redisplay)
| 臂 | ops | hit% | **p50** | max |
|---|---|---|---|---|
| HIT(重复前缀)| 20 | 100% | **0.58ms** | 2.0ms |
| MISS(+90ms 后端)| 12 | 0% | 90.4ms | 90.6ms |
| MIX(交替)| 16 | 50% | 90.3ms | 90.5ms |

engine 内部(hit 路径)≈0.001ms;UI 段(popup+redisplay)≈0.3–0.6ms——与 EVS-4.4 的 ~2ms tty 地板同量级且更低(单行 popup)。

### 25.3 判定
**C8c/C12 达成**:hit 工作负载下 keypress→visible <1ms;C11 结构性成立(hit 路径零后端接触)。项目第一次出现**两个数量级的用户可感知收益**(90ms → 0.58ms)。

诚实边界:novel-prefix miss 仍是 backend-bound(~90ms)——这正是现代 IDE 的形态(命中即时、未命中等待服务器)。命中率提升属 EVS-5.2 Stage D(cross-revision reuse,需独立契约)与真实打字流研究。

### 25.4 过程修复
- didChange JSON 缺闭括号(EVS-4.3 storm-real 数据因此基于陈旧文档,已注明);
- collect/round_trip 现跳过 publishDiagnostics 通知帧;
- spawn 支持额外 argv(exec_argv),fake server 改标准 LSP 逐头读取(原 bulk read 对小帧死锁);
- 一个 getenv-vs-emacs-environ 交互段错误(原型期硬编码默认值绕开,gdb 定位)。



## 26. EVS-5.3 Closure — Real Typing Workload(F1,2026-08-24)

### 26.1 工作负载(tty Emacs + loopback 后端,miss 注入 90ms 思考时间)
| 场景 | ops | exact | extend | miss | backend avoided | p50 |
|---|---|---|---|---|---|---|
| identifier-growth(逐字符打词)| 32 | 9 | 2 | 21 | **34.4%** | 90.3ms(miss 主导)|
| retry(同请求重试)| 10 | 10 | 0 | 0 | **100%** | **0.074ms** |
| edit-interleaved(编辑交错)| 12 | 0 | 0 | 12 | 0% | 90.4ms |

Stage-B(同 revision 内 prefix 增长复用)落地后,增长类开始产生 extend 命中;保守失效策略下编辑交错类按设计全部 miss——**零误命中保持**。

### 26.2 F2 门禁评估(§9.5)
identifier+retry 类已进入亚毫秒;剩余 backend-bound 集中在**编辑后立即补全**的流程。cross-revision reuse 的可证明收益目标 = 把 edit-interleaved 的 0% 提升到 >50% 且 false_hit=0。该目标明确但实现风险高(需要 unrelatedness proof rule)——**F2 维持关闭**,等待真实用户数据显示该场景占比足以证明其复杂度。

### 26.3 项目最终状态(F1 后)
```text
ENCA core (P1-P3 + wakeup + transport)   <0.1ms      FROZEN
completion cache (strict rev + growth)   hit<1ms     Stage C GO
real typing hit rate                     34%-100%    MEASURED
edit-interleaved                         0%(by design)
false hits                               0           HARD GATE
```
项目从"架构优化"完整转型为"语义延迟实验平台":每一层的成本、每一个杠杆的有效性、每一条边界,都有可复现实验与原始数据支撑。


## 27. EVS-5.3.1 Closure — C13/C8c 完整版:真实打字分类 × 真实 popup/redisplay 路径(2026-08-25)

### 27.1 问题
把 EVS-5.3 的真实打字分类(retry / 前缀增长 / novel / 编辑交错)接到 EVS-5.2.6 的真实 UI 路径(popup overlay 安装 + 强制 redisplay,buffer 真实显示于 tty),对每类给出完整 keypress→visible 分布(p50/p95/p99/p99.9/max)。这是 Stage-D 决策的指定输入(契约见 CACHE.md §11)。

### 27.2 结果(tty,loopback 后端,miss 注入 90ms;原始数据 bench/results/evs531_ui_typing.log)
| cell | ops | exact | extend | miss | avoided | eng p50 | vis p50 | vis p95 | vis max |
|---|---|---|---|---|---|---|---|---|---|
| RETRY | 30 | 30 | 0 | 0 | **100%** | **0.15ms** | 4.6ms | 13.2ms | 30.9ms |
| GROWTH | 32 | 9 | 2 | 21 | **34.4%** | 90.3ms | 94.7ms | 95.5ms | 95.5ms |
| NOVEL | 15 | 0 | 0 | 15 | 0% | 90.4ms | 95.4ms | 119.1ms | 119.1ms |
| EDITMIX | 12 | 0 | 0 | 12 | 0%(设计如此)| 90.4ms | 95.0ms | 98.1ms | 98.1ms |

hit 类归因:install p50 ~0.04ms,redisplay p50 ~4.5ms —— cache hit 的可见延迟几乎全部是"真的画出 popup"这一 tty 重绘地板,与引擎和缓存无关。

### 27.3 测量口径修正(重要)
§25 的 HIT p50=0.58ms 是在**工作 buffer 未显示**(未 switch-to-buffer)时测得的 redisplay,几乎无物可画。buffer 真实显示后,诚实的 hit 类 keypress→visible 为 ~4.6ms p50,由 ~4.5ms tty redisplay 地板主导;引擎侧仍 <1ms(C8c 引擎子门保持 MET),UI 地板按 EVS-4.4 纪律单列、绝不隐藏。§25 数字按原样保留,但引用时必须带此口径说明。

### 27.4 交叉验证
GROWTH cell 与 F1 的引擎侧数字**完全一致**(exact=9 / extend=2 / miss=21 → avoided 34.4%),证明 UI 路径未扰动缓存语义;false_hit 门槛无异常(RETRY 全 hit / EDITMIX 全 miss by design)。

### 27.5 判定
- C8c:引擎侧 <1ms MET(全分类);全路径 hit p50 4.6ms,归因如上。
- C13:四类完整分布已报告,不设阈值(按契约由实验决定)。
- Stage-D 输入不变:唯一 backend-bound 的是 NOVEL(诚实的冷 miss,现代 IDE 同形态)与 EDITMIX(保守失效导致 0%)。把 edit-interleaved 复用从 0% 提到 >50% 且 false_hit=0,仍是唯一真实杠杆;其余一切已处于或低于 tty 绘画地板。

### 27.6 工件
- harness:test/enca/evs531-ui-typing.el(KPV31/SUM31 行格式,单一时钟覆盖 engine+popup+redisplay)
- runner:bench/enca/evs531_run.sh(WSL,script -qec,EVS53_LOG 直写仓库 results/)
- 契约+结果:bench/enca/evs5/CACHE.md §11
- 排障记录:harness 首版 GROWTH 词选择公式误写为 `(% i 12)`(原版 `(/ i 4)`)导致 exact 虚高 34.4%→46.9%;经原版复现 + 二分定位后修正。


## 28. EVS-5.4 Closure — 真实 LSP 打通 elisp 用户路径(2026-08-25)

### 28.1 契约与实现
`enca-evs-start` 的 BACKEND 参数落地 docstring 早已承诺的字符串形态:传路径即 spawn 真 LSP server(ENCA_LSP_CLANGD,握手全部在 start 时、测量路径之外);新增 `enca-evs-lsp-sync TEXT` 以 didOpen/didChange(full)推送文档状态,version 恒等于 ENCA revision。模拟延迟在 CLANGD 模式结构性不可能(session.c 仅 LOOPBACK 分支注入)。契约:bench/enca/evs5/REAL_LSP.md。

### 28.2 结果(tty,真 clangd 18.1.3;原始数据 results/evs54_real_lsp.log)
| cell | ops | exact | extend | miss | avoided | eng p50 | vis p50 |
|---|---|---|---|---|---|---|---|
| COLD | 1 | – | – | 1 | – | **3.51ms** | 10.5ms |
| RETRY | 30 | 30 | 0 | 0 | **100%** | **0.14ms** | 2.6ms |
| GROWTH | 32 | 9 | 2 | 21 | **34.4%** | 4.29ms | 7.0ms |
| NOVEL | 15 | 0 | 0 | 15 | 0% | **4.60ms** | 7.4ms |
| EDITMIX | 12 | 0 | 0 | 12 | 0%(by design)| 4.27ms | 6.9ms |

### 28.3 判定
- false_hit=0 硬门禁保持;GROWTH 精确复现 F1 混合(9/2/21);
- **引擎级 source 序列与 loopback 版逐 op 完全一致(32/32)**:后端替换对缓存层透明,交叉验证强度空前;
- C8c 引擎侧条款各类全 MET(hit <1ms);可见延迟归因仍是 redisplay 地板(inst ~0.06ms / red ~2.4ms),与 §27 一致。

### 28.4 重要口径警示
NOVEL 真实 miss p50 ~4.6ms 远低于注入 90ms 与 B2 真实项目 ~77ms:合成单行无 include 文档给 clangd 的 AST 近乎为空,且 didOpen 让解析与 setup 重叠。**本阶段验证的是路径正确性与分类形状,不修正后端主导性结论——真实项目的 candidate-ready 仍以 §21 的 78–92ms 为准。**

### 28.5 本阶段揪出并修复的两个真实缺陷
1. **read_frame 帧边界丢失**(session.c):消费一帧后累加器直接清零,丢弃同块已读入的流水线帧字节。loopback 严格一写一帧回声,历代 loopback 阶段全部不可见;真 server 会把 publishDiagnostics/$/progress 与应答混流 → 解析失步 → 野指针 SIGSEGV。修复:memmove 保留余量。
2. **enca-evs.c 缺原型**(自 cacc4a55/EVS-5.2.6 潜伏):enca_malloc/enca_free/enca_json_collect_item_labels 隐式 int 声明,属未定义行为。-O2 下 GCC 恰好整寄存器透传 RAX(实证:HEAD 版二进制重跑 531 harness 全部数字如常),但语言不保证,-O0 物化截断则确定性崩溃——本阶段首次接入真后端时即触发。补 memory.h/jsonrpc.h/sys/wait.h 修复。
   顺手修:evs_ct_exec else 分支缺花括号导致 hit 也计 ct_misses(仅统计,无门禁消费)。
修复后回归:原生套件 **34201 checks / 0 failures**(WSL clangd 就位后 B2 臂首次真跑),evs54 全量重跑通过。

### 28.6 环境
WSL 侧 `apt install clangd`(18.1.3)、valgrind 已装;Windows clangd 19.1 未用(构建树在 WSL)。


## 29. EVS-5.5 Closure — 真实补全/编辑轨迹测量(2026-08-25)

### 29.1 定位与契约
测量专用阶段:零 cache/引擎改动(纯 elisp harness),为 Stage-D 决策提供此前一直缺失的输入。事件模型 CompletionEvent + EditRelation 十类关系 + H0–H4 请求分类 + 引擎真值交叉校验,全部冻结于 bench/enca/evs5/TRACE.md。

### 29.2 结果(真 clangd 18.1.3;原始数据 results/evs55_trace.log)
| cell | requests | H0 | H0m | **H1** | H2 | H3 | H4 | safe% | eng_hits |
|---|---|---|---|---|---|---|---|---|---|
| IDGROW 打字链 | 13 | 1 | 0 | 0 | 0 | 0 | 12 | 0% | 1 |
| CALLARGS 参数生长 | 5 | 0 | 0 | **4** | 0 | 0 | 1 | **80%** | 0 |
| CURSOR 纯移动 | 5 | 0 | 4 | 0 | 0 | 0 | 1 | 0% | 2 |
| FAREDIT 无关区编辑 | 11 | 0 | 0 | **10** | 0 | 0 | 1 | **91%** | 0 |
| SESSION 混合权重 | 26 | 0 | 10 | 4 | 1 | 0 | 11 | **15.4%** | 5 |

### 29.3 判定
- **复用上限按行为双峰分布**:打字链 0%(每键都在改 token,判 H4/H2 正确);无关区编辑 91%、参数生长 80% —— 这才是 Stage-D 能变现的行为。混合合成会话 15.4%,低于 20% 讨论线,但权重是我们脚本的、不是用户的。
- **H0m 发现**:纯光标移动即改变 cache key(cursor 参与键)而语义不变;14 例中 7 例仍命中(同 revision 内键重复)。键归一化层可能以低于 Stage-D 的复杂度拿到部分收益——进入 Stage-D 成本侧公式。
- UNDO 5/5 按保守策略判不安全 ✓;引擎真值零矛盾:false_hit=0 门禁在轨迹层继续保持。
- 决策公式 P(H1)×backend_latency 两端均缺真实数据:(a) 用户 edit-after-completion 行为占比,(b) 本机真实项目后端延迟。**Stage-D 维持关闭,但从空白页变成了带测量仪器与逐行为上限的受控等待。**

### 29.4 排障记录
harness 三次迭代:①arm() 初版漏 didOpen → 首请求/纯移动单元全 nobackend;②空前缀种子请求用于锚定首键分类;③字符串下标(0 基)vs 缓冲位置(1 基)差一偏移导致 CALLARGS 首参数并入 token("proca")——EDT55 证据行 + 批量探针定位,常量统一为缓冲位置后消除。


## 30. P0-EIPB Phase 1 — 核心交互基准落地(2026-08-25)

### 30.1 路线调整
项目主线自本日起从"单路径归因(EVS)"升级为"全编辑器系统测量(P0-EIPB)":EVS-1..5 证明了 completion/LSP 一条路径的纪律,但 startup/editing/redisplay/GC/search/undo/font-lock/file-I/O/multi-window/soak 等域从未系统测量,"现代 IDE 性能"的说法在矩阵填满前不成立。框架与纪律冻结于 bench/eipb/EIPB.md:禁综合分、百分位强制、A/B/C/D 四构建学说、tty 与 GUI 分离、§26 归因门禁继续有效。

### 30.2 Phase 1 结果(tty,四构建各 141 行;原始数据 results/eipb_t1.log,矩阵 bench/eipb/MATRIX.md)
| 域 | 代表格 p50 (A/B/C/D) |
|---|---|
| 插入/删除/粘贴(64KB–10MB)| 全部 ≤0.008ms,四构建无系统性差异 |
| 编辑+可见(1MB)| 0.60–1.13ms(p50);p99 尾部受 GC 干扰 |
| Redisplay R0/R1/R2/R3 | 0.03–0.06 / ~1.4 / ~0.9–2.1 / ~2.9–7.1ms |
| GC 强制暂停 | **p50 18–30ms,p99 167–245ms,max 189–320ms** |
| 风暴均值暂停(160MB 活跃堆)| 175–347ms |
| 单键 undo | ~1.2–1.4µs |
| 10MB 搜索(elisp 循环口径)| literal 1.7–3.2s;regexp-id 0.5–1.0s |

### 30.3 判定
- **ENCA 影响 ≈ 噪声带**:editing/redisplay/search/undo 全域 A≈B≈C≈D,无任何一格出现系统性回归——P0 结论在全交互域推广成立;
- **T1 最大发现与 ENCA 无关**:GC 强制暂停 max 达 190–320ms 且 vanilla 同样存在。这是 Emacs core 级别的交互停顿源,量级碾压表内其他所有尾部,是后续"现代 IDE 差距"讨论中第一个被数据点名的真实候选(font-lock 路径之前);
- 启动墙钟在百 ms 量级噪声过大(B tty-cold 590ms vs batch3 82ms 自相矛盾),标记为临时值,Phase 2 改 N≥5 取中位数;
- 搜索口径为 elisp while-loop 含每次匹配调用开销(~60 万次),非纯 C 扫描,已注明。

### 30.4 排障记录(全部实证)
①`(undo)` 命令批处理怪癖(user-error)→ 改 `primitive-undo`;②`gc-elapsed` 实际以秒推进(10 次 GC delta=1.48)与手册 µs 说法不符 → 按 ×1000 修正;③**撤销必须抑制自身记录**,否则历史每次调用翻倍(O(2^n) 爆炸,harness 卡死根因);④**已消费条目须及时清空**,残留会让后续 primitive-undo 报 "outside visible portion"(bisect6 逐轮插桩定位)。


## 31. P0-EIPB Phase 2 — 尾延迟归因(2026-08-25)

### 31.1 执行概况
合同冻结三问(font-lock 链 / GC 用户路径 / 多窗口多缓冲),D+A 双构建 tty 各 **467 行、完全对称、零 FATAL**,总墙钟 2925s。原始数据 results/eipb_t2.log,判定表 bench/eipb/phase2/report/PHASE2.md。

### 31.2 判定
- **Q1 font-lock 是编辑尾延迟源,但强模式依赖**:c-mode 单键内 jit 上下文块重刷可 ≥100ms(100KB 下 40 键中 6 次,vanilla 同样存在);org-mode 每键 ~12-15ms 均匀税;elisp/python 键级 <1-4ms 实际免税。冷全量 fontify:c/1MB ≈108-110s、python/1MB ≈265-286s、c/10MB ≈786-964s(A/D 无系统性差)。
- **Q2 GC 暂停确实落入用户可感路径(首次实测证实)**:强制每 25 键 GC 使 max 31→52ms(+67%)、p99 +51%,两构建同形;但默认阈值下自然触发仅 6/1000 键,tail 仅 +7-10ms。T1 大堆 190-320ms 危险形态不变——优先级排序应为"随堆大小增长的暂停时长",非 GC 频率。
- **Q3 窗口数(非缓冲数)是可见延迟乘子**:edit+visible 随窗格数 1→2 翻倍后饱和(8 窗 ~45-47ms vs 1 窗 12-20ms);空闲 noop 与窗口数无关(~0.15ms);滚动成本反随窗格变小而降。缓冲切换亚线性。
- **ENCA 影响仍≈噪声带,但出现一个待查信号**:EOB 插入/空闲 noop/冷 fontify 全部 D==A;mid-buffer 编辑+可见类 D 高出 A 1.3-2.8x 且随规模放大(buf10 1.66x→buf100 2.68x→8x100 聚焦 3.83x)。Phase 2 未跑 B/C,无法区分 fork 底座 vs ENCA 启用 → 标记 PENDING-B/C-CHECK,按学说不下结论。

### 31.3 排障记录(session-2/3 全部实证)
① 上 session 死因:磁盘 harness 是半成品重构(`eipb2--timed-reps` 被调用未定义、`wb-make-buffers` 命名错位),且 FL 单节 420s 超时杀死后续全部问题 → 本轮补齐定义、逐格 condition-case 隔离、FL/GCPATH/WB 分节独立超时;② plain 控制臂崩溃根因:**上游 jit-lock--run-functions 在 jit-lock-functions 为空时对 (min nil beg) 求值崩溃**(batch 复现,vanilla 同样)→ 控制臂跳过 jit 调用并留痕;③ popup 数据整列缺失:辅助函数经参数 push 只改局部形参(lexical-binding)→ 改返回值传递;④ GCPATH 臂表引号列表内 `(* 64 1024 1024)` 不求值 → 字面量;⑤ script(1) pty 默认高度装不下 8 窗(window-min-height)→ harness 开局 set-frame-height 50;⑥ wsl.exe 会话退出会杀裸 nohup 后台任务 → setsid+`</dev/null` 启动器。


## 32. P0-EIPB Phase 2.1 — mid-buffer 信号 B/C 归因闭环(2026-08-25)

### 32.1 执行概况
针对 §31 遗留的 PENDING-B/C-CHECK 信号(mid-buffer 编辑+可见 D 比 A 高 1.3–3.8x):A/B/C/D 四构建、两轮交错(A,B,C,D ×2)、GC 钉死(64MB 阈值+格前回收)、纯 fundamental、确定性输入轨迹,240 行零 FATAL,墙钟 135s。原始数据 results/eipb_p21.log,判定书 bench/eipb/phase2/report/PHASE2_1.md。

### 32.2 判定
- **Phase-2 信号不复现**:六格中位 D/A 全落 0.66–1.17,无任何 D 特异性超标;
- **既非 ENCA 也非 fork 底座**:B≈C≈D 三者每格聚簇,唯一离群的是 vanilla A 且方向为更慢 —— 按 §31 冻结决策树归入"测量环境"分支,定性为**运行顺序伪影**(Phase 2 每节 D 先 A 后;Phase 2.1 中 A 恒占第 1 位,位置而非构建身份跟踪偏差);
- 可声明结论保持最小:**运行顺序受控后,四构建在 mid-buffer×可见路径同带**;<30% 的跨构建差在本单 VM tty 装置上不可分辨,维持 PENDING 多轮矩阵(学说 3 不变);
- "vanilla 比 fork 慢"本身同样是位置伪影,**禁止**作为结论外传。

### 32.3 制度产出
① 未来所有 tier 运行强制 round-robin 交错构建,禁固定顺序(EIPB.md 学说增补);② 尾部指标(p95/max)单会话置信不足,跨构建尾部比较一律待多会话;③ tag `enca-eipb-phase2-attribution-closure` 由本节授权,随 Phase 2.1 提交落地。


## 33. P0-EIPB Phase 3 — 用户路径覆盖扩展:T3-A 文件打开链 + T3-B 交互 isearch(2026-08-25)

### 33.1 执行概况
覆盖阶段而非优化阶段。4 构建 × 3 独立 session、每轮 `shuf` 随机顺序(学说 7+8),12 session ×79 行完全对称、零 FATAL。文件打开链五段分解(read+decode / mode / fontify / redisplay / idle-drain),isearch 按键分布(hit/miss × 100KB/1MB)。原始数据 results/eipb_t3.log,判定书 bench/eipb/phase3/report/PHASE3.md。

### 33.2 判定
- **isearch 增量按键实际免费**:稳态 p50 0.11–0.16ms,两尺寸、命中/未命中、四构建全部同带;唯一尾部是首键的 BOB 全扫(100KB ≈1.6–2.8ms;1MB ≈7–16ms,单次/搜索)。≤1MB 缓冲下 isearch 不构成交互延迟风险;
- **文件打开链无可感知停顿(≤10MB 温缓存)**:io 段 ~4ms/MB 线性且四构建同带;链中最贵段是 fontify(与 T2 结论衔接);idle drain 仅微秒级——redisplay 返回后用户路径里没有隐藏成本;
- **gcs_delta 协议对称性完美**:elisp/1MB 四构建均恰 117 次 GC —— 分配行为跨构建逐字节确定,兼作 allocator 一致性检查;
- **ENCA 无系统性方向**:所有 >30% 波动标志都被相邻指标/相邻 rep 反向抵消,按学说 8 全部记为 observed difference,不作因果表述;
- 交互延迟地图现状:已证实的毫秒级停顿仍然只有 T2 的 c-mode jit 块(≥100ms)、T1 大堆 GC 暂停(190–320ms)、T2 Q3 多窗重绘乘子(~45–47ms@8 窗)。file I/O 与 isearch 进入"已排除"列。

### 33.3 边界与遗留
温页缓存口径(真冷缓存需 drop-cache 权限,deferred);elisp 臂封顶 1MB(10MB elisp 冷全量 fontify 单次 ~60s+ 会淹没 session 预算,T2 已有该 regime 合成数据);首键指标 n=3 过薄,尾部结论需 ≥10 session;shuf 多行 marker 为已知无害怪癖,顺序可由日志行序列恢复。T3 余项:IDE-MIXED-01 轨迹、xref/imenu、org/dired 类工作流。


## 34. P0-EIPB Phase 3.1 — IDE-MIXED-01 脚本会话轨迹(2026-08-25)

### 34.1 执行概况
冻结脚本:开项目(20 文件混合语言)→ 切换 → c-mode 打字 → capf 补全 → 滚动 → isearch → 编辑/删除 → undo → 再补全 → 窗口循环 → 保存 → idle。逐动作时间戳轨迹(日志序=时序),4 构建 ×2 轮随机顺序(r1:D A B C;r2:A D C B),205 行/session 完全对称、零 FATAL、零补全错误。原始数据 results/eipb_t31.log,判定书 bench/eipb/phase3/report/PHASE3_1.md。

### 34.2 判定
- **组合未暴露新瓶颈**:所有动作中位数 ≤ 其单路径对应值;已知停顿恰好出现在预测位置(c-jit 打字 p95 ~93–123ms 全构建一致;打开尾部的秒级段 = c 文件冷 fontify 而非 I/O)——真实会话成本 = 各部分成本之和;
- **地图新增覆盖项:native completion-at-point 整轮 ≈45–70ms**(含 Completions 窗口渲染),是 GC/fontify 之外测得的最大稳态交互延迟;覆盖数据而非缺陷判定,LSP 类后端已由 EVS 单独测量;
- **ENCA 无方向性信号**:散点尾部尖峰(isearchkey B 664ms×1、wincycle D 53ms×1、capf2 C 339ms×1)n≤10 无跨构建模式,PENDING 多 session;session 墙钟 A +18%(n=2 且恰占前位)仅记 observed difference;
- undo 中位 0.21ms、idle drain 0.19–0.22ms —— 会话深处仍无隐藏成本。

### 34.3 排障记录(smoke 三迭代全部实证)
① kill-buffer 对修改过的文件缓冲弹交互确认 → 脚本会话在 summary 发出后挂死至超时 → 清 modified 标志+unlock+静音查询函数;② primitive-undo 按"边界段"消费,脚本操作无边界致一次吞光(undostep n=1)→ eipb3--op 每操作后追加 undo-boundary(同时更贴近真实命令粒度);③ dabbrev-expand 在"同前缀+上下文擦除"重放下暴露内部状态机崩坏(search-failed / wrong-type-argument 两轮实证)→ 弃用,换官方无状态入口 completion-at-point(elisp capf 收集缓冲内标识符,fn-NNNN 天然候选)。


## 35. P0-EIPB Phase 3.2 — xref/imenu 命令延迟 + org 工作负载(2026-08-26)

### 35.1 执行概况
4 构建 ×2 轮随机顺序,8 session ×47 行完全对称、零 FATAL;协议确定性再次完美(xr/scan matches_mean 四构建均 = 1068.0000)。原始数据 results/eipb_t32.log,判定书 bench/eipb/phase3/report/PHASE3_2.md。

### 35.2 判定(地图更新)
- **imenu 首次建索引入列停顿**:30KB elisp / 20KB c 的冷构建 ≈300–560ms(一次性、每缓冲首次),缓存重建 ~0.25ms;goto 导航自由(0.21–0.42ms);
- **org/global-cycle 入列停顿**:2000 标题整缓冲可见性扫描 p50 ≈0.42–0.56s、p95 至 1.2s —— 与多窗重绘同档,vanilla 同形 → Emacs core 属性;
- **交互级 org 安全**:子树折叠 p50 ~4.3–5.0ms、标题导航 ~0.8ms;
- **xref 项目扫描**(grep 后端)≈67–98ms p50,D/A 1.5x 散布 n=6 不可分辨 → observed difference;
- **ENCA 全程无方向**;org 冷 fontify 300KB ≈1.7–3.2s(n=2 噪声带内)与 T2 org 标度衔接。

### 35.3 排障记录(probe 链全程实证)
① `xref-matches-in-directory` 的 FILES 是 **find-glob 语义**而非正则:".*" 只匹配点文件(静默 0 命中)→ 改 "*";探针链:最小用例 → *xref-grep* 原始缓冲 → 手工管线 → glob 语义定位;② 手工复刻 grep 管线漏掉函数内部的 `<C> → <C> -E` 模板改写,BRE 下 `\[` 匹配字面括号 —— 探针伪影记录;③ 会话在全部 teardown 探针通过后仍挂死于 kill-emacs 内部(process-list 为空!)→ 以 confirm-kill-processes nil + 成功臂内退出缓解,**根因未解**,标记给 T4 soak harness(需大量干净退出)。


## 36. P0-EIPB Phase 3.3 — dired 覆盖 + T3 收官、Atlas 冻结 v1(2026-08-26)

### 36.1 执行概况
最小冻结集:空/小(20)/大(2000)目录进入、refresh、create/rename/delete(+revert)、dired↔file 跳转。4 构建 ×2 轮随机,8 session ×37 行完全对称、零 FATAL。原始数据 results/eipb_t33.log,判定书 bench/eipb/phase3/report/PHASE3_3.md。

### 36.2 判定
- **A≈B≈C≈D 全格成立 → dired = mapped / 无 ENCA 信号**;
- 新测绘条目:进入 2000 文件大目录 ≈177–234ms(一次性);小目录 ~10ms;变更类(create/rename/delete 含 revert+渲染)全部 ~8–9ms;跳转 ~2.3ms;
- empty 目录的 p95/max ~0.5–0.7s 为 dired 机械首次冷启动(ls 子进程+模式建立),四构建同形 = 会话位置伪影,p50 行才是稳态真值。

### 36.3 T3 收官 —— Atlas 冻结 v1
覆盖缺口清零(dired 落地)。**停顿列五项**:c-jit ≥100ms、大堆 GC 190–320ms、多窗重绘 ~45–47ms@8窗、imenu 冷索引 ~300–560ms、org/global sweep ~0.5s。**关闭列**:编辑/undo/isearch/file-I-O/LSP/completion transport/mixed 组合/xref/capf/dired 全操作/org 交互级。明确延期(非待办):真冷缓存打开、GUI 变体、magit 类流。
**Phase 4 门禁**:SOAK 必须回答"关闭行是否持续关闭、停顿行是否保持稳定"(30M→2H),且 p32 的 kill-emacs 退出挂死列为 **T4 必须归因项**,不得以"主体没崩"判绿;RSS 判定按斜率(slope)而非首尾相减,区分合法增长与无界增长。
