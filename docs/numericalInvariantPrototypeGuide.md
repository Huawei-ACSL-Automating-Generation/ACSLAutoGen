# 数值不变式原型架构与使用指南

## 1. 状态与范围

`research/numerical-invariant-prototypes` 分支完成了两个经过实验验证的数值不变式方向：

1. C-finite 多项式递推发现，用于产生非线性等式候选。
2. Clause2Inv 风格的 generate-combine-check，用于组合原子 clause，并由 Z3 独立验证。

两个实现只通过公开的 `symbolic::Expr` facade 访问不可变表达式 DAG，不包含
`Analyzer/Symbolic/detail` 头文件。Clause2Inv 还完成了从真实 `LoopInfo` 和符号执行状态
抽取迁移模型、可选 DeepSeek provider、精确受限 C 机器整数验证和 ACSL 输出。

当前完成标准是“受限语义下的 sound prototype”：

- `Proved` 候选必须通过完整 SMT obligation。
- 不支持的 C 语义、未知内存边界、solver `unknown`、超时和预算耗尽都不输出不变式。
- 默认插件组不访问网络。只有显式 opt-in 才选择 Clause2Inv prototype group。
- Z3 缺失时项目仍可构建，SMT 路径返回 `SolverUnavailable`。

本地 C-finite 插件已在三个仓库 benchmark 上自动验收，生成结果由 Frama-C 解析；
Code2Inv 非线性样例还通过了 Frama-C/WP 的独立保持性证明。Clause2Inv 使用真实
DeepSeek 在两个 benchmark 上完成了生成、复证和 WP 检查。详细输入和结果见
`numericalInvariantBenchmarkReport.md`。

自动提取循环后的 postcondition、大规模 corpus 通过率和面向具体 ABI 的实现定义行为策略
仍不属于当前完成范围。

## 2. 总体数据流

```text
Clang AST
  |
  v
ProgramState / Path symbolic execution
  |
  +-- immutable symbolic::Expr DAG
  +-- branch guards
  +-- current/entry scalar values
  +-- integer and memory definedness conditions
  |
  v
extractTransitionModel()
  |
  v
TransitionModel
  |
  +-------------------------------+
  |                               |
  v                               v
discoverPolynomialRecurrences()   ClauseProvider
  |                               |
  |                               v
  |                         atomic clauses
  |                               |
  |                               v
  |                     combineInvariantClauses()
  |                               |
  +---------------+---------------+
                  |
                  v
            verifyInvariant()
                  |
                  v
       verifyAndEmitLoopInvariant()
                  |
                  v
          ACSL loop invariant
```

`TransitionModel` 是算法之间唯一的迁移协议。候选不通过 ACSL 字符串交换，只有最终验证通过
后才格式化为 ACSL。

## 3. 目录与模块

| 文件 | 责任 |
| --- | --- |
| `transitionModel.h/.cpp` | 公共迁移模型和结构校验 |
| `transitionModelExtractor.h/.cpp` | 从 `Path`/`ProgramState` 提取真实循环迁移 |
| `polynomialRecurrence.h/.cpp` | C-finite 多项式递推发现 |
| `clauseCombiner.h/.cpp` | Z3 翻译、obligation 验证、clause 组合 |
| `clauseProvider.h/.cpp` | 严格 JSON AST、HTTP transport、OpenAI-compatible provider |
| `clauseSynthesis.h/.cpp` | 多轮 provider 反馈与总预算管理 |
| `invariantEmitter.h/.cpp` | 最终独立复证和 ACSL 格式化 |
| `clause2InvPrototype.h/.cpp` | 提取、合成、复证和输出的端到端 API |
| `numericalInvariantPrototypePlugin.cpp` | SpecGenerator opt-in 插件适配 |

插件组选择集中在 `SpecGenerator/groups.h/.cpp`：

| `ACSLG_NUMERICAL_INVARIANTS` | 普通循环行为 |
| --- | --- |
| 未设置或其他值 | 原有默认 group，不运行新增数值插件 |
| `local` | 运行本地 verified polynomial 插件，不访问网络 |
| `llm` | 运行本地 polynomial 插件和 Clause2Inv provider 插件 |

