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

