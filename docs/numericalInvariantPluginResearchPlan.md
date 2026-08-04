# 新型数值不变式插件调研与实现计划

> 调研日期：2026-07-27  
> 目标：在现有 StInGX 线性/仿射析取不变式之外，选择两个有学术依据、能够落入
> ACSLAutoGen 插件框架、并能由 Frama-C/WP 消费的数值不变式生成方向。
>
> **完成状态（2026-07-30）：已完成当前原型目标。** 两个候选均通过公共 `Expr` API、
> 非平凡正反例、预算/确定性和全量回归门槛。Clause2Inv 另已完成显式 opt-in 的主循环入口、
> 受限 C 机器整数与内存 definedness。维护和使用说明见
> `docs/numericalInvariantPrototypeGuide.md`。

## 1. 结论

建议按以下顺序实现：

1. **CFiniteRecurrencePlugin（P0）**  
   基于 CAV 2024 的 polynomial-expression C-finite recurrence 方法，从一次循环符号执行
   得到的分支迁移中提取多项式递推关系，生成 StInGX 无法表示的非线性等式不变式。
2. **ClauseCombineInvariantPlugin（P1）**  
   基于 ISSTA 2025 Clause2Inv 的 generate-combine-check 方法，将 StInGX、recurrence、
   区间/循环边界和可选 LLM 产生的原子 clause 进行反例驱动组合，再通过 SMT 严格验证后
   输出合取或析取不变式。

两个方向互补：

- C-finite recurrence 是新的、确定性的**候选生产器**，主要扩展非线性表达能力。
- Clause2Inv 是**候选组合与验证器**，主要解决单个 clause 正确但逻辑组合不完整的问题。
- 两者都必须输出结构化 `symbolic::Expr`，禁止通过解析已有 ACSL 文本交换结果。
- 任意算法超时、遇到不支持语义或 solver 返回 `unknown` 时均不输出候选，不以猜测结果
  降低正确性。

## 2. 当前架构与能力缺口

### 2.1 现有数据流

当前循环处理过程是：

```text
SetEntryAndCurrentPlugin
    -> 对循环体符号执行一轮
    -> symbolicLoopEntry / symbolicLoopCurrent / inactivePaths

SetIndexPlugin / SetPatternsPlugin / SetSharedStatePlugin
    -> 填充 LoopInfo

LinearInvariantPlugin (StInGXPlugin)
    -> buildLoopInvariant()
    -> Expr/PPL 线性迁移关系
    -> LinTS::ComputeLinTSInv()
    -> ACSL + path-sensitive post-info
```

关键位置：

- `src/SpecGenerator/specGenerator.h`：`LoopInfo` 和 PI/PS 插件接口。
- `src/SpecGenerator/loopInfoPlugins.cpp`：循环 entry/current、index、pattern 信息提取。
- `src/SpecGenerator/loopInvariantPlugins.cpp`：StInGX 等循环规格插件。
- `src/Analyzer/Symbolic/invariant.cpp/.tpp`：`Expr -> PPL`、迁移系统和不变式回写。
- `src/Stingx/`：基于 Farkas lemma 的 affine disjunctive invariant 算法。

StInGX 对应的算法已经是 VMCAI 2025 的 affine disjunctive invariant generation。因此不应
再增加一个功能高度重叠的 polyhedral/affine 插件。

### 2.2 主要缺口

1. `x += y; y++` 等更新可产生二次关系，但 PPL 线性约束不能表示二次等式。
2. 乘法、几何增长和部分分支循环会被 `loopHasNonAffineOps()` 提前排除。
3. 现有插件大多独立输出完整不变式，缺少跨算法复用候选 clause 的机制。
4. 当前 StInGX 结果在较后阶段被直接渲染为字符串，不适合作为其他算法的结构化输入。
5. 插件框架没有统一的候选正确性证书，也没有 initiation/consecution/post obligation
   检查接口。
6. `propose()` 只决定 post-info 的主结果；所有 PS 插件的 ACSL 都会被输出。新增插件如果
   没有统一验证层，可能把弱候选或重复候选直接写入最终注释。

## 3. 学术方法筛选

| 方法 | 能力增量 | 工程可行性 | 主要风险 | 结论 |
|---|---:|---:|---|---|
| C-finite polynomial-expression recurrence（CAV 2024） | 非线性等式、分支递推 | 高 | 单项式爆炸、整数溢出语义 | **选择，P0** |
| Clause2Inv generate-combine-check（ISSTA 2025） | 合取/析取组合、目标导向 | 中高 | 需要 SMT；LLM 成本 | **选择，P1** |
| LORIS local-reasoning feedback（TOPLAS 2026） | 更强的 LLM 修正能力 | 中低 | 双 LLM、自然语言证明形式化、延迟高 | 后续实验 |
| CHC/PDR/Spacer | 通用归纳不变式 | 高 | 方法成熟但并非近期新方向；模型抽取成本高 | 作为 checker/后备 solver |
| 再增加 octagon/polyhedra 域 | 低成本线性增强 | 高 | 与 StInGX 重叠明显 | 不单独立项 |

LORIS 的局部推理错误反馈值得后续研究，但首批插件不采用，原因是它要求模型先生成自然
语言证明，再由另一个模型形式化为一阶逻辑步骤。相比 Clause2Inv，依赖、调用成本和错误
归因链都更长，不适合当前尚无统一 SMT/候选 IR 的项目阶段。

## 4. 两个插件共用的基础设施

先实现共享层，避免 recurrence、StInGX 和 Clause2Inv 各自重复抽取循环语义。

### 4.1 结构化循环迁移模型

新增：

```text
src/Analyzer/Invariant/
    loopTransitionModel.h
    loopTransitionModel.cpp
    invariantCandidate.h
    invariantVerifier.h
```

建议的数据结构：

```cpp
struct ScalarStateVar {
    const clang::VarDecl *decl;
    symbolic::Addr address;
    symbolic::Expr entryValue;
    clang::QualType type;
};

struct TransitionBranch {
    symbolic::Expr guard;
    std::vector<symbolic::Expr> nextValues; // 与 variables 一一对应
    analyzer::Path::PathState pathState;
};

struct LoopTransitionModel {
    std::vector<ScalarStateVar> variables;
    symbolic::Expr entryCondition;
    symbolic::Expr loopCondition;
    std::vector<TransitionBranch> continuingBranches;
    std::vector<TransitionBranch> interruptBranches;
};
```

抽取规则：

1. 以 `entryAndCurrentInfo.symbolicLoopEntry` 中唯一 entry path 为基准。
2. 只跟踪循环 entry 已存在的整数/布尔标量；第一阶段排除指针、数组、结构、浮点。
3. 对 `symbolicLoopCurrent` 每条 active path，读取变量当前值作为 simultaneous
   `nextValues`，读取 path conditions 与 loop condition 组成 branch guard。
4. 未修改变量显式写成 `x' = x`，不能从模型中省略。
5. 分支 guard 无法转换时，可在 recurrence 插件中把分支视为 nondeterministic
   over-approximation；SMT verifier 则必须拒绝不完整 guard，不能把 over-approximation
   当成精确 post proof。
6. 模型中的所有 `Expr` 必须属于当前 `ACSLGContext::ExprFactory`。

在 `LoopInfo` 增加：

```cpp
std::shared_ptr<const analyzer::invariant::LoopTransitionModel> transitionModel;
std::vector<analyzer::invariant::VerificationGoal> verificationGoals;
```

新增 `SetScalarTransitionModelPlugin`，注册在 `SetEntryAndCurrent` 之后。模型抽取失败应返回
“unsupported reason”，而不是让整个规格生成失败。

### 4.2 结构化候选

```cpp
enum class CandidateOrigin {
    StingX,
    CFiniteRecurrence,
    EntryBounds,
    LoopGuard,
    UserPostcondition,
    ExternalGenerator
};

struct InvariantClause {
    symbolic::Expr predicate;
    CandidateOrigin origin;
    std::unordered_set<symbolic::SourcePoint> usedPoints;
    std::string diagnostic;
};

struct InvariantCandidate {
    symbolic::Expr predicate;
    std::vector<CandidateOrigin> origins;
    std::size_t astCost;
};
```

要求：