旧的 `ACSLG_ENABLE_CLAUSE2INV_PROTOTYPE=1` 仍兼容并等价于 `llm`，新调用应使用统一变量。
复杂循环继续使用原有 complex groups。

## 4. 迁移模型

核心结构如下：

```cpp
struct TransitionVariable {
    symbolic::Expr current;
    symbolic::Expr entry;
};

struct TransitionBranch {
    symbolic::Expr guard;
    std::vector<symbolic::Expr> nextValues;
    std::vector<symbolic::Expr> definednessConditions;
};

struct TransitionModel {
    std::vector<TransitionVariable> variables;
    std::vector<symbolic::Expr> parameters;
    symbolic::Expr precondition;
    symbolic::Expr loopCondition;
    std::vector<TransitionBranch> branches;
    std::optional<symbolic::Expr> postcondition;
    TransitionIntegerSemantics integerSemantics;
};
```

约束：

- 所有表达式必须来自同一个 `ExprFactory`。
- 每个 branch 的 `nextValues` 数量必须等于状态变量数量。
- guard、precondition、loop condition、postcondition 和 definedness condition 必须是布尔值。
- extractor 只跟踪循环 entry 已存在的 Bool 或 8/16/32/64 位整数标量。
- 指针、数组和结构不成为状态变量，但其访问安全条件可以进入 branch definedness。

`TransitionIntegerSemantics::Mathematical` 供代数原型使用；
`TransitionIntegerSemantics::CMachine` 启用受限 C 机器整数语义。

## 5. C-finite 多项式递推

入口：

```cpp
auto result = discoverPolynomialRecurrences(model);
```

算法流程：

1. 从 current 变量和 parameter 建立确定顺序的符号基。
2. 以精确 `mpq_class` 构造规范化稀疏多项式。
3. 枚举限制次数内的单项式。
4. 对每个 branch 构造精确单项式替换。
5. 对候选特征值求所有 branch 的共同零空间：
   `q(next) = lambda * q(current)`。
6. `lambda == 1` 时产生 `q(current) == q(entry)`。
7. entry 可精确证明为零时产生 `q(current) == 0`。

默认预算：

| 项目 | 上限 |
| --- | --- |
| 状态变量 | 4 |
| 分支 | 4 |
| 总符号 | 8 |
| 多项式次数 | 2 |
| 单项式 | 64 |
| 候选 | 128 |
| 特征值绝对值 | 16 |

支持字面量、变量、parameter、`+`、`-`、一元负号和乘法。除法、未知叶节点、超次数替换和
规模超限会返回明确状态，不截断、不使用浮点近似。

## 6. Clause2Inv 管线

### 6.1 原子 clause

原子 clause 可以来自：

- 调用方提供的本地候选；
- StInGX 或其他候选生成器的结果；
- 实现 `ClauseProvider` 的外部 provider；
- `OpenAICompatibleClauseProvider`，当前用于 DeepSeek。

LLM 只负责提出候选，不能批准候选。响应必须是受预算约束的 JSON 表达式 AST；任意自由文本、
未知变量、函数调用、指针解引用、量词和逻辑组合原子都会被拒绝。

### 6.2 组合

组合器按确定顺序尝试：

- 原子本身；
- `old && new`；
- `old || new`。

先用已有反例过滤明显失败的组合，再执行完整 SMT obligation。默认限制为 16 个原子、
128 个候选和 64 次 solver query，单次查询超时 250 ms。

### 6.3 证明义务

候选 `I` 只有依次满足以下义务才是 `Proved`：

1. Initiation：`precondition => I(entry)`。
2. Loop-condition definedness。
3. 每个 branch 的 guard、更新和内存 definedness。
4. Consecution：`I(current) && loopCondition && guard => I(next)`。
5. ExitPost：存在 postcondition 时，
   `I(current) && !loopCondition => postcondition`。

任何 obligation 得到反例时返回失败类别、branch 和受限 counterexample。`unknown`、超时和
预算耗尽不会降级为成功。

### 6.4 最终输出

`verifyAndEmitLoopInvariant()` 会再次独立调用 verifier。只有第二次证明仍为 `Proved` 才输出：

