# Project Architecture（整体框架概览）

本文档覆盖 ACSLGen 的架构与数据流，帮助读者理解从 `compile_commands.json` 到 ACSL 输出的完整流程。

---

## 1. 系统目标与主流程

ACSLGen 基于 Clang 做静态分析与重写，核心目标是：
- 解析 C/C++ 源码（基于 `compile_commands.json` 的真实编译参数）
- 进行符号执行与规则推导
- 生成 ACSL 合约（函数级 contract、循环的 invariant/assigns/variant 等）
- 将生成结果写回到 `_acsl.c` 输出文件

主流程概览：
1. **Clang Tool 入口**（`src/main.cpp`）：解析参数、构建 ClangTool
2. **AST 与 Context 初始化**（`src/Context`）：建立 SourceManager/Rewriter/函数集合
3. **分析与符号执行**（`src/Analyzer`）：生成 ProgramState、执行路径探索
4. **规格生成**（`src/SpecGenerator`）：按插件生成 ACSL 文本
5. **输出与重写**：将 ACSL 插入源码并写出 `_acsl.c`

---

## 2. 目录与模块布局

- `src/main.cpp`  
  Clang tooling 入口，负责 `-ast-only`、函数过滤与输出命名

- `src/Context`  
  AST 上下文封装、Rewriter 管理、函数收集与插入点管理

- `src/Analyzer`  
  语义分析与符号执行主引擎（ProgramState、PathConditions、跨 TU 支持）

- `src/Analyzer/Symbolic`  
  符号表达式系统、归纳与求值基础设施

- `src/SpecGenerator`  
  规格生成插件系统、模板与调度

- `src/Stingx`  
  约束与线性变换库（用于不变式与线性关系推导）

- `src/Utils`  
  基础工具与公共辅助

其他目录：
- `tools/`：辅助脚本或工具
- `scripts/`：构建/运行/调试脚本（见 `scripts/README.md`）
- `tests/`：测试集合
- `benchmark/`：性能与场景用例

---

## 3. 端到端数据流

1. **输入阶段**  
   - `compile_commands.json` 提供真实编译参数与 include 路径  
   - 用户指定目标源文件与可选的函数过滤参数

2. **AST 构建与预处理**  
   - Clang FrontendAction 解析 TU  
   - 收集函数列表、注释与 SourceManager/ASTContext 信息  
   - 预处理阶段清理旧 ACSL 注释（保留顶层 `requires`）

3. **符号执行与状态构建**  
   - 对每个函数创建 `ProgramState`  
   - 执行语句级步进，维护：
     - MemoryModel（变量/指针/结构体字段）
     - PathConditions（分支条件）
     - ReturnExpr（返回值）
   - 同时生成 preState/postState 供规格生成

4. **规格生成（函数 + 循环）**  
   - SpecGenerator 按 group 编排插件  
   - 函数级：`requires/ensures/assigns`  
   - 循环级：`loop invariant/assigns/variant`  
   - 插件产出 ACSL 文本与 post-state 信息（用于合并与替换）

5. **合并与标签插入**  
   - 合并多路径 post-state  
   - 统一处理 label/SourcePoint 并注入标记

6. **重写输出**  
   - 将 ACSL 插入函数定义前  
   - 输出 `<stem>_acsl.c`

---

## 4. 核心子系统与职责

### 4.1 Clang Tooling 入口层
- **文件**：`src/main.cpp`
- **职责**：
  - 参数解析（函数过滤、AST-only）
  - 注释处理：移除非顶层 `requires` 的旧 ACSL 注释
  - 输出命名与写文件
  - 重写策略：ACSL 注释插入到函数定义前，避免与生成结果冲突

### 4.2 Context（编译上下文封装）
- **文件**：`src/Context/context.h` / `src/Context/context.cpp`
- **职责**：
  - 管理 `ASTContext` / `SourceManager` / `Rewriter`
  - 持有当前分析使用的 `ExprFactory`，统一管理符号 DAG 的生命周期
  - 收集目标函数与插入点
  - 统一写入策略与标签管理

### 4.3 Analyzer（符号执行与函数级分析）
- **文件**：`src/Analyzer/*`
- **职责**：
  - 遍历 TU 中可分析函数
  - 构建 `ProgramState`，执行语句级符号执行
  - 汇总 pre/post 状态，交给 SpecGenerator
  - Path 与 State 解耦，内存模型、路径条件与作用域生命周期分别维护

关键构件：
- `ProgramState`（`src/Analyzer/state.*`）：内存模型、路径条件、局部作用域管理
- `ACSLFunction`（`src/Analyzer/function.*`）：函数封装与元信息
- `analysis.cpp`：分析流程、插入生成结果
- `crossTU.*`：跨 TU 支持与编译数据库集成

### 4.4 Symbolic（表达式与推理基础）
- **文件**：`src/Analyzer/Symbolic/*`
- **职责**：
  - 构建符号表达式与地址模型
  - 提供表达式等价、简化、哈希等操作
  - 为不变式/归纳提供表达式语义基础