- `predicate` 是布尔 `Expr`，不保存 ACSL 字符串。
- 去重使用 factory 内 canonical node identity；跨 factory 输入先 `importedInto()`。
- ACSL 只在最终候选通过验证后调用 `Expr::getACSL()` 渲染。
- StInGX 的 `buildLoopInvariant()` 需要先返回结构化 predicate，再由现有插件渲染；禁止
  ClauseCombine 插件反向解析 `loop invariant ...` 字符串。

### 4.3 候选 provider

增加轻量 provider 层，而不是依赖循环插件之间的执行顺序：

```cpp
class InvariantClauseProvider {
  public:
    virtual ~InvariantClauseProvider() = default;
    virtual std::vector<InvariantClause>
    collect(const LoopTransitionModel &, const LoopInfo &) const = 0;
};
```

首批 provider：

- `StingXClauseProvider`
- `RecurrenceClauseProvider`
- `EntryAndGuardClauseProvider`
- `PatternClauseProvider`
- `ExternalClauseProvider`（默认关闭）

原 `StInGXPlugin` 与 `CFiniteRecurrencePlugin` 可以复用 provider，但仍保持现有插件注册方式。

### 4.4 统一正确性检查

统一返回：

```cpp
enum class ObligationKind { Initiation, Consecution, ExitPost };
enum class ProofStatus { Proved, Disproved, Unknown, Unsupported };

struct Counterexample {
    ObligationKind obligation;
    std::unordered_map<const clang::VarDecl *, llvm::APSInt> pre;
    std::unordered_map<const clang::VarDecl *, llvm::APSInt> post;
};
```

对候选 `I` 检查：

```text
Initiation:  EntryCondition => I
Consecution: I && LoopCondition && TransitionBranch => I'
ExitPost:    I && !LoopCondition => Goal       （存在 goal 时）
```

基础原则：

- 只有 initiation 和所有 continuing branch 的 consecution 都为 `Proved` 才能输出。
- `ExitPost` 用于目标导向选择；没有 goal 时允许输出已证明的归纳不变式。
- solver 的 `Unknown`、timeout 和 unsupported 均不得视为证明成功。
- 对 C 整数必须明确语义。第一阶段只接受能够证明无有符号溢出的数学整数表达式，或把
  类型上下界加入 obligation；不能在 `int`、数学整数和 bit-vector 语义之间静默切换。

## 5. 点子一：CFiniteRecurrencePlugin

### 5.1 学术依据

CAV 2024 的工作不只为单个变量找递推，而是搜索有界次数的多项式表达式 `q(x)`，使其在
循环分支之间共同满足 C-finite recurrence：

```text
q(x(k+r)) = c1*q(x(k+r-1)) + ... + cr*q(x(k))
```

论文证明，在给定多项式次数 `d` 和递推阶数 `r` 后，满足条件的多项式构成有限个向量
空间的并，并给出两个可高效求解的特殊情形：

1. 一阶递推 `r = 1`。
2. 所有分支 transition 都是线性的情形。

论文原型首先尝试二次模板，找不到非平凡结果后再尝试三次模板。TACAS 2025 的 AISE v2.0
进一步展示了 recurrence invariant 与 symbolic execution/abstract interpretation 组合的
工程价值；其在 SV-COMP 2025 ReachSafety-Loops 类别取得第一。

### 5.2 适用示例

```c
int i = 0;
int y = y0;
int x = x0;
while (i < n) {
    x += y;
    y += 1;
    i += 1;
}
```

StInGX 能表达 `i`、`y` 的线性关系，但不能直接表达典型二次关系。recurrence 插件应能
产生类似：

```c
loop invariant
  2 * (x - \at(x, LoopEntry))
    == i * (2 * \at(y, LoopEntry) + i - 1);
```

另一个目标是分支间变量本身没有统一递推、但某个多项式表达式有统一递推的循环。第一阶段
把 guard 忽略为 nondeterministic 分支，只接受对每个分支 transition 都成立的 recurrence，
因此结果仍然 sound。

### 5.3 内部多项式 IR

新增：

```text
src/Analyzer/Invariant/Polynomial/
    monomial.h
    polynomial.h
    polynomial.cpp
    exactMatrix.h
    exactMatrix.cpp
    exprPolynomialConverter.h
    exprPolynomialConverter.cpp
```

表示建议：

```cpp
using Rational = mpq_class;

struct Monomial {
    llvm::SmallVector<std::uint8_t, 8> exponents;
};

class Polynomial {
    llvm::DenseMap<Monomial, Rational> terms;
};
```

必须支持：

- `+`、`-`、`*`、整数常量、整数常量次幂。
- substitution/composition：`q(p1(x), ..., pn(x))`。
- 规范化：去零项、`mpq_canonicalize()`、公分母和系数 gcd 归一化。
- 精确相等，不允许 `double`。
- 从公开 `LiteralExpr`、`UnaryExpr`、`BinaryExpr` facade 递归转换，不访问内部 node。

### 5.4 MVP 算法

配置：

```text
maxVariables = 8
maxBranches = 16
degrees = [1, 2]，显式启用后可尝试 3
recurrenceOrder = 1
maxMonomials = 120
timeBudgetPerLoop = 200 ms
```

步骤：

1. 从 `LoopTransitionModel` 读取所有 continuing branch 的 simultaneous updates。
2. 将每个 `nextValue` 转成 `Polynomial`；任一变量不可转换则该插件对该循环返回
   unsupported。
3. 枚举总次数不超过 `d` 的单项式基 `B = [1, x1, ..., xn, x1^2, x1*x2, ...]`。
4. 对每个 branch 计算 substitution matrix `M_j`，满足：

   ```text
   B(next_j(x)) = M_j * B(x)
   ```

5. 搜索一阶齐次/非齐次递推：

   ```text
   q(next_j(x)) = lambda*q(x) + c
   ```

   对所有 branch 使用同一个 `lambda` 和 `c`。优先处理 `lambda in {0, 1, -1}` 以及更新
   系数中出现的整数；第二阶段再通过精确特征多项式和有理根定理枚举候选 eigenvalue。
6. 对每个 `lambda` 解所有 `(M_j - lambda*I)` 的公共 nullspace。非齐次项通过扩展常量
   单项式处理。
7. 删除常量解、线性重复解和可由更低次数基线性组合得到的解。
8. 将 recurrence 转为可输出的多项式 invariant：

   - `lambda = 1, c = 0`：直接输出 `q(x) == q(x_entry)`。
   - `lambda = 1, c != 0`：若 `indexInfo` 可给出精确迭代次数 `k`，输出
     `q(x) == q(x_entry) + c*k`。
   - 两个表达式具有相同非零 `lambda`：消去指数项，输出
     `q1(x)*q2(entry) == q2(x)*q1(entry)`。
   - `lambda2 = lambda1^m` 且 `m` 很小：可输出交叉幂关系；设置次数上限避免膨胀。
   - 无法消去 `lambda^k` 时不输出 ACSL；保留 diagnostic 供后续 solver 使用。
9. 将归一化后的 `Polynomial` 重建为 factory-owned `Expr`。
10. 对每个 branch 重新做精确多项式 substitution，验证 `I(next) - I(current) == 0`。
11. 通过统一 verifier 复核 initiation/consecution 后输出。

### 5.5 插件接入

新增：

```text
src/SpecGenerator/recurrenceInvariantPlugin.cpp
tests/unit/Analyzer/polynomial_test.cpp
tests/unit/Analyzer/recurrenceInvariant_test.cpp
tests/unit/SpecGenerator/recurrenceInvariantPlugin_test.cpp
```

插件类型建议使用 `PathInsensitiveLoopInvPlugin`：

- recurrence 是对所有 continuing branch 成立的全局不变式。
- 第一阶段只输出 `acsl`，不改写 post-info。
- 这样可与 StInGX 同时运行，不参与 PS plugin 的 `propose()` 主 post-info 竞争。

注册：

```cpp
REGISTER_ACSL_PLUGIN(CFiniteRecurrencePlugin, "cFiniteRecurrence");
```

先增加实验组：

```cpp
REGISTER_ACSL_GROUP(ExperimentalNonlinearLoopInv,
                    "loopAssigns",
                    "cFiniteRecurrence",
                    "loopVariant");
```

完成 benchmark gate 前不加入默认组。

### 5.6 分阶段实施