```text
loop invariant acslg_polynomial: <ACSL expression>;
loop invariant acslg_clause2inv: <ACSL expression>;
```

标签标识候选来源，并允许使用 `frama-c -wp-prop=acslg_polynomial` 或
`-wp-prop=acslg_clause2inv` 独立选择证明义务。emitter 会拒绝非法 ACSL 标签。

正式 Clause2Inv 插件还要求最终候选依赖至少一个在某条迁移分支中实际更新的状态变量。
未变化变量与其入口值之间的平凡关系不会抢先成为输出；未变化条件仍可作为有用组合的一部分。
这防止 provider 或组合器状态被误当成证明结果，也减少虽正确但无信息量的结果。

## 7. 受限 C 机器语义

精确模式使用 Z3 Int 表示数学值，并额外维护类型范围和 definedness，而不是把 C 整数直接
近似为无界整数。

已支持：

- Bool 和 8/16/32/64 位 signed/unsigned 整数叶节点。
- Clang 已明确表示的 integer promotion、usual arithmetic conversion 和回写 cast。
- unsigned 加减乘的模运算。
- signed 加减乘和一元负号的可表示性检查。
- C 向零截断的 signed 除法/余数。
- 除零与 `INT_MIN / -1`。
- 合法 shift count、unsigned shift、受限 signed 左移。
- 精确 `~`、`&`、`|`、`^`。
- Bool/整数转换、signed-to-unsigned 模转换。
- 可证明保值的 signed narrowing 和 unsigned narrowing 模转换。

明确拒绝：

- signed 右移，因为结果依赖实现策略。
- 不能证明可表示的 unsigned-to-signed 转换。
- 直接在窄类型上执行、且 AST 未体现 integer promotion 的运算。
- 浮点、枚举之外的非整数标量、向量和超出支持位宽的整数。

这些 `Unsupported` 是 soundness 边界，不是 solver 失败。若未来支持具体 ABI，应新增显式
target policy，并保持默认拒绝。

## 8. 内存 definedness

`MemoryModel` 以 allocation provenance 保存可选的元素数量。已知 extent 来源包括：

- 固定长度数组；
- `BSL_SAL_Calloc(count, sizeof(T))`；
- 可以精确换算为元素数量的 `BSL_SAL_Malloc`。

实际访问要求：

```text
0 <= offset && unsigned(offset) < extent
```

指针形成允许 one-past：

```text
0 <= offset && unsigned(offset) <= extent
```

覆盖普通加减、`+=/-=` 和 `++/--`。指针差值及关系比较要求两端具有相同 provenance，并且
都满足 formation 边界；跨对象 `==/!=` 仍允许。

以下情况设置 `hasUnknownMemoryAccess`，extractor 会在调用 Z3 或 provider 前拒绝：

- 参数指针等没有 extent 的地址；
- null 或无法求值地址的读写；
- use-after-free、double-free、interior-pointer free；
- 不同 allocation 间的指针差值或关系比较。

null 读取返回 `UnknownExpr`；null 写入使用内部占位地址让符号执行继续，但绝不会进入不变式
证明。

## 9. 构建与测试

启用 Z3：

