# loopInvariantPlugins.cpp 插件说明

本文档介绍 `src/SpecGenerator/loopInvariantPlugins.cpp` 中的各个“循环规格”插件：它们如何从符号执行状态与 `LoopInfo` 中提取信息，并生成 ACSL 的 `loop invariant` / `loop assigns` / `loop variant` 子句，以及用于后续合成 post-state 的辅助信息。

---

## 1. 插件在整体架构中的位置

本项目的循环规格生成大致分两步：

1. **LoopInfo 解析阶段**（见 `src/SpecGenerator/loopInfoPlugins.cpp`）  
   对某个 loop 语句构造 `LoopInfo`，里面包括：
   - loop 的结构信息：init/cond/inc/body AST 指针
   - loop entry/current 的符号状态（symbolic states）
   - 索引信息（indexInfo）：识别出的索引变量、边界、步长、循环次数上界等
   - 变化模式（patternInfo）：哪些地址随循环线性变化（init + step）
   - shared 状态（sharedMemoryMap）：所有 real entry paths 合并后仍能确定的值

2. **Loop invariant/assigns/variant 生成阶段**（本文档主题，见 `src/SpecGenerator/loopInvariantPlugins.cpp`）  
   基于 `LoopInfo`，不同插件负责生成：
   - `loop invariant ...;`
   - `loop assigns ...;`
   - `loop variant ...;`
   同时返回用于构造 post-state 的信息（后续可能影响其它规格生成或内联断言）。

最终在 `src/SpecGenerator/specGenerator.cpp` 的 `emitLoopInvariant(...)` 中：
- 会按组（group）拉取插件并运行
- 将插件返回的子句按类型分类（assigns/invariant/variant）
- 将插件返回的 post-info 做“在真实 entry path 上的替换（substitution）并合并”，形成最终 post-state

---

## 2. Path-insensitive vs Path-sensitive：两类 loop 插件

项目将 loop 规格插件分成两大类（接口见 `src/SpecGenerator/specGenerator.h`）：

### 2.1 Path-insensitive（PI）插件

接口：`PathInsensitiveLoopInvPlugin`  
返回：`PathInsensitiveLoopInvPlugin::GenResultType`

特点：
- 插件产出的结论被视为对所有路径“全局合并”的结果（不区分分支路径）。
- 返回结构里除了 `acsl` 外，还有：
  - `globalNormalPathPostInfo`：正常退出路径的全局 post 信息
  - `globalInterruptPathsPostInfo`：各条中断路径（break/return 等）的全局 post 信息

典型用途：生成 `loop assigns`、`loop variant`，以及一些能整体成立的 invariant。

### 2.2 Path-sensitive（PS）插件

接口：`PathSensitiveLoopInvPlugin`  
返回：`PathSensitiveLoopInvPlugin::GenResultType`

特点：
- 允许区分“正常路径”和“中断路径”，并为不同路径给出不同的 post-state/条件。
- 适合表达“存在/必然”这类与路径密切相关的性质，例如搜索循环：
  - 正常退出：区间内都不命中
  - 中断退出：区间内至少有一次命中

---

## 3. 默认启用的插件组（groups）

插件并不是全部同时启用，而是通过 group 选择。默认组配置在 `src/SpecGenerator/groups.cpp`：

- `DefaultLoopInfo`：`SetEntryAndCurrent`, `setPatterns`, `setSharedState`, `setIndex`
- `DefaultPathInsensitiveLoopInv`：`checkAndDumpLoopInfo`, `loopAssigns`, `paradigmMaxMin`, `loopVariant`
- `DefaultPathSensitiveLoopInv`：`paradigmSearch`, `StInGXPlugin`
- `ComplexPathInsensitiveLoopInv`：`checkAndDumpLoopInfo`, `complexLoopAssigns`
- `ComplexPathSensitiveLoopInv`：`StInGXPluginForComplexLoop`

也就是说：
- 常规循环：会先跑 `DefaultLoopInfo` 填充 `LoopInfo`，再跑默认 PI/PS loop 插件。
- 复杂循环：可能改走 `Complex*` 组，启用更保守/更少依赖的插件。

---

## 4. LoopInfo 中与本文件最相关的字段（速览）

`LoopInfo` 定义在 `src/SpecGenerator/specGenerator.h`。本文件主要使用：

- `entryAndCurrentInfo`（由 `SetEntryAndCurrentPlugin` 填充）
  - `symbolicLoopEntry`：把“loop entry 点”当作符号化起点后的 ProgramState（通常只有 1 条 path）
  - `symbolicLoopCurrent`：对 loop body 执行一轮（cond->body->inc）后的 ProgramState（可能多条 path）
  - `inactivePaths`：在执行一轮过程中变为 inactive 的 path（break/return 等）
  - `loopEntryPoint`：SourcePoint，用于统一标签（LoopEntry）