#### R0：迁移模型与 Polynomial IR

- 完成 `LoopTransitionModel`。
- 完成 Expr/Polynomial 双向转换。
- 完成精确 substitution、矩阵和 nullspace。
- 不接插件，只做单元测试。

#### R1：确定性 affine/triangular loop

- `r=1`，`degree<=2`，单分支。
- 支持 `lambda=1` 和 index-based inhomogeneous recurrence。
- 输出二次等式。

#### R2：多分支共同 recurrence

- 多个 continuing branches。
- guard 可忽略为 nondeterministic over-approximation。
- 求公共 nullspace，覆盖论文的 expression recurrence 核心场景。

#### R3：几何递推与 degree 3

- 支持相同 eigenvalue 的指数消元。
- 加入精确 eigenvalue 枚举。
- 在预算允许时尝试 degree 3。

#### R4：post-info 增强（可选）

- 仅当 normal exit 条件与 recurrence 可唯一解析变量值时，生成 post memory。
- 否则维持“只输出 invariant”，不制造 Unknown 或覆盖 StInGX post-info。

### 5.7 测试计划

单元测试：

- Expr 到 Polynomial：常量、负数、交叉项、拒绝除法/位运算/浮点。
- substitution：simultaneous assignment，防止把 `x'=y, y'=x` 错做顺序赋值。
- exact matrix/nullspace：有理系数、零空间多基、系数规范化。
- degree/monomial/time budget。

算法测试：

- `x += y; y++` 的二次 invariant。
- `x *= 2; y *= 2` 的交叉乘积 invariant。
- 两个 branch 对单变量无共同递推、但对某个二次表达式有共同递推。
- hash 相同但结构不同的 `Expr` 不混淆。
- unsigned、可能溢出、除法、取模、位运算明确返回 unsupported。
- `break`/`return` 分支不参与 continuing consecution，但应进入 exit/post 检查。

集成测试：

- 生成 ACSL 后运行 Frama-C/WP，证明 invariant establishment 和 preservation。
- 与 StInGX 同时启用时不重复输出等价线性 clause。
- OpenHiTLS 现有测试不发生生成结果回退。

验收门槛：

- 所有输出候选均通过内部 exact check 和 WP。
- 至少建立 10 个 StInGX 无法输出、recurrence 能输出有效不变式的非线性回归样例。
- unsupported loop 不崩溃，不改变现有插件输出。
- 默认 degree 2 时，受支持小循环的插件 P95 时间不超过 200 ms；超过预算只跳过该插件。

## 6. 点子二：ClauseCombineInvariantPlugin

### 6.1 学术依据

Clause2Inv 将“猜完整 invariant”拆成三步：

1. Generate：产生不带 `&&`/`||` 的原子 clause。
2. Combine：把新 clause 与历史 clause/失败候选用 `&&`、`||` 组合。
3. Check：用 SMT 检查 initiation、inductiveness 和 post-condition；失败模型作为
   counterexample，用于过滤候选并决定下一轮关注点。

论文报告其在 316 个线性任务中解决 312 个，在 50 个非线性任务中解决 44 个。该方法对本
项目的价值不在于必须接入 LLM，而在于：

- StInGX、recurrence、index/pattern 已经能产生大量有价值的局部 clause。
- 当前框架缺少将这些 clause 自动组合为析取/合取 invariant 的统一层。
- LLM 可以作为可选 provider，永远不进入 trusted computing base。

### 6.2 插件目标

典型情况：

```c
x = 0;
while (x < n) {
    x++;
}
```

候选 provider 可能分别产生：

```text
x <= n
x == 0
x >= 0
n < 0
```

单个 `x <= n` 可能不满足 initiation。组合器利用 initiation counterexample 将其放宽为：

```text
(x <= n) || (x == 0)
```

再做完整归纳检查，而不是丢弃第一次失败中已经正确的 clause。

### 6.3 SMT 层

建议新增可选 Z3 依赖：

```cmake
option(ACSLG_ENABLE_Z3 "Enable SMT-backed invariant checking" ON)
find_package(Z3 CONFIG QUIET)
```

若发行版不提供 CMake package，再使用 `find_path(z3++.h)` 和 `find_library(z3)`。Z3 提供
C++ API、整数/实数/bit-vector/array theory，并可在后续扩展到 CHC/Spacer。初版只使用
普通 incremental solver，不直接依赖 fixedpoint answer 的不稳定打印格式。

新增：

```text
src/Solver/
    smtExpr.h
    smtSolver.h
    z3ExprConverter.cpp
    z3InvariantVerifier.cpp
```

转换要求：

- `Expr` DAG 节点按 identity memoize，避免共享子树重复翻译。
- 当前变量与 next-state 变量使用不同 symbol namespace。
- `LiteralExpr`、整数算术、比较、逻辑连接先支持。
- C 整数默认按数学整数并显式加入类型边界；需要精确溢出行为的任务使用 bit-vector
  mode，不能混合。
- 除法、余数、移位必须按 C 语义单独实现；未实现前拒绝对应模型。
- 每个 obligation 设置独立 timeout，并支持全 loop 总预算。

### 6.4 Clause 来源

确定性 provider 默认启用：

1. **Entry/guard**：entry path condition、index 上下界、loop condition 的原子比较。
2. **Pattern**：`x == x_entry + step*k`、单调性、类型上下界。
3. **StInGX**：结构化 affine constraints。
4. **Recurrence**：多项式等式和关系。
5. **Goal-derived**：循环后 assertion、调用方 postcondition 或 RTE safety goal 中的原子项。

可选外部 provider：

```cpp
class ExternalClauseProvider {
  public:
    virtual expected<std::vector<InvariantClause>, ProviderError>
    generate(const LoopPromptModel &, FailureAttention) = 0;
};
```

安全边界：

- 默认构建和默认插件组不需要网络或 API key。
- 外部 provider 通过配置的子进程/JSON 协议接入，不把 SDK 绑定进核心库。
- 输出必须是受限 JSON AST，例如 `{"op":"<=", "lhs":..., "rhs":...}`，禁止直接接收
  ACSL/C 字符串并拼接。
- 变量必须来自白名单；禁止函数调用、指针解引用、量词和未声明名字。
- 所有外部 clause 与本地 clause 使用同一 SMT verifier。
- 日志记录 provider、模型标识、prompt hash、原始响应 hash和最终证明状态，但默认不记录
  可能含私有源代码的完整 prompt。

### 6.5 Counterexample-driven 组合

核心状态：

```cpp
struct CombinationState {
    ExprSet clauseStore;
    ExprSet failedCandidates;
    std::vector<Counterexample> counterexamples;
    std::priority_queue<ScoredCandidate> worklist;
};
```

每轮算法：

1. provider 返回新原子 clause，规范化并去除 tautology/contradiction。
2. 把 clause 本身放入 worklist。
3. 与历史 clause/高价值失败候选分别构造 `old && new`、`old || new`。
4. 先在已有 counterexample 上直接求值；不能通过历史反例的候选不调用 SMT。
5. 按以下顺序打分：

   ```text
   已覆盖历史反例数
   > goal clause 覆盖度
   > provider 可信度（exact recurrence / StingX 高于 external）
   > AST 更小
   > 逻辑深度更浅
   ```

6. 对最高分候选执行三类 obligation。
7. 失败时保存 `(ObligationKind, model)`：

   - Initiation 失败：候选过强，优先尝试 `||`。
   - ExitPost 失败：候选过弱，优先尝试 `&&`。
   - Consecution 失败：同时探索 `&&` 和 `||`。

8. 找到完整证明候选后做一次独立 solver context 复核，再返回。
9. 达到预算时返回当前已证明的最强归纳 clause 集；如果一个都没有则 `nullopt`。

必须设置组合边界：

```text
maxAtomicClauses = 64
maxStoredExpressions = 512
maxAstNodes = 80
maxLogicalDepth = 4
maxSolverQueries = 200
solverTimeout = 500 ms/query
loopBudget = 5 s（默认 deterministic 模式建议更低）
```

依赖 `ExprFactory` interning 后，重复组合会自然共享 DAG node，但仍需按 `astCost` 限制搜索，
因为 node 共享不会消除组合数量的指数增长。

### 6.6 Verification goal 获取

