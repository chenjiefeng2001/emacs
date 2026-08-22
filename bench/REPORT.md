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

## 5. 当前仓库状态
- 分支 `master` 与 `origin/master` 同步,**无任何已跟踪文件被修改**(diff 为空);
- untracked 仅 3 个顶层目录:`bench/`、`src/enca/`、`test/enca/`;
- `test/enca/` 下残留运行产物 `rt_out.txt`/`rt_err.txt`(过期)与本次验证的 `rt_out2.txt`/`rt_err2.txt`,均不在 `.gitignore` 内。

## 6. 遗留事项与建议
1. **ASan 验证未做**:stale-drop 曾出现 61≠50 的计数偏差,疑似 `advance_generation` 释放旧 cancel source 与 worker 无引用地并发读取之间存在 UAF 窗口(仅为假设);建议跑 `make -C test/enca asan` 复核。
2. 清理或 gitignore `rt_out*.txt`/`rt_err*.txt`。
3. 三个目录是否纳入版本控制待定;若日后真正 configure Emacs,重跑 `baseline.ps1` 会自动记录 configure-options。