- `indexInfo`（由 `SetIndexPlugin` 填充）
  - `indexRealAddr / indexSymbolicAddr / indexSymbolicValue`
  - `op / indexBound`
  - `indexPattern`（init + step）
  - `preciseLoopCount / maxLoopCount`

- `patternInfo`（由 `SetPatternsPlugin` 填充）
  - `normalExitPatternsMap`：正常 step-path 上一致的 (addr -> pattern?)
  - `interruptedPathPatternsMaps`：每条中断 path 上的 (addr -> pattern?)
  - `allPatternsMap`：跨所有路径合并后的 pattern（更保守）

- `sharedMemoryMap`（由 `SetSharedStatePlugin` 填充）
  - “所有 real entry paths 合并后仍能确定”的 memory 写集合（用于提高后续推导稳定性）

---

## 5. 本文件插件逐个说明

### 5.1 `CheckAndDumpLoopInfoPlugin`（ID: `checkAndDumpLoopInfo`）

- 类型：PI
- 主要作用：调试/诊断（不生成 ACSL）
- 依赖：无硬依赖（有就打印，没有就提示未设置）
- 输出：
  - `acsl = nullopt`
  - post-info 为空

实现逻辑：
1. 如果 `entryAndCurrentInfo` 存在：打印 `symbolicLoopEntry` 的 dump（并检查它是否只有 1 条 path）
2. 如果 `indexInfo` 存在：打印索引地址、符号值、边界、循环次数等
3. 如果 `patternInfo` 存在：打印 normalExitPatternsMap 中每个地址的 pattern（或太复杂为 nullopt）

效果：
- 让你在日志中看到 LoopInfo 的解析结果，常用于排查：
  - loopInfoPlugins 是否成功识别 index/pattern
  - path 数量是否异常（导致后续插件无法工作）

---

### 5.2 `LinearInvariantPlugin`（ID: `StInGXPlugin`）

- 类型：PS
- 主要作用：基于“索引条件 + 符号执行状态”生成 loop invariant，并返回路径敏感 post-info
- 依赖：`entryAndCurrentInfo` + `indexInfo`
- 输出：
  - `acsl`：可能为 `loop invariant ...;` 的组合（由 analyzer 层生成，可能为空）
  - `normalPathPostInfos` / `interruptPathsPostInfos`：用于后续合成 post-state

核心实现步骤：
1. **依赖检查**：缺少 entryAndCurrentInfo 或 indexInfo 直接报错
2. **构造 loopCond（符号循环条件）**：
   - 从 `indexInfo.indexSymbolicValue`、`indexInfo.indexBound`、`indexInfo.op` 组装二元表达式
   - 将 `<` / `>` 规约到 `<=` / `>=`（例如 `i < n` 变为 `i <= n-1`），便于后续打印/归纳
   - 对 `!=` 做方向性规约（依赖 step 正负）
3. **准备 entry/current state**：
   - clone 出 entry path（要求只有 1 条）
   - 若 `sharedMemoryMap` 存在，把“能常量化的 VariableAddress 写入 entry memoryState”，减少 Unknown
   - clone 出 loopCurrent（可能多条 path）
4. **控制状态爆炸**：
   - 若 `loopCurrent.paths + inactivePaths` 太多则放弃（返回空 optional）
   - 若 loop 中包含数组/指针操作，则关闭 `generateBranches`（倾向合并分支）
5. **调用 analyzer 层归纳**：`analyzer::buildLoopInvariant(loopCond, entryPath, loopCurrent, inactivePaths, generateBranches)`
6. **包装返回值**：
   - 将 analyzer 返回的 (memoryMap, pathConds) 转成 PostPSInfo
   - 对 return-path 的 returnExpr：当前实现把复杂 return 表达式替换为 Unknown

典型效果：
- 对“索引条件清晰”的 for/while 循环，能生成相对强的 `loop invariant`（具体格式取决于 analyzer 实现）。

常见限制：
- entry path 必须是单路径（前面已有分支拆分会导致插件报错）
- indexInfo 能否生成取决于 `SetIndexPlugin` 的识别能力

---

### 5.3 `LinearInvariantPluginForComplexLoop`（ID: `StInGXPluginForComplexLoop`）

- 类型：PS
- 主要作用：复杂循环的 StInGX 版本：不依赖 indexInfo，直接对 condExpr 做符号求值得到 loopCond
- 依赖：`entryAndCurrentInfo`
- 输出：同 `LinearInvariantPlugin`

与普通版本的关键差异：
- 不用 `indexInfo` 手工拼 loopCond
- 直接在 entry path 上 `evalExpr(loopInfo.condExpr)`，要求结果不分支（`evalExprs.size()==1`）