Clause2Inv 原算法依赖 loop post-condition，而当前 `LoopInfo` 没有统一 goal。分两阶段处理：

#### C1：inductive-only 模式

- 只检查 initiation/consecution。
- 输出所有非平凡、已证明、互不蕴含的 clause。
- 以较强候选为优先，不允许退化到 `true`。
- 可先用于增强 ACSL/WP 上下文，不宣称已经证明某个下游 assertion。

#### C2：goal-directed 模式

新增 `SetLoopVerificationGoalsPlugin`，提取：

- 循环后紧邻的 `assert`/Frama-C assertion。
- 循环体数组访问、除零、移位等 RTE safety condition。
- 函数 postcondition 中能切片到该循环修改变量的原子条件。
- `indexInfo` 可构成的精确正常退出关系。

每个 goal 保存结构化 `Expr` 和 provenance。组合器只有在
`I && !LoopCondition => Goal` 被证明后才标记为 `goal-complete`。不完整 goal 不应阻止输出
本身有效的 invariant，但必须在 diagnostic 中区分。

### 6.7 插件接入

新增：

```text
src/SpecGenerator/clauseCombineInvariantPlugin.cpp
tests/unit/Solver/z3ExprConverter_test.cpp
tests/unit/Solver/invariantVerifier_test.cpp
tests/unit/SpecGenerator/clauseCombineInvariantPlugin_test.cpp
```

注册：

```cpp
REGISTER_ACSL_PLUGIN(ClauseCombineInvariantPlugin, "clauseCombineInvariant");
```

插件建议为 `PathInsensitiveLoopInvPlugin`：

- 它输出的是所有路径共享的已证明 invariant。
- 第一阶段不生成 post-info。
- 不影响 StInGX 作为 PS 插件生成的路径敏感 post-state。

实验组：

```cpp
REGISTER_ACSL_GROUP(ExperimentalCombinedLoopInv,
                    "loopAssigns",
                    "cFiniteRecurrence",
                    "clauseCombineInvariant",
                    "loopVariant");
```

避免重复输出：当 ClauseCombine 已包含某个 StInGX/recurrence clause 时，最终 emitter 应按
结构蕴含/identity 去重。短期内可配置实验组不直接运行独立 recurrence emitter，只把它作为
provider。

### 6.8 分阶段实施

#### C0：Z3 adapter 与 obligation verifier

- Expr 到 Z3 AST。
- initiation/consecution 检查和 counterexample model。
- 仅支持 QF_LIA；无候选生成。

#### C1：本地 clause + deterministic combiner

- entry/guard/pattern provider。
- `&&`/`||` 组合、历史反例过滤和预算。
- 无 LLM、无 post goal。

#### C2：接入 StInGX 和 recurrence provider

- 重构 StInGX 为结构化 candidate。
- 组合线性、非线性 clause。
- 增加蕴含去重。

#### C3：goal-directed

- 提取 assertion/RTE/postcondition slice。
- 启用 ExitPost obligation。
- 输出 goal coverage diagnostic。

#### C4：可选 external/LLM provider

- JSON schema、子进程 provider、fake provider 测试。
- 默认关闭。
- counterexample attention 只暴露“哪类 obligation 失败”和必要变量赋值。

#### C5：LORIS 风格局部推理反馈（研究项）

- 只有 Clause2Inv 基础版本稳定后再评估。
- 不让自然语言证明进入 trusted path。
- 形式化后的每个 implication 仍由同一 SMT adapter 检查。

### 6.9 测试计划

SMT adapter：

- 每个公开 Expr 运算到 Z3 的语义一致性。
- current/next/LoopEntry namespace 不串位。
- 有符号/无符号边界、溢出模式、除法和余数拒绝策略。
- timeout/unknown 不被当成 proved。

组合器：

- initiation 失败后使用 `||` 放宽。
- post 失败后使用 `&&` 加强。
- consecution 失败时双向搜索。
- 历史 counterexample 能在无 solver query 的情况下剪枝。
- canonical identity 去重。
- 达到 AST、query、时间预算后确定性终止。
- 外部 provider 给出恶意/非法 JSON 时拒绝。

集成：

- Clause2Inv 论文中的线性 motivating example。
- 多 phase/if-else 循环，需要析取 invariant。
- 非线性 clause 由 recurrence provider 提供后完成组合。
- 没有 verification goal 时只输出 inductive invariant。
- 有 assertion/RTE goal 时验证 ExitPost。
- Frama-C/WP 对最终 ACSL 做二次验证。

验收门槛：

- 不输出任何未被 verifier 证明的 candidate。
- deterministic-only 模式在固定 seed/固定 provider 下完全可复现。
- 至少建立 15 个“单一 provider 不完整、组合后可证明”的回归样例。
- 与现有 StInGX 默认组相比，WP 已证明目标数有净增长，且现有已证明目标不回退。
- 默认关闭 external provider 时，不发生网络访问，也不需要凭据。

## 7. 推荐实施顺序

```text
阶段 A：共享 TransitionModel + Candidate IR
    ↓
阶段 B：Polynomial IR + recurrence R1/R2
    ↓
阶段 C：把 StInGX 输出改为结构化 candidate
    ↓
阶段 D：Z3 adapter + obligation verifier
    ↓
阶段 E：deterministic ClauseCombine
    ↓
阶段 F：goal extraction
    ↓
阶段 G：可选 external/LLM provider
```

这样安排的原因：

1. recurrence 在不引入新 solver 的情况下就能先产生明确能力增量。
2. 结构化 StInGX/recurrence candidate 是 ClauseCombine 的高质量本地输入。
3. Z3 adapter 独立测试稳定后，再让反例驱动搜索依赖它。
4. LLM 最后接入，不阻塞本地算法，也不扩大初始 trusted computing base。

## 8. Benchmark 与评估

建立三层 benchmark：

1. **微型算法集**
   - affine、triangular、polynomial、geometric、multi-phase、nondeterministic branch。
   - 每个程序附预期 invariant 和预期 unsupported reason。
2. **学术数据集**
   - CAV 2024 recurrence/PExpr 样例。
   - Clause2Inv 线性与非线性 benchmark（先确认 artifact license）。
   - SV-COMP ReachSafety-Loops 数值子集。
3. **项目真实数据**
   - 当前 OpenHiTLS 集成样例。
   - 现有 sweep 脚本中的 WP 目标。

每次实验记录：

```text
ACSLG 成功率
插件适用/unsupported/timeout 数量
候选 clause 数、组合候选数、solver query 数
生成时间 P50/P95/max
内部 obligation proved/disproved/unknown
Frama-C parse 成功率
WP proved goals / total goals
相对 StInGX baseline 的新增证明与回退
```

默认组启用条件：

- 全量现有测试通过。
- 所有 emitted invariant 均通过独立 WP preservation 检查。
- benchmark 中无已证明目标回退。
- generation timeout 有硬上限。
- unsupported C 语义不会被静默近似后输出。

## 9. 主要风险与控制

### 9.1 多项式规模爆炸

单项式数量为组合数 `C(n+d, d)`。必须同时限制变量数、次数、分支数、单项式数和时间。先按
dependency slice 选择与 loop condition/goal/modified variable 有关的变量，再构造模板。

### 9.2 C 整数与数学整数不一致

这是最高 soundness 风险。候选中的乘法可能在 C 执行时溢出，但 ACSL 数学表达式本身不
溢出。计划要求：

- transition 按 C 语义建模；
- 只有在操作定义且无 UB 的路径上证明 consecution；
- 必要时加入类型范围和 RTE 前提；
- bit-vector 候选不能未经证明直接打印成数学整数等式。

### 9.3 分支 over-approximation

recurrence 对所有 branch update 都成立时，忽略 guard 是 sound 但可能变弱。ClauseCombine
的 post proof 需要精确 guard；无法精确转换时应返回 unsupported。

### 9.4 Solver 非完备性

Z3 对 nonlinear integer arithmetic 可能返回 unknown 或超时。策略是：

- recurrence 候选先用精确多项式 normalization 证明；
- SMT 负责组合和目标检查；
- unknown 不输出；
- 可选尝试不同 tactic，但不能把随机结果当作证书。

### 9.5 LLM 不确定性和代码隐私

external provider 默认关闭。即使启用，LLM 只生成候选，不参与证明。需要明确配置、超时、
脱敏策略和审计 hash，不能在没有用户配置时上传源代码。