```bash
cmake -S . -B build -DBUILD_TESTS=ON -DACSLG_ENABLE_Z3_PROTOTYPE=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

验证可选依赖路径：

```bash
cmake -S . -B build-no-z3 -DBUILD_TESTS=OFF -DACSLG_ENABLE_Z3_PROTOTYPE=OFF
cmake --build build-no-z3 -j2
```

当前完成基线为 Z3 配置 329/329 CTest 通过。无 Z3 配置的 `ACSLG` 和
`test_all` 构建通过，依赖 SMT 的 benchmark CTest 不会注册。

主要测试：

| 测试文件 | 覆盖 |
| --- | --- |
| `polynomialRecurrence_test.cpp` | 非线性递推、共同分支、反例、限制、确定性 |
| `clauseCombiner_test.cpp` | obligation、预算、反例、C 机器语义、内存边界 |
| `transitionModelExtractor_test.cpp` | 真实 AST/Path 抽取和端到端证明 |
| `clauseProvider_test.cpp` | JSON AST、HTTP envelope、配置和可选真实 API |
| `clauseSynthesis_test.cpp` | 多轮反馈、无进展、provider/solver 总预算 |
| `invariantEmitter_test.cpp` | 最终复证后输出 |
| `loopInvariantPlugins_test.cpp` | group 隔离、opt-in 和可选真实插件链路 |
| `numericalInvariantBenchmark.cmake` | 真实 corpus、确定性、关闭模式、Frama-C 与 WP |

真实 API 测试默认跳过，避免普通测试产生费用或依赖网络。

## 10. 使用方式

### 10.1 直接使用 C-finite API

```cpp
invariant::TransitionModel model = buildModelFromPublicExprFacades();
auto result = invariant::discoverPolynomialRecurrences(model);
if (result.status == invariant::RecurrenceStatus::Success) {
    for (const auto &candidate : result.candidates) {
        if (candidate.invariant)
            consume(*candidate.invariant);
    }
}
```

调用方必须在同一个 `ExprFactoryScope` 内创建模型表达式。

### 10.2 直接使用 Clause2Inv

```cpp
MyClauseProvider provider;
auto result = invariant::runClause2InvPrototype(
    concreteEntry,
    symbolicEntry,
    symbolicCurrent,
    loopCondition,
    loopEntryPoint,
    provider);

if (result.status == invariant::Clause2InvPipelineStatus::Emitted)
    consume(*result.acsl);
```

生产代码应检查枚举状态和 `reason`，不能只检查字符串是否为空。

### 10.3 通过 ACSLG 使用本地插件

```bash
ACSLG_NUMERICAL_INVARIANTS=local \
  ./build/src/ACSLG -p <compile-database-dir> path/to/source.c
```

该模式只运行确定性的本地发现和 Z3 复证，不需要 API key。

### 10.4 通过 ACSLG opt-in 使用 DeepSeek

所需环境变量：

```bash
export ACSLG_NUMERICAL_INVARIANTS=llm
export ACSLG_LLM_API_KEY='<local secret>'
export ACSLG_LLM_BASE_URL='https://api.deepseek.com'
export ACSLG_LLM_MODEL='<configured model>'
```

API key 只从环境读取。本地配置可以放在已被 Git 忽略的 `.acslg-local/` 下，但不得提交。

运行：

```bash
./build/src/ACSLG -p <compile-database-dir> path/to/source.c
```

输出仍写入源文件旁的 `source_acsl.c`。未设置 `llm` 时不会访问 provider。

运行付费真实 API 测试时还需显式设置：

```bash
export ACSLG_RUN_LIVE_LLM_TESTS=1
ctest --test-dir build --output-on-failure
```

## 11. 扩展规则

新增候选生成器时：

1. 输入和输出使用 `TransitionModel` 与公开 `symbolic::Expr`。
2. 不包含 `Symbolic/detail`，不缓存裸 node 指针。
3. 明确支持的 AST、规模预算和确定顺序。
4. 不支持输入返回状态，不截断成近似语义。
5. 最终候选仍经过 `verifyInvariant()`。

新增 C 语义时：

1. 同时修改 extractor 的类型可接受性检查和 Z3 translator。
2. value 与 definedness 必须同时编码。
3. 增加可证明正例、UB/实现定义反例和真实 AST 提取测试。
4. 验证无 Z3 构建。

新增 provider 时：

1. 实现 `ClauseProvider`，只返回结构化原子 clause。
2. 设置响应、节点、深度、调用和总 solver 预算。
3. provider 失败不得绕过 verifier。
4. 凭据只从环境或受保护的外部 secret store 获取。

## 12. 已知非目标

- 自动从循环后的 assertion/函数契约构造 `postcondition`。
- Frama-C/WP 大规模 corpus 统计与默认开启数值插件。
- 浮点不变式。
- 并发和原子内存模型。
- 任意 heap alias/shape analysis。
- 未经显式 target policy 的实现定义整数行为。
- 将不受当前规模预算约束的候选生成器接入生产路径。

这些项目需要新的验收标准，不应通过放宽当前 `Unsupported` 路径完成。