适用场景：
- condExpr 无法被 `SetIndexPlugin` 识别为“简单索引条件”，但符号执行仍能求值得到可用 loopCond

限制：
- condExpr 的符号求值不能产生分支

---

### 5.4 `LoopAssignsPlugin`（ID: `loopAssigns`）

- 类型：PI
- 主要作用：生成 `loop assigns ...;`，并尽量构造正常/中断路径的全局 post-state 信息
- 依赖：`entryAndCurrentInfo` + `indexInfo` + `patternInfo`
- 输出：
  - `acsl`：`loop assigns ...;` 或 `loop assigns \\nothing;`
  - `globalNormalPathPostInfo`：对被写地址的 post 值（尽量推导，否则 Unknown）+ 必要条件（pathConds）
  - `globalInterruptPathsPostInfo`：每条中断路径下的被写地址集合（Unknown 为主）

核心实现思路：
1. **确定“非局部可观察”的地址集合**  
   通过 `isLocal(addr)` 过滤掉 local 地址（root decl 不在 preState.varAddrMap 里）。

2. **利用 patternInfo 判断哪些地址在循环中会变化**  
   遍历 `patternInfo.normalExitPatternsMap`：
   - `pattern == nullopt`：变化过于复杂（或分支不一致），保守处理
   - `pattern != nullopt`：认为该地址遵循 `init + step` 的线性变化

3. **尝试把“随索引移动的地址”提升为 Range（SymbolAddress）**  
   通过 `tryGetAsRange` 检测：
   - base 地址本身在 normalExitPatternsMap 中有 pattern（表示指针基址在移动）
   - 或 offset 是一个有 pattern 的 `SymbolValueExpr`（典型：i 在变）
   
   如果可提升，则构造一个 `SymbolAddress`，并把 length 设置为 loopCount（preciseLoopCount 或 maxLoopCount）。

4. **推导 post 值（尽可能）**  
   对非 range 的地址：
   - 若 `preciseLoopCount` 已知（通常意味着 step=±1、无额外条件、无中断路径），用：
     `post = init + step * loopCount`
   - 若 loopCount 不精确但步长匹配（abs(step) 一致），则尝试用“循环后 index 值”构造 post，并插入 guard 条件：
     - `index_post >= bound` 且 `index_post < bound + step`（step>0）
     - step<0 类似
   
   如果推导失败，则对该地址 post 值设为 Unknown。

5. **生成 assigns 的 ACSL 文本**  
   `assignedAddrs` 中存放的是“符号地址”，需要在每个 real entry path 上 substitute 才能打印。
   打印过程走 `getACSLOfValue(...)`，并对重复片段做 hash 去重。

6. **中断路径的 assigns/post-info**  
   每条中断路径：
   - 把正常路径识别出的 assignedAddrs 全部 Unknown
   - 再把该中断路径 `interruptedPathPatternsMaps[i]` 中出现的地址也加入（Unknown）

效果与局限：
- 对常见“数组/指针线性访问”能生成相对紧凑的 assigns（可能带范围）
- 对复杂 range/复杂地址表达式有较多 TODO，整体仍偏启发式与保守

---

### 5.5 `ComplexLoopAssignsPlugin`（ID: `complexLoopAssigns`）

- 类型：PI
- 主要作用：复杂循环下的 `loop assigns` 兜底版本
- 依赖：`entryAndCurrentInfo`
- 输出：同 `LoopAssignsPlugin`（但更保守）

实现逻辑（概念上更简单）：
1. 从 `symbolicLoopCurrent` 的所有 paths 中收集地址：
   - 跳过 local
   - 跳过“指向结构体”的地址（暂不处理）
   - 若某地址相对于 entry path 未变化，跳过
2. 以上收集出的地址集合即 assigns 集合；打印时同样需要 substitute 并 getACSL
3. post-state 一律 Unknown（保守）
4. 对每条 inactive path 再额外收集一次被写集合，构造中断路径的 post-info

适用场景：
- patternInfo/indexInfo 生成失败或不可用时，仍希望能给出“写集合”的保守近似

---

### 5.6 `ParadigmMaxMinPlugin`（ID: `paradigmMaxMin`）

- 类型：PI
- 主要作用：识别“求最大/最小值”范式，并生成更强的 invariant + post-state
- 依赖：`entryAndCurrentInfo` + `indexInfo` + `patternInfo`
- 输出：
  - `acsl`：来自 `loopInvTemplates.h` 的模板 invariant（可能多段）
  - `globalNormalPathPostInfo`：对 m 的 post 值建模为 `MaxMinOverRange(...)`

范式识别流程：
1. 只在 index step 为 ±1 时工作（否则直接放弃）
2. 遍历 loop body 中每个 `if`：
   - 从 if 条件中匹配 `m < a[i]` / `a[i] > m` / `m > a[i]` 等变体，并据此判断是 max 还是 min
   - 解析 a[i] 的形态，支持：
     - `p[i]`（ArraySubscriptExpr）
     - `*(p+i)`（Deref(Add)）
     - `*it`（it 自身按与 index 相同步长变化的指针）