## 10. 最小原型验证结果

验证日期：2026-07-27。验证分支：
`research/numerical-invariant-prototypes`。

本节按提交时间保留阶段性结论。10.5 至 10.7 中的“下一步”和“尚未完成”描述的是当时
快照，后续完成情况以 10.8、本文件顶部完成状态和
`docs/numericalInvariantPrototypeGuide.md` 为准。

本轮验证只判断算法核心能否在重构后的公共 `Expr` API 上成立，不注册正式 ACSL 插件，
不从 `LoopInfo` 自动抽取迁移模型，也不以 Frama-C/WP 结果代替后续集成验收。

### 10.1 提交与公共实验层

| 提交 | 内容 | 结果 |
| --- | --- | --- |
| `8b2efe8` | `TransitionModel`、精确多项式 IR、C-finite recurrence | 可行 |
| `24c2add` | 可选 Z3 adapter、obligation verifier、deterministic clause combiner | 可行 |

两个原型位于独立的 `NumericalInvariantPrototypeLib`。输入只包含公共
`symbolic::Expr` facade；实现和测试均未包含 `Symbolic/detail` 头文件，也未访问 node 或
handle。所有表达式必须属于同一个 `ExprFactory`，branch update 数量必须与状态变量数量
一致。

### 10.2 C-finite recurrence 验证

原型使用 `mpq_class`、稀疏多项式和精确高斯消元，对所有 branch 的
`q(next) = lambda * q(current)` 方程堆叠求共同零空间。当前限制为：

```text
状态变量 <= 4
分支 <= 4
符号总数 <= 8
总次数 <= 2
单项式 <= 64
输出候选 <= 128
候选 lambda 为 0、1、-1 和迁移式中绝对值不超过 16 的整数系数
```

实测结果：

1. 对 `x'=x+y, y'=y+1, i'=i+1` 找到包含 `x` 的二次 `lambda=1`
   recurrence，并生成 `q(current) == q(entry)` 形式的公共 `Expr` invariant。
2. 对两个分支 `x'=2x`/`x'=-2x`、共同更新 `y'=4y+3`，先得到共同
   `lambda=4` 空间的基 `x^2` 和 `y+1`，再在有界两两组合中实际产出
   `x^2+y+1`。
3. 对 `x'=2x` 与 `x'=3x` 不输出错误的共同非平凡 recurrence。
4. 除法返回 `UnsupportedExpression`；三次更新在 degree-2 配置下返回
   `LimitExceeded`，没有截断高次项。
5. 重复运行的规范化系数、候选顺序和 invariant 结构一致。

结论：**算法原型可行**。它证明当前 facade 足以承载精确的表达式级 recurrence 分析。
进入正式插件前仍需解决从符号执行状态构造 branch update、C 整数溢出/UB 条件、变量
dependency slicing，以及更一般的特征值和 recurrence order。

### 10.3 Clause2Inv 本地组合验证

本机使用 Z3 4.8.12。adapter 支持整数算术、比较、布尔连接和 branch next-state
substitution，分别检查：

```text
Pre => I
I && LoopCondition && BranchGuard => I'
I && !LoopCondition => Post
```

原型固定限制为 16 个原子 clause、128 个组合、64 次 solver query、单次 250 ms。
`unknown`、超时、预算耗尽和不支持表达式都不会被视为证明。失败 query 返回 obligation、
branch index 和模型值；后续候选先在历史 counterexample 上直接求值，不修复反例的候选
不再调用 solver。

实测结果：

1. 对 `x=0; while (x<n) x++`，确认 `x<=n` 单独在 `n<0` 初态失败，并从
   `x<=n`、`x==0` 合成且证明 `(x<=n)||(x==0)`，进而满足
   `n>=0 ==> x==n` 的 exit-post obligation。
2. 构造了两个原子分别不足以推出 post、只有合取后可证明的样例，成功选择合取候选。
3. 两分支样例能准确报告第二个 branch 的 consecution 反例。
4. 零 query 预算返回 `BudgetExceeded`，含除法的 invariant 返回 `Unsupported`。
5. 极低 Z3 resource limit 稳定返回 `Unknown`，候选不被接受。
6. 同一输入重复执行时，候选、尝试数、query 数和最终结构一致。

结论：**本地 deterministic Clause2Inv 原型可行**。外部 LLM provider 不是算法可行性的
前置条件。正式接入前需要实现 `LoopInfo`/symbolic path 到 `TransitionModel` 的提取、
结构化 provider 接口和 C 类型语义；当前结果不等价于已经通过 WP 的生产级 invariant。

### 10.4 构建、测试与候选决策

- Z3 开启：完整 CTest 共 219 个条目通过；其中新增 5 个 recurrence 测试、6 个有效 Z3
  测试，solver-availability 测试在 Z3 已启用时按设计跳过。
- Z3 关闭：使用 `-DACSLG_ENABLE_Z3_PROTOTYPE=OFF` 的全新构建目录成功配置，并成功构建
  `NumericalInvariantPrototypeLib` 和 `ACSLG`；SMT API 返回 `SolverUnavailable`。
- 现有插件注册和默认插件组未修改。

两个首选点子均满足“公共 Expr、非平凡样例、精确/SMT 检查、错误候选拒绝、有界且确定、
全量测试通过”的可行性门槛，因此没有启动 SETTA 2024 模运算候选或 Spacer CHC/PDR
备选。下一阶段应先实现共享迁移模型抽取与 C 语义约束，再把这两个原型提升为 candidate
provider/verifier；不能直接把实验库注册为默认插件。

### 10.5 Clause2Inv external provider 与反馈循环

实现日期：2026-07-28。

| 提交 | 内容 | 结果 |
| --- | --- | --- |
| `b8dd7e5` | 忽略 `.acslg-local/` 本地凭据目录 | 凭据文件权限为 `0600`，不被 Git 跟踪 |
| `5e3c0fc` | 结构化 clause provider、OpenAI-compatible client、curl transport | fake transport 与真实 DeepSeek API 均通过 |
| `d2b3932` | 有总预算的 generate-combine-check 反馈循环 | fake provider 两轮反馈与真实端到端证明均通过 |

外部 provider 只接收稳定编号后的迁移模型：

```text
当前状态变量：v0, v1, ...
循环入口符号：e0, e1, ...
参数：p0, p1, ...
Pre、LoopCondition、每个 branch guard 和 next value、可选 Post
```

响应不是自由格式 ACSL，而是受限 JSON AST。每个候选必须只有一个比较根节点
`eq/ne/lt/le/gt/ge`，算术子树只允许整数、已知符号、`neg/add/sub/mul`。解析器使用
LLVM JSON API，严格拒绝额外字段、未知符号、布尔组合、超深 AST、过多节点和过多
clause；成功解析后只通过公共 `symbolic::Expr` facade 构造表达式。

`OpenAICompatibleClauseProvider` 当前支持以下环境配置：

```text
ACSLG_LLM_API_KEY
ACSLG_LLM_BASE_URL
ACSLG_LLM_MODEL
```

本地实验配置位于被忽略的 `.acslg-local/deepseek.env`，文档和提交均不包含凭据。
HTTP transport 不把 Authorization header 放入 curl argv；header 通过子进程标准输入传入，
请求和响应临时文件使用 `0600` 权限并在调用后删除。provider 默认要求 HTTPS，限制单次
超时、响应大小和输出 token。DeepSeek V4 默认思考模式容易消耗短响应的 token 预算，
本原型显式使用非思考模式完成结构化候选生成。

反馈循环 `synthesizeInvariantClauses` 的执行顺序为：

1. 先组合并验证本地 clause；本地结果足够时不访问网络。
2. 调用 provider，累积并按结构相等去重原子 clause。
3. 使用剩余的全局 Z3 query 预算执行组合与 obligation 检查。
4. 仅把最后一个 `Refuted` 结果的 obligation 类别和 branch index 用于下一轮提示。
5. `unknown`、unsupported、transport/JSON 错误、跨 factory 表达式或预算耗尽立即停止，
   不输出 invariant。
6. 只有 `Pre => I`、每个 branch 的 consecution 和可选 exit-post 均为 `unsat` 时返回结果。

验证结果：

