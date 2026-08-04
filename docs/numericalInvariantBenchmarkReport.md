# 数值不变式插件 Benchmark 验收报告

## 1. 验收目标

本报告验证两个数值不变式方向已经进入 ACSLG 的普通循环插件路径，并能在仓库
`benchmark/` 中的实际程序上生成可解析、可复证的 ACSL：

1. 本地 C-finite 多项式递推发现。
2. DeepSeek 候选生成加 Clause2Inv combine-check。

“正确”在这里表示生成的 loop invariant 满足项目内精确 C 机器整数模型的 initiation、
definedness 和 consecution 义务，并由 Frama-C 接受。选定样例额外使用 Frama-C/WP
独立证明标签对应的建立性和保持性。它不表示当前版本已经自动证明 benchmark 的全部
postcondition；extractor 尚未从循环后的 C assertion 自动构造 exit-post obligation。

验收日期为 2026-07-30。本机工具为 Clang/LLVM 19.1.7、Frama-C 31.0 和 Z3 4.8.12。

## 2. 插件入口

```bash
# 完全本地、无网络
ACSLG_NUMERICAL_INVARIANTS=local ./build/src/ACSLG input.c

# 本地发现加外部 Clause2Inv provider
ACSLG_NUMERICAL_INVARIANTS=llm ./build/src/ACSLG input.c
```

默认未设置时保持原插件行为。`local` 选择
`VerifiedNumericalPathInsensitiveLoopInv`，`llm` 选择
`Clause2InvPrototypePathInsensitiveLoopInv`。后者同时运行本地 polynomial 插件和
Clause2Inv 插件。

输出使用来源标签：

```text
loop invariant acslg_polynomial: ...;
loop invariant acslg_clause2inv: ...;
```

## 3. 自动化本地验收

CTest `numerical_invariant_benchmarks` 直接运行构建出的 `ACSLG`，输入来自仓库原始
benchmark。测试脚本位于 `tests/integration/numericalInvariantBenchmark.cmake`。

| Benchmark | 生成的不变式 | 结果 |
| --- | --- | --- |
| `AutoSpec/code2inv_133_benchmark/2.c` | `y >= 0 && y + 2*x - y*y == 2` | 项目 Z3 复证；Frama-C/WP 2/2，非线性保持性由 Z3 证明 |
| `AutoSpec/fib_46_benchmark/30.c` | `i >= 0 && 2*c + i - i*i == 0` | 项目 Z3 复证；Frama-C 接受生成 ACSL |
| `AutoSpec/code2inv_133_benchmark/120.c` | `i >= 1 && sn - i == -1` | 项目 Z3 复证；Frama-C 接受生成 ACSL |

脚本还执行以下回归：

- 每个输入运行两次并比较 SHA-256，确保输出确定；
- 以 disabled 模式运行 `2.c` 和 `30.c`，确认非线性结果确由新增插件提供；
- Frama-C 解析全部三个生成文件；
- 使用 `-wp-prop=acslg_polynomial` 精确选择 `2.c` 的两个 WP 目标，并要求
  `Proved goals: 2 / 2` 且保持性实际调用 Z3；
- Z3 不可用时不注册该测试，主项目和普通单元测试仍可构建。

## 4. Clause2Inv 真实 Provider 验收

真实 API 运行只在人工显式 opt-in 时执行，不进入默认 CI，也不保存请求、响应或凭据。
API key 位于 Git 忽略的 `.acslg-local/` 配置中。

### 4.1 线性同步更新

输入：`AutoSpec/code2inv_133_benchmark/120.c`

观测输出：

```text
loop invariant acslg_clause2inv: sn <= i;
```

该候选经项目 verifier 复证后才写入文件。Frama-C/WP 对标签
`acslg_clause2inv` 的建立性和保持性给出 2/2 Valid。

### 4.2 分支更新

输入：`AutoSpec/code2inv_133_benchmark/3.c`

循环在 `z <= y` 时执行 `y = z`，否则保留 `y`。本地 C-finite 在这个分支模型上没有产生
候选；Clause2Inv 生成：

```text
loop invariant acslg_clause2inv: y <= \at(y, BeginOf_foo_e53a);
```

它表达 y 相对函数入口单调不增。Frama-C/WP 对两个标签目标均证明成功：建立性由 Qed
完成，分支保持性由 Z3 完成。

首次实验曾输出只涉及未变化变量 z 的入口关系。随后增加
`requireChangedStateVariable` 质量策略，正式插件只接受依赖实际更新状态的最终组合；
修正后的输出为上述 y 关系。

LLM 输出本身不受信任且可能随 provider 变化。验收结论依赖的是“返回候选必须通过固定
JSON AST、预算、精确机器整数义务和最终独立复证”，不是依赖某次自然语言回答。

## 5. 保守拒绝

| Benchmark | 原因 | 行为 |
| --- | --- | --- |
| `code2inv_133_benchmark/94.c` | 当边界达到 `INT_MAX` 时更新可能发生 signed overflow | nonlinear 候选不输出 |
| `code2inv_133_benchmark/15.c` | `unknown()` 形成当前 extractor 不支持的外部未知分支 guard | 在 provider 调用前拒绝提取 |

这些结果是 soundness 边界。不能通过切换到数学整数、忽略未知 guard 或接受 solver
`unknown` 来提高表面通过率。

## 6. 语料规范化

部分 Code2Inv/FIB 文件使用现代 C11 不接受的遗留写法。自动测试只在构建目录的副本中做
透明规范化，不修改 benchmark：

- `void main()` 改为 `int main()`；
- 对使用 `assert` 但未包含头文件的输入添加 `<assert.h>`；
- 将函数体中的 `static_assert(...)` 改为运行时 `assert(...)`。

这些改动不改变循环迁移。临时文件和生成 ACSL 位于
`build/tests/numerical-invariant-benchmarks/`。

## 7. 复现

```bash
cmake -S . -B build -DBUILD_TESTS=ON -DACSLG_ENABLE_Z3_PROTOTYPE=ON
cmake --build build -j2
ctest --test-dir build -R numerical_invariant_benchmarks --output-on-failure
```

单独验证自动生成的非线性标签：

```bash
frama-c build/tests/numerical-invariant-benchmarks/local-a/2_acsl.c \
  -wp -wp-prover z3 -wp-prop=acslg_polynomial -wp-timeout 10
```

真实 provider 测试需要先从受保护的本地配置导出 `ACSLG_LLM_*`，再设置：

```bash
export ACSLG_RUN_LIVE_LLM_TESTS=1
ctest --test-dir build \
  -R 'LiveDeepSeek|LiveDeepSeekResult' --output-on-failure
```

不要在命令历史、日志、测试源或提交中写入 API key。

## 8. 当前边界

- 数值插件默认关闭，避免改变现有调用方行为。
- `local` 仅覆盖预算内的二次多项式递推，最多四个状态变量和四个分支。
- `llm` 依赖外部服务，只负责候选生成；失败不会降级为未经证明的输出。
- 复杂循环仍走原有 complex group。
- 未自动抽取 exit postcondition，因此当前 benchmark 验收针对 loop invariant 本身。
- 全 corpus 的覆盖率、运行时间分布和与 StInGX 的去重仍需单独评测。
