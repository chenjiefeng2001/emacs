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