- fake provider 第一轮给出 `x<=n`，SMT 报告 initiation 失败；第二轮给出 `x==0`，组合器
  证明 `(x<=n)||(x==0)`。
- 本地 clause 已能证明目标时 provider 调用次数为 0。
- solver query、provider call、round、atomic clause 和 JSON AST 预算均有独立测试。
- opt-in 真实 API 测试使用 `deepseek-v4-flash` 对合成循环生成可解析 clause。
- opt-in 真实端到端测试将 DeepSeek clause 送入组合器，找到 invariant 后又调用独立
  `verifyInvariant` 再次证明；LLM 输出本身从不进入 trusted path。
- Z3 开启时完整 CTest 为 231/231；Z3 关闭时
  `NumericalInvariantPrototypeLib` 与 `ACSLG` 均成功构建。

本阶段仍未完成生产插件集成。下一步是从 `LoopInfo`、符号执行路径和循环入口/出口状态
构造 `TransitionModel`，明确 branch completeness 与 C 整数/UB 语义，再将已证明的
`symbolic::Expr` 转换为 ACSL `loop invariant`。在此之前 external provider 保持 opt-in，
不加入默认插件组。

### 10.6 实际符号执行状态到迁移模型

实现日期：2026-07-28。

| 提交 | 内容 | 结果 |
| --- | --- | --- |
| `a64ce6d` | 从 `Path`、`ProgramState` 和 `LoopInfo` 产物提取 `TransitionModel` | 受限数学整数原型可行 |

新增的 `extractTransitionModel` 直接消费 `SetEntryAndCurrent` 生成的三类快照：

- 循环入口的具体值，用于 `TransitionVariable::entry` 和 precondition；
- 重符号化后的循环入口，用于 `TransitionVariable::current`；
- 执行一次 condition/body/increment 后的所有 active path，用于 guarded branch 和
  `nextValues`。

提取器按源位置和变量名稳定排序声明，检查所有表达式来自同一个 `ExprFactory`，并只通过
公共 `Expr` facade 遍历表达式。当前更新和 guard 支持字面量、当前状态符号、一元正负号/
逻辑非、加减乘、比较和逻辑连接；未知值、外部内存、除法、缺失变量、inactive path、
变量或分支超限均返回带原因的失败状态，不生成部分模型。

机器整数语义是显式 soundness gate：

```text
默认：RejectMachineIntegers
仅测试/研究：MathematicalIntegersForPrototype
变量上限：8
分支上限：16
```

因此，现有 C `int` 循环在默认配置下不会进入 SMT 或 LLM。只有测试显式选择数学整数原型
模式时才允许抽取；后续必须补充 machine integer、溢出、UB 和类型范围编码，才能让正式
插件启用该路径。

验证结果：

1. 从实际 `for` 循环提取 `i`、`x` 的单分支更新，并由 Z3 证明
   `i >= 0 && x == i`。
2. 从含 `if/else` 的 `while` 循环保留两个 guarded branch。
3. 默认整数模式明确返回 `Unsupported`。
4. 含除法更新的循环在原型支持集之外，明确返回 `Unsupported`。
5. Z3 开启时完整 CTest 为 235/235；Z3 关闭时原型库和主程序均成功构建。

这一步消除了“算法只接受手工构造模型”的主要集成缺口，但还没有生成 ACSL，也没有注册
插件。下一阶段先建立“已证明候选到 ACSL”的受控输出，保留变量声明和源点映射；之后再
增加 opt-in 插件壳，并继续保持默认插件组不变。

### 10.7 受控输出与 opt-in 插件链路

实现日期：2026-07-28。

| 提交 | 内容 | 结果 |
| --- | --- | --- |
| `1ee6b8f` | 将完整 SMT obligation proof 与 ACSL 输出绑定 | 未证明候选无法获得 ACSL |
| `a71cad4` | 组合提取、合成、预算预留、独立复证和输出 | 实际循环状态端到端通过 |
| `ce1c00b` | 注册非默认 Clause2Inv prototype 插件和专用 group | fake、禁用态和真实 API 均通过 |

`verifyAndEmitLoopInvariant` 是候选进入输出的受控边界。它重新检查 initiation、每个 branch
的 consecution 和可选 exit-post obligation；只有 `VerificationStatus::Proved` 才调用
公共 `Expr::getACSL`，返回 `loop invariant ...;` 和 used source points。以下状态均不含
ACSL 字符串：

```text
Refuted
Unknown
Unsupported
BudgetExceeded
InvalidModel
SolverUnavailable
FormattingError
```

`runClause2InvPrototype` 把 transition extraction、clause synthesis 和受控输出组合为单一
调用。它预先按 `1 + branch_count + has_postcondition` 从总 solver query 预算中保留最终
独立证明所需额度；剩余预算不足以同时覆盖 synthesis 和 emission 时，在 provider 调用前
返回 `BudgetExceeded`。因此二次证明不会突破声明的 64-query 总预算。

SpecGenerator 注册了 `Clause2InvPrototypePlugin`，但四个原有默认 loop-invariant group
均不包含它。显式实验 group 为：

```text
Clause2InvPrototypePathInsensitiveLoopInv
```

该 group 保留原默认 path-insensitive 插件并追加 Clause2Inv。本阶段的实现要求同时设置：

```text
ACSLG_ENABLE_CLAUSE2INV_PROTOTYPE=1
ACSLG_ASSUME_MATHEMATICAL_INTEGERS=1
```

第一个开关允许 external provider，第二个开关明确接受当前原型把 C `int` 当作数学整数的
限制。缺少任一开关时插件不访问网络、不输出候选。provider 凭据仍只从
`ACSLG_LLM_API_KEY` 等环境变量读取，本地文件继续位于被 Git 忽略的目录。

这一双开关约束已被 10.8 的精确机器整数阶段替代；当前代码不再读取
`ACSLG_ASSUME_MATHEMATICAL_INTEGERS`。

新增验证覆盖：

1. 使用真实 `LoopInfo` 和 fake provider 完成
   `Path/ProgramState -> TransitionModel -> synthesis -> independent proof -> ACSL`。
2. 默认机器整数 gate 在 provider 调用前拒绝；最终证明预算不足时同样不调用 provider。
3. refuted、unsupported 和 zero-query 候选均不能生成 ACSL。
4. 插件只存在于显式 prototype group；未 opt-in 时返回空结果。
5. 使用本地 DeepSeek 配置运行真实插件测试，模型生成的 clause 经 Z3 复证后成功输出
   `loop invariant`。
6. Z3 开启时完整 CTest 为 244/244；Z3 关闭时
   `NumericalInvariantPrototypeLib` 与 `ACSLG` 均成功构建。

截至 10.7 的实现完成了 Clause2Inv 点子的最小框架集成，但仍缺少 C machine integer/
overflow/UB 编码、exit postcondition 自动抽取和 Frama-C/WP 验收。后续精确整数工作记录
在 10.8；即使补齐该受限子集，研究 group 仍不应并入默认配置。

### 10.8 受限 C 机器整数与 definedness proof

实现日期：2026-07-28。