3. 符号执行校验：
   - then 分支执行后，m 的值应与 elementValue（a[i]）一致
   - else 分支执行后，m 必须保持不变
4. 匹配成功后：
   - 追加模板 invariant（模板位于 `src/SpecGenerator/loopInvTemplates.h`）
   - 更新 post-state：将 m 的 post 值设为 `MaxMinOverRange(arrayRange, "k", extremum, pointAfterLoop)`

模板输出大意：
- 对所有已扫描元素，m 是上界/下界
- 存在某个位置 j，使得 m 等于某个元素
- 索引范围约束（0 <= index <= n 等）

局限：
- 对数组区间的选择目前相对简化（常见是 [0, bound)），更精确的 offset/length 仍待完善
- 范式匹配非常严格：只要符号执行校验不通过就不会输出（宁可不输出也不冒险不 sound）

---

### 5.7 `LoopVariantPlugin`（ID: `loopVariant`）

- 类型：PI
- 主要作用：生成 `loop variant ...;`
- 依赖：`indexInfo`
- 输出：
  - `acsl`：`loop variant <expr>;`（若 `<expr>` 能成功转 ACSL）

实现逻辑：
- 直接取 `indexInfo.maxLoopCount`（SetIndexPlugin 计算得到）作为 variant 表达式
- 若 `getACSL` 失败则放弃（返回 nullopt）

解释：
- maxLoopCount 的设计意图是“到边界的剩余距离的上界”，在合适的循环中可以作为变元用于终止性证明。
- 该选择比较简单保守，不保证对所有循环都严格成立，因此实现上也允许失败。

---

### 5.8 `ParadigmSearchPlugin`（ID: `paradigmSearch`）

- 类型：PS
- 主要作用：识别“搜索循环”范式，生成路径敏感的量词不变式与 post-info
- 依赖：`entryAndCurrentInfo` + `indexInfo` + `patternInfo`
- 输出：
  - `acsl`：尽可能生成 `loop invariant \\forall integer k; ...;`（失败则为空）
  - `normalPathPostInfos` / `interruptPathsPostInfos`：携带 forall/exists 量词条件等信息

前置条件（重要）：
- `inactivePaths.size() == 1`：只允许一个“搜索成功”的中断路径
- index step 为 ±1
- 中断路径上的 path condition 数量为 1（当前只处理单 predicate）

核心实现思路：
1. 取中断路径的 predicate（例如 a[i] == x、a[i] != x 等），记为 `cond(i)`
2. 将 `cond(i)` 转换成 `pred(k)`：
   - 找到 `cond` 中用到的每个 Symbol
   - 如果该 Symbol 对应的地址有 pattern（init, step），则替换为 `init + step * (k - i_init)`（或反向）
   - 若 Symbol 不随循环变化，则直接复用原 facade；发生替换时由 factory 重建受影响路径
   - 若 Symbol 的来源地址是 SymbolAddress（数组/指针访问），还会对子 offset 的 Symbol 做替换，并记录该数组基底
3. 由 `pred(k)` 构造两类量词条件：
   - 正常退出路径：`forall k in range. !pred(k)`
   - 中断退出路径：`exists k in range. pred(k)`
4. 量词区间 `range` 的构造依赖 indexStep：
   - step>0：从当前 index 到 bound（不含 bound）
   - step<0：从 bound 到当前 index（不含当前 index）
5. 尝试把 normal 路径上的 forall 形式打印为 ACSL 文本；打印失败则只返回 post-info

效果（直观理解）：
- 若循环没有触发 break/return，则可以得到“区间内都没有命中”的不变式
- 若循环触发 break/return，则可表达“区间内存在某个命中点”

局限：
- 仅处理单 predicate、单中断路径
- predicate 内涉及的符号必须能从 patternInfo 推导成关于 k 的表达式，否则放弃

---

## 6. 如何新增/扩展 loop 插件（简要建议）

如果要在本框架下新增一个 loop 插件，通常需要考虑：
1. 需要哪些 `LoopInfo` 字段作为依赖（entry/current、index、pattern、shared）
2. 输出的是 PI 还是 PS（是否需要区分中断路径）
3. 输出子句属于 assigns/invariant/variant 哪一类（specGenerator 会按前缀分类）
4. 是否需要更新 post-state（memoryMap/pathConds/returnExpr）以帮助后续推导

可以参考：
- `src/SpecGenerator/loopInfoPlugins.cpp`：如何填充 LoopInfo（pattern/index/shared）
- `src/SpecGenerator/specGenerator.cpp`：插件结果如何被 substitute、合并、并最终写回 post-state