Symbolic 对外提供普通值语义的 facade，对内使用不可变、可复用的 node DAG：

```cpp
ExprFactoryScope scope(context.getExprFactory());
LiteralExpr x{10};
LiteralExpr y{20};
Expr sum = x + y;
```

- **公开 facade**
  - `Expr` / `Addr` 是通用表达式和地址值；每个值记录所属 factory 与非空只读 node 指针。
  - `LiteralExpr`、`UnaryExpr`、`BinaryExpr`、`SymbolValueExpr`、`StructureExpr` 提供类型化构造与只读访问。
  - `VariableAddress`、`FieldAddress`、`SymbolAddress` 提供类型化地址访问；offset/length 以 `Expr` facade 暴露。
  - `withType`、`withField`、`withOffset` 等更新操作返回新 facade，不修改原节点。

- **内部 node DAG**
  具体 node、handle、原生 RTTI 与结构相等实现位于 `src/Analyzer/Symbolic/detail/`，不属于外部 API。`ExprFactory` 按“hash bucket + 结构相等”驻留节点，相同结构复用同一节点，hash 冲突不会错误复用。factory 由 `ACSLGContext` 持有，跨 factory 的值必须通过 `importedInto(...)` 显式导入。

- **溯源信息**
  符号值、结构体和符号地址的内部节点记录来源地址与 `SourcePoint`；公开代码通过 facade 的 `sourceAddress()`、`from()`、`fromPoint()` 等只读接口访问。

- **范围与聚合表达式 facade**
  `Expr::rangeIndex(...)` 提供索引占位，`SumOverRangeExpr`、`QuantifierOverRangeExpr`、`MaxMinOverRangeExpr` facade 描述区间聚合。替换操作递归重建受影响路径，并复用其余节点。它们主要在循环不变式生成阶段构造（`src/SpecGenerator/loopInvariantPlugins.cpp`）。

- **线性化与多面体算法**
  `Expr::toLinearExpr(...)` 把可线性化的 facade 转成 PPL 输入。多面体引擎只消费转换结果，不拥有或修改符号 DAG。

- **源点绑定（`SourcePoint`）**  
  负责把语义节点绑定到稳定的源代码标签，供 ACSL 输出与替换使用。

### 4.5 SpecGenerator（规格生成与插件体系）
- **文件**：`src/SpecGenerator/*`
- **职责**：
  - 组织插件与 group
  - 生成函数 contract 与循环规格
  - 将 post-state 合并并转为 ACSL 文本
  - 信息不足时输出保守合约，避免流程中断

下面用一个函数与一个循环的例子说明合约的最终形态。

函数级 contract 由 `functionContractPlugins.cpp` 生成，路径信息与写集合会被归并到 `assigns` 与 `behavior` 中。带分支的函数会形成多个 behavior 与对应的前提/后置条件：
```c
int f(int *p, int x) { if (x > 0) { p[0] = x; return 1; } return 0; }
/*@
  assigns p[0];
  behavior b0:
    assumes x > 0;
    assigns p[0];
    ensures \result == 1;
  behavior b1:
    assumes !(x > 0);
    assigns \nothing;
    ensures \result == 0;
*/
```

循环规格分两层生成：`loopInfoPlugins.cpp` 提取 entry/current 状态、索引变量与线性访问 pattern；`loopInvariantPlugins.cpp` 基于这些信息输出 `loop invariant/assigns/variant`。典型计数循环通常会同时生成写集合、变元和归纳不变式：
```c
for (int i = 0; i < n; i++) { p[i]++; x++; }
/*@
  loop assigns p[i .. n - 1], x, i;
  loop invariant x == \at(x, LoopEntry) + i;
  loop variant n - i;
*/
```
对于搜索或 max/min 模式的循环，插件会引入量词或聚合模板（如 `\forall`、`MaxMinOverRange(...)`），在保持可证明性的同时减少手工注解负担。

核心文件：
- `specGenerator.*`：总调度与输出拼装
- `groups.*`：插件编排
- `functionContractPlugins.cpp`：函数规格生成
- `loopInfoPlugins.cpp` / `loopInvariantPlugins.cpp`：循环规格生成
- `*_Templates.h`：ACSL 文本模板

### 4.6 Stingx（线性关系与不变式工具）
- **文件**：`src/Stingx/*`
- **职责**：
  - 提供线性表达、矩阵、变换与约束管理
  - 为不变式推导提供基础设施
  - 相关算法基于 Farkas' lemma 的线性约束推导，技术细节可参考：
    @inproceedings{ke2025affine,
      title={Affine disjunctive invariant generation with farkas' lemma},
      author={Ke, Jingyu and Fu, Hongfei and Liu, Hongming and Sun, Zhouyue and Chen, Liqian and Li, Guoqiang},
      booktitle={International Conference on Verification, Model Checking, and Abstract Interpretation},
      pages={187--213},
      year={2025},
      organization={Springer}
    }

### 4.7 Utils（公共辅助）
- **文件**：`src/Utils/*`
- **职责**：通用工具函数、简化逻辑与基础数据结构