| 提交 | 内容 | 结果 |
| --- | --- | --- |
| `29ac154` | 为 verifier 和 extractor 增加精确机器整数模式 | Bool、32 位有符号/无符号子集可证明 |
| `0a32b4c` | Clause2Inv 插件切换到精确模式 | 删除数学整数确认开关，真实 DeepSeek 链路通过 |
| `005ef95` | 使用 LLVM APInt 修复常量折叠的宿主 C++ 溢出 | 溢出、除法和移位边界不再触发宿主 UB |
| `23b68ce` | 保留 AST 运算结果和更新 computation type | 无符号 `++`、`+=` 可被真实循环提取 |
| `e0ab7c8` | 编码 C 除法和余数 | 向零截断、除零和 `INT_MIN / -1` 均有测试 |
| `b6a1cc6` | 编码受限 C 移位 | 无符号移位和可证明安全的 signed 左移可用 |
| `4f271d4` | 编码 C 位运算 | `~`、`&`、`\|`、`^` 通过定宽 bit-vector 精确验证 |
| `d589740` | 新增不可变且 interned 的 `CastExpr` | 转换目标类型进入 DAG identity、导入和 substitution |
| `26409c4` | 在符号执行中保留显式整数/布尔转换 | 隐式转换仍沿用类型标注，避免影响旧插件 |
| `d016789` | 为精确 verifier 和 extractor 增加 cast 语义 | Bool、Int32、UInt32 的受限转换通过真实 AST 链路 |
| `ce53b98` | 保留受支持的隐式整数转换 | 整数提升和 Int32/UInt32 usual conversion 通过全量回归 |
| `0c24ac3` | 验证显式提升后的 8/16 位更新 | portable signed 回写和 unsigned 模回写通过 |
| `1b3c436` | 从真实 AST 提取窄整数普通赋值 | signed/unsigned 16 位循环端到端证明通过 |
| `a4896c0` | 保留窄整数自增和复合赋值的转换 | 提升、运算、回写 DAG 通过真实循环和 UB 验证 |
| `43261a8` | 为字面量 facade 增加精确整数值访问 | `INT64_MIN`/`UINT64_MAX` 无符号信息不丢失 |
| `429931a` | 验证同类型 64 位整数迁移 | UInt64 模回绕、Int64 溢出和真实循环通过 |
| `641df81` | 验证 64 位移位迁移 | UInt64 左值配 Int32 count 的真实循环通过 |
| `2ceed0c` | 保留标量 32/64 位隐式转换 | Int32→Int64/UInt64 的真实 AST 与模语义通过 |
| `b2a301d` | 删除结构成员算术的 cast 兼容例外 | 结构字段参与 usual conversion 时保留精确转换 |
| `aa0c5e9` | 选择性线性化保值整数 cast | PPL 保留系数，模/窄化转换继续拒绝 |
| `87d23ba` | 修复移动后指针的数组下标访问 | lvalue/rvalue 都先求值基址并累加 index |
| `d55a78c` | 覆盖移动指针后的负下标 | `p+1` 后 `p[-1]` 正确归一化回对象基址 |
| `d20fba6` | 在路径内记录 pointer object extent | offset、复制、跨 factory 和分支合并语义通过 |
| `c4f3e9b` | 从固定数组类型推导 object extent | 常量数组元素数进入路径状态并随作用域清理 |
| `7494c6c` | 保留 `BSL_SAL_Calloc` 的元素数量 | 标量和结构分配的符号/常量 extent 通过 |
| `00f9fbe` | 从精确 `BSL_SAL_Malloc` 字节数推导 extent | 符号乘法可用，非整元素字节数保持 unknown |
| `6ab4093` | 在符号路径记录 pointer access bounds | 已知边界生成条件，未知边界显式标记 unsupported |
| `8e05cdc` | 在 transition verifier 证明 pointer bounds | 固定数组循环通过，越界/未知 extent 不会被接受 |
| `ae26e42` | 在 `free` 后失效 allocation extent | use-after-free 和 double-free 显式变为 unsupported |
| `a477e90` | 区分 pointer formation 与实际访问边界 | one-past 可形成，超过 one-past 或解引用 one-past 不会被接受 |
| `94f2722` | 补齐复合赋值和自增的 pointer formation | `+=`、`-=`、`++/--` 均记录 one-past obligation |
| `bdeaeeb` | 保守处理 null pointer 读取 | `*p`、`p[i]`、`p->field` 返回 unknown 并标记 unsupported |
| `63d8604` | 保守处理 null pointer 写入 | 无法求值的 lvalue 使用内部占位地址继续分析，但禁止不变式提取 |
| `5860cfa` | 检查 pointer pair provenance | 差值和关系比较要求同一有界对象，跨对象相等比较仍允许 |
| `162e062` | 将普通循环主入口接到 opt-in group | 环境变量为 `1` 时真实 ACSLG 路径执行 Clause2Inv |

`TransitionModel::integerSemantics` 现在区分数学整数与 C 机器整数。精确模式把受支持的
C 整数编码为 Z3 Int 加范围约束，并为每个可能执行的表达式同时生成 value 和 definedness：

```text
signed int:   -2^31 <= x <= 2^31-1
unsigned int: 0 <= x <= 2^32-1
unsigned +,-,*: 结果按 2^32 取模
signed +,-,* 和一元负号: 结果必须仍在 signed int 范围内
signed /,%: 向零截断；除数非零且排除 INT_MIN/-1
shift count: 0 <= count < 32
unsigned <<,>>: 精确结果；signed << 还要求左值非负且结果可表示
~,&,|,^: Int 与定宽 bit-vector 间往返，结果按操作数 signedness 解释
int -> bool: 与零比较
bool -> int/unsigned: 映射为 0 或 1
signed int -> unsigned int: 按 2^32 取模
narrow signed -> int32: 保值提升
int32 -> narrow signed: 仅结果可表示时视为 portable-defined
int32 -> narrow unsigned: 按目标位宽取模
unsigned 64-bit arithmetic: 结果按 2^64 取模
signed 64-bit arithmetic: 结果必须仍在 [-2^63, 2^63-1]
```

验证顺序增加了 loop condition definedness 和每个 branch guard/update definedness
obligation。只有 initiation、全部 definedness、consecution 和可选 exit-post 都证明为
`unsat`，候选才允许输出；有符号溢出会返回 `Definedness` 失败，而不是被当作数学整数
继续证明。pipeline 会为这些额外查询预留总预算。

真实 AST 路径现在保留 Clang 已完成的整数类型判断：

- 普通一元/二元运算保存 AST 结果类型；
- 显式整数/布尔 cast 保存为独立的不可变 `CastExpr`，而不是修改共享节点的结果类型；
- Clang `IntegralCast`、`IntegralToBoolean` 和布尔到整数转换在目标为 Bool 或 8/16/32/64 位
  整数时保存为 `CastExpr`；`LValueToRValue` 和 `NoOp` 不生成节点；
- 隐式常量转换直接生成目标类型字面量，避免初始化值携带无意义 cast；
- `++/--` 使用 Clang 的 integer promotion 类型构造“提升旧值、加减 1、转换回原类型”；
- 复合赋值使用 `CompoundAssignOperator` 的 computation type，通过 `CastExpr` 提升操作数并
  转换回左值类型；
- 32 位无符号自增和复合赋值按 `2^32` 取模；窄 signed 安全回写可证明，越界回写触发
  `Definedness` 失败；窄 unsigned 回写按目标位宽取模。
- `LiteralExpr::integerValue()` 通过 `int64_t | uint64_t` 精确公开整数值，Z3 翻译不再将
  `UINT64_MAX` 窄化为负数；
- 同类型 Int64/UInt64 算术、比较、除余和位运算进入精确模型，真实
  `unsigned long long += 1ULL` 循环可被提取并验证。
- 移位左右操作数按 C 规则独立完成整数提升；32/64 位左值可配 32/64 位计数，并按左值位宽
  检查 `0 <= count < width`，真实 `unsigned long long <<= int` 循环通过。
- 标量隐式转换目标扩展到 Int64/UInt64；Clang 已降低的 32/64 位 usual conversion 保存为
  `CastExpr`，负 Int32 转 UInt64 的真实循环按 `2^64` 取模并完成证明；
- 隐式常量转换使用精确 integer variant，64 位 mask、最小值和最大值计算不触发宿主 C++ UB。

转换节点已经贯通 factory interning、跨 factory 导入、值/路径/range substitution、ACSL
输出、符号收集和表达式大小预算。真实 AST 回归用例同时包含 `(_Bool)x` 循环条件和
`(unsigned)x` 更新：数学整数原型模式明确拒绝该模型，精确机器模式保留两个 cast，并由
Z3 完成 initiation、definedness 和 consecution 证明。

当前 sound 子集仍有明确限制：

- 真实 transition extractor 接受 Bool 和 8/16/32/64 位整数；窄整数更新必须在 AST 中明确
  呈现为提升到 Int32、执行计算、再转换回变量类型；
- verifier 可对 8/16 位叶节点施加精确范围，并验证已经显式降低为“提升、32 位计算、回写”
  的模型；直接在窄类型上进行一元、二元、比较或位运算仍返回 `Unsupported`；
- 支持已经由 Clang AST 完成类型转换的加、减、乘、除法、余数、比较、逻辑连接，以及独立提升
  shift count 的无符号左右移、受限 signed 左移和同类型操作数的 `~`、`&`、`|`、`^`；
- 数学整数模式明确拒绝移位和位运算，不会把它们近似为普通整数算术；
- signed 右移具有实现定义语义，当前明确返回 `Unsupported`；
- `UInt32 -> Int32` 仍明确返回 `Unsupported`；有符号窄化只接受可证明落在目标范围内的
  portable-defined 情况，否则在 `Definedness` obligation 中拒绝；
- Clang 隐式整数转换只对 Bool 和 8/16/32/64 位整数目标物化。一次不加筛选的全量切换实验
  曾造成 9 项回归失败；排除非数值 cast、归一化隐式常量并补齐精确宽度处理后，完整回归恢复；
- 结构字段参与 usual conversion 时不再绕过 `CastExpr`。结构集成测试明确检查
  Int32→UInt64 转换，避免把负值可能发生的模转换误当成数学整数恒等映射；
- PPL 只透明线性化对全部源值都保值的转换：signed/unsigned 同符号扩宽，以及目标 signed
  位宽更大的 unsigned→signed。signed→unsigned、窄化和同宽 unsigned→signed 继续拒绝；
- PPL 无符号字面量直接构造任意精度 `Coefficient`，不再让 `UINT32_MAX` 经宿主 `int`
  窄化成 `-1`；通用简化器也不会借线性重建删除 cast 或改变运算结果类型；
- 数组 lvalue/rvalue 都通过 `evaluatedAddress()` 解析 `p+i`、`p++` 等地址表达式，并把
  下标累加到已有 symbolic offset；移动指针后的写入和读取已有端到端回归；
- 有符号负下标在最终地址仍位于对象内时可用：回归覆盖 offset 1 加 index -1 后访问
  offset 0。MemoryModel 只检查归一化后的最终常量 offset；
- symbolic address 的 root 已提供 allocation provenance，`MemoryModel` 另以
  `SymbolAddrBaseInfo` 为键保存可选的元素数量。extent 跟随同一 base 的不同 offset，
  copy/assignment 会导入目标 factory，`clear()` 会清除，路径合并只保留两边结构相同的
  extent；它不进入 address DAG identity，因为同一来源的动态分配在不同路径可以有不同大小；
- 固定数组声明会从 `ConstantArrayType` 精确取得元素数并写入 extent，局部数组离开作用域时
  同步清理；`BSL_SAL_Calloc(count, sizeof(T))` 会为标量和结构分配保存精确的符号
  `count`。`BSL_SAL_Malloc` 对单字节元素、可整除常量字节数以及精确
  `count * sizeof(T)` 保存 extent；无法精确换算的字节表达式保持 unknown；
- 数组访问、指针解引用和 `->` 会在 `Path` 上独立记录
  `0 <= offset && (unsigned)offset < extent`，不把 definedness 错当作路径假设。clone 保留
  obligation，merge 取并集；参数指针等未知 extent 设置显式 unsupported 标志；
- 产生指针结果的加减法和 `++/--` 会另外记录
  `0 <= offset && (unsigned)offset <= extent`。因此对象末尾 one-past 指针可以形成，但实际
  解引用仍受严格 `< extent` 约束；超过 one-past 的循环候选会在 `Definedness` obligation
  被反驳；
- 普通和复合指针加减、自增、自减共用 formation obligation；复合赋值同时修复了左值求值
  分叉、右值不分叉时的路径所有权问题；
- null 或其他无法求值为地址的 `*p`、`p[i]`、`p->field` 读取不再终止分析，而是返回
  `UnknownExpr` 并设置 unsupported memory 标志。对应写入使用仅供状态机继续执行的内部
  占位地址，同样设置该标志；transition extractor 会在调用 solver/provider 前拒绝路径；
- 指针差值和 `<`、`>`、`<=`、`>=` 会比较 `SymbolAddrBaseInfo` provenance，并要求两端
  都满足 one-past formation 边界。不同 allocation、无法求值地址或未知 extent 均标记
  unsupported；`==`、`!=` 不要求同源；
- 每个 `TransitionBranch` 携带已排序的 memory definedness conditions。extractor 拒绝未知
  extent、非当前标量依赖和非 Bool 条件；Z3 在 branch guard 下同时证明条件计算本身 defined
  且 bounds 成立。固定数组循环已完成真实 AST 抽取和证明，错误候选返回 `Definedness`；
- `BSL_SAL_Free` 只接受仍有 extent 且 offset 精确为零的 allocation base；成功释放会删除
  extent。释放后访问、double-free、interior pointer free 和未知来源 free 都设置 unsupported
  memory-safety 标志，不会进入 invariant 验证；
- 同宽 UInt64→Int64 等可能产生实现定义结果的转换仍返回 `Unsupported`；
- 没有自动抽取 exit postcondition；选定 benchmark 已有 Frama-C/WP 验收，但尚无全 corpus
  覆盖率和性能统计。

插件只在 `ACSLG_NUMERICAL_INVARIANTS=llm` 时访问 external provider，并且不属于默认
group；旧开关仅保留兼容。本阶段使用真实 DeepSeek 配置完成全插件链路。最近一次 Z3
开启的完整 CTest 为 329/329；Z3 关闭时 `ACSLG` 和 `test_all` 均成功构建。

当前原型目标到此完成。实现定义整数行为、自动 exit postcondition 和全 corpus
Frama-C/WP 统计是独立的生产化工作；在引入明确 target policy 和新验收标准前，现有
`Unsupported` 行为应保持不变。参数指针没有长度信息时继续保持 unknown extent，不得假定
任意 offset 已定义。

## 11. 插件与 Benchmark 集成结果

实现日期：2026-07-30。

| 提交 | 内容 | 结果 |
| --- | --- | --- |
| `c4b581f` | 将 verified polynomial 发现器接入普通循环插件组 | `local`/`llm` 模式可从真实 `LoopInfo` 生成并复证候选 |
| `b106a68` | 增加真实 benchmark、来源标签和 Frama-C/WP 验收 | Code2Inv 2/120 与 FIB 30 自动通过，非线性标签由 WP/Z3 证明 |
| `3d8fe76` | Clause2Inv 拒绝只依赖未变化状态的平凡候选 | 分支 benchmark 3 输出 y 单调关系，而非 z 的恒等入口关系 |

统一入口为 `ACSLG_NUMERICAL_INVARIANTS=local|llm`。默认不改变旧行为；旧的
`ACSLG_ENABLE_CLAUSE2INV_PROTOTYPE=1` 仅作为兼容入口。自动 benchmark 测试运行两次检查
确定性、验证 disabled 对照、调用 Frama-C 解析，并由 WP 精确选择
`acslg_polynomial` 标签。真实 DeepSeek 还在 Code2Inv 120 和分支样例 3 上产生了经项目
verifier 与 Frama-C/WP 双重确认的 ACSL。

详细可复现结果见 `docs/numericalInvariantBenchmarkReport.md`。仍未完成的是全 corpus
统计和 exit postcondition 自动提取。

## 12. 参考资料

1. Chenglin Wang, Fangzhen Lin. **On Polynomial Expressions with C-Finite Recurrences in
   Loops with Nested Nondeterministic Branches**. CAV 2024.  
   <https://doi.org/10.1007/978-3-031-65627-9_20>
2. Yao Lin, Zhenbang Chen, Ji Wang. **AISE v2.0: Combining Loop Transformations**.
   TACAS 2025 competition contribution.  
   <https://zbchen.github.io/files/tacas2025.pdf>
3. Weining Cao et al. **Clause2Inv: A Generate-Combine-Check Framework for Loop Invariant
   Inference**. ISSTA 2025.  
   <https://doi.org/10.1145/3728920>
4. Tianchi Li et al. **Guiding LLM-based Loop Invariant Synthesis via Feedback on Local
   Reasoning Errors**. TOPLAS 2026.  
   <https://doi.org/10.1145/3806652>
5. Jingyu Ke et al. **Affine Disjunctive Invariant Generation with Farkas' Lemma**.
   VMCAI 2025（当前 StInGX 方法背景）。  
   <https://doi.org/10.1007/978-3-031-82700-6_9>
6. Z3 Guide. **Fixedpoints / Generalized PDR with SPACER**（后续 CHC 扩展基础）。  
   <https://microsoft.github.io/z3guide/docs/fixedpoints/engineforpdr/>
7. ACSL language overview and Frama-C examples.  
   <https://frama-c.com/acsl.html>
