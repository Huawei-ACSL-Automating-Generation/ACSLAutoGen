# 项目报告：ACSLG 在 OpenHiTLS（BN 与后量子密码 PQC）上的自动规格生成与 Frama-C WP 验证

## 0. 摘要

本项目提供一条自动化验证流水线：对 OpenHiTLS 的 C 源码，使用 ACSLG 自动生成 ACSL 规格（函数合约与循环注释），并调用 Frama-C WP（Qed）对生成后的代码进行目标证明统计，以量化“可验证性”和“生成规格的有效性”。项目同时提供脚本化实验框架（构建、批量运行、日志与结果汇总），用于覆盖大数运算（BN）与后量子密码（PQC）模块的代表性函数集合。

本报告包含三大板块：
1) **实现说明**：架构、模块职责、插件系统、脚本工程化与关键建模策略。  
2) **使用文档**：一键运行、可复现参数、输出解读。  
3) **实验结果**：指标定义、数据集/函数选择、结果汇总、失败原因归类与改进方向。

---

## 1. 项目概述

### 1.1 背景与动机
OpenHiTLS 属于工程化程度高的密码库，包含大量宏、平台适配、回调/内建函数与复杂类型体系。对这类代码做形式化验证往往需要组件化、分层推进：上层 API 依赖大量底层基础构件（如内存管理、引用计数、缓冲区/字节序处理、大数运算与 KEM/签名组件等）。若底层函数缺乏稳定的契约与抽象，上层验证会被底层未建模行为迅速放大，难以收敛。

另一方面，手工为大量底层函数补齐 ACSL 合约（`requires/ensures/assigns`、别名关系、缓冲区边界、错误码分支、资源生命周期等）代价很高，且工程代码普遍存在条件编译、平台差异与复杂错误路径，使得“覆盖所有可能路径”的规格更难做到并容易与实现演进产生偏离。

基于此，本项目以 Clang/LLVM 工具链为基础，在真实编译参数（`compile_commands.json`）下解析目标 C 代码，面向函数体进行语义分析与状态推导（例如符号执行、pre/post 状态抽象、循环信息提取），自动生成可被 Frama-C 接受的 ACSL 注释文件，并通过 Frama-C WP（Qed）对生成结果进行证明目标统计，形成“可生成、可验证、可度量”的工程化反馈闭环。

### 1.2 目标
本项目目标以“可复现、可度量”为导向，分为两类：
1) **生成内容目标（合约信息）**：在真实编译参数（`compile_commands.json`）下，对目标函数自动生成可供验证使用的 ACSL 规格，主要包括函数级 `requires/ensures/assigns` 与行为分解（behavior），以及循环级 `loop invariant/assigns/variant` 等关键子句，并输出带注释的 `*_acsl.c` 文件。
2) **覆盖范围目标（目标代码集合）**：面向 OpenHiTLS 中具有代表性的函数族进行验证闭环评估，包括 BN 目录下的基础函数与包含循环/数组操作的底层运算函数（覆盖 BNADD 等典型算子），以及后量子密码（PQC）模块中的代表性接口函数集合（以“可生成、可验证、可统计”为基准形成可复现实验集）。

---

## 2. 系统设计与实现说明

本节从“输入→分析→生成→验证→汇总”的完整链条说明实现细节，并穿插展示实际生成的合约片段与脚本输出片段。

### 2.1 输入、输出与主流程
ACSLG 基于 Clang/LLVM 工具链实现，输入以 `compile_commands.json` 为核心：它提供真实的 include 路径、宏定义与编译选项，使分析在工程语境下成立。工具以“源文件 + 函数”为粒度工作，输出为在原始 C 文件基础上插入 ACSL 注释后的 `*_acsl.c` 文件。

典型调用形态如下（以编译数据库目录 `COMP_DB_DIR` 为例）：
```bash
build/src/ACSLG -p "$COMP_DB_DIR" openHiTLS/.../file.c --func TargetFunc --out-dir out/
```

从数据流角度，主流程可以概括为：
1) 解析 TU 并构建 AST（Clang Tooling）；2) 初始化分析上下文（SourceManager/Rewriter/函数集合）；3) 对目标函数做符号执行与状态汇总（pre/post state）；4) 按插件生成函数合约与循环规格；5) 将注释写回并输出 `*_acsl.c`。

### 2.2 静态分析与规格生成实现（核心数据结构）
实现上，ACSLG 将“语义分析”和“规格生成”明确分层：

从代码结构看，关键模块可按职责划分为：

| 模块 | 主要职责 |
|---|---|
| `src/main.cpp` | Clang Tooling 入口：解析参数、选择目标函数、驱动整体流程与输出命名 |
| `src/Context/*` | 编译上下文封装：SourceManager/Rewriter 管理、函数收集、插入点与标签管理 |
| `src/Analyzer/*` | 语义分析与符号执行：`ProgramState`/`Path` 管理、路径条件、跨 TU 导入等 |
| `src/Analyzer/Symbolic/*` | 符号表达式与地址系统：表达式简化、哈希/等价、ACSL 打印 |
| `src/SpecGenerator/*` | 插件系统与模板：函数合约与循环规格生成、插件组编排与合并 |
| `src/Stingx/*` | 线性关系与约束工具：为部分不变式推导提供基础设施 |

首先在 Analyzer 中，对每个函数建立 `ProgramState` 并以 `Path` 维护路径状态。路径状态会持续更新三类关键信息：内存模型（MemoryModel）、路径条件（PathConditions）与返回值表达式（ReturnExpr）。其中内存模型用统一的“地址/值”抽象表达变量、指针与结构体字段，例如：
- `VariableAddress`：变量地址（形参/局部/可见符号的抽象地址）
- `FieldAddress`：结构体字段地址（形如 `p->field`）
- `SymbolAddress`：带 offset/length 的符号地址（用于表达范围写入、数组区间等）
- `Structure`：结构体值容器（字段值集合）
- `SymbolValue` / `LiteralExpr` / `UnknownExpr`：符号值、常量与保守占位

在 SpecGenerator 中，生成逻辑采用插件体系：函数级 contract 插件负责输出 `requires/ensures/assigns` 与行为分解（behavior），循环级插件负责输出 `loop invariant/assigns/variant` 并返回用于后续合成 post-state 的辅助信息。最终输出时，生成的 ACSL 注释会被插入到函数定义前并写入 `*_acsl.c`。

当函数存在显式分支时，函数合约通常会以多个 behavior 表达不同路径下的后置性质与写集合（下例仅用于展示格式，并非特定函数的实际输出）：
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

以下给出 `BinAdd` 的生成结果片段（节选自 `noasm_bn_bincal_acsl.c`，省略部分实现体）：
```c
/*@
  assigns r[0 .. n - 1];
  behavior b0:
    assigns r[0 .. n - 1];
*/
BN_UINT BinAdd(BN_UINT *r, const BN_UINT *a, const BN_UINT *b, uint32_t n)
{
  ...
  /*@
      loop assigns r[0 .. n - 1], nn, bb, aa, carry, rr;
      loop variant nn;
  */
  while (nn >= 4) { ... }
  ...
}
```
该例体现了两个关键点：其一，`assigns` 会把对外可观察的写集合显式化（这里是 `r[0..n-1]`）；其二，循环会生成 `loop assigns/variant`，用于支撑 WP 的别名与终止性相关目标。

### 2.3 循环规格：两阶段信息提取与插件体系
循环规格生成在实现上分为两个阶段：

第一阶段是 LoopInfo 解析：对某个 loop 构造 `LoopInfo`，抽取 init/cond/inc/body 的结构信息，并在“loop entry / loop current”两个抽象时刻上收集符号状态；同时尽可能识别索引变量、边界与步长（indexInfo），以及地址随循环变化的线性模式（patternInfo）。这一步为后续的 `loop assigns` 与不变式推导提供原料。实现上常见的 LoopInfo 字段包括：
- `entryAndCurrentInfo`：loop entry/current 的符号状态与中断路径（break/return）集合
- `indexInfo`：索引变量、边界、步长、循环次数上界等
- `patternInfo`：地址变化模式（例如 `init + step` 的线性变化），用于 range 提升与 post 值推导
- `sharedMemoryMap`：跨 entry paths 合并后仍能确定的值，用于减少 Unknown 扩散

在默认实现中，这些字段分别由一组 LoopInfo 插件填充（例如 `SetEntryAndCurrent` 负责构造 entry/current 状态，`setIndex` 负责索引识别，`setPatterns` 负责变化模式抽取，`setSharedState` 负责跨路径可确定值合并）。当识别成功时，后续 `loopAssigns` 会优先把“随索引移动的写入”提升为范围（`SymbolAddress` + length），并在可能情况下推导循环后的 post 值；识别失败时则退化为更保守的写集合与 Unknown 后态。

第二阶段是 `loop invariant/assigns/variant` 生成：插件以两类接口组织——Path-insensitive（PI）与 Path-sensitive（PS）。PI 插件倾向生成对所有路径都成立的结论（例如保守写集合、变元），PS 插件允许区分正常退出与中断路径，并为不同路径生成更精细的 post-info（适合搜索/提前退出类循环）。

默认情况下，循环插件会按“插件组（group）”启用（例如先运行 LoopInfo 组填充 `LoopInfo`，再运行默认的 PI/PS 插件组生成循环规格）。典型的默认组合思路是：PI 插件优先产出 `loop assigns/variant` 的稳定近似，PS 插件在信息充分时再补强 invariant 与路径相关 post-info。

在典型“索引条件清晰”的循环中，插件可以生成更强的不变式；在复杂循环中，生成会更保守，常见表现是：`loop assigns` 仍可给出较准确的写集合，但 `loop invariant` 可能退化为较宽松甚至较复杂的析取式（例如出现多分支合并后的条件拼接）。这也是工程化验证中常见的折中：优先保证流程可继续，并通过 WP 统计反馈“哪些性质已可证、哪些仍缺失”。

### 2.4 工程脚本、验证集成与结果汇总
项目提供脚本化流水线将“生成—验证—汇总”固化下来，主要脚本包括：

`compile.sh` 负责构建 ACSLG；`experiment.sh` 负责批量运行 ACSLG + Frama-C WP；`run_single.sh` 用于单函数定位；`report_summary.py` 负责从 `results.csv` 生成汇总报告与表格。

`experiment.sh` 的行为可以概括为：对每个（suite, file, function）先运行 ACSLG 生成 `*_acsl.c`，若生成成功则调用 Frama-C WP（Qed）对该文件执行证明并解析 `[wp] Proved goals:` 统计，最终将逐函数结果写入 `results.csv`，并自动生成 `report.md` 与 `bn_wp_results.txt`。

`results.csv` 以“每函数一行”的方式记录关键字段（suite/source/function/耗时/是否生成成功/WP 证明统计等）。例如在一次全量运行记录中，`BinAdd` 的一行结果为（摘录字段）：
```text
suite=noasm, function=BinAdd, acslg_error=no, wp_result=partial, wp_proved/wp_total=25/38
```
对应的 CSV 表头如下（用于二次分析与可视化处理）：
```text
suite,source,function,acslg_time_sec,acslg_rc,acslg_error,acslg_issue,wp_proved,wp_total,wp_result,wp_issue
```
这种结构化记录用于支撑工程化迭代：当某类函数出现集中失败时，可快速定位是解析、建模、生成或验证环节发生回归。

### 2.5 工程适配（内建/建模）的边界
为在工程化密码库上获得稳定的“可运行闭环”，本项目对若干常见阻塞点采用最小建模策略：例如将 `_Atomic(T)` 视为 `T`、将 `enum` 视为 `int`、对部分分配/清零函数（如 `BSL_SAL_Malloc`、`memset_s`）在分析层做内建建模，并忽略不影响语义的提示性内建（如 `__builtin_expect`）。这些策略以“让分析继续前进”为首要目标，适合用于批量实验与失败归因；若要追求更强的规格与更高的证明比例，则需要进一步补齐语义一致性与别名模型。

---

## 3. 使用文档

### 3.1 环境准备
项目依赖 LLVM/Clang 19、Frama-C（WP）等工具链组件，并依赖 Z3/GMP/PPL 等基础库。工程目录提供容器化构建环境（`Dockerfile`），用于固化依赖版本与构建步骤。

主机侧要求为 Linux + Docker（20.10+）。脚本与默认路径假设工作区根目录包含同级的 `ACSLG/` 与 `openHiTLS/` 两个目录；下述命令均以工作区根目录为当前目录。

1) 导入预制镜像（如已获得 `acslg-env.tar.gz`）：
```bash
tar -xzf acslg-env.tar.gz
docker load -i acslg-env.tar
docker run --rm -it -v "$(pwd)":/workspace acslg-env bash
```

2) 自行构建镜像（使用工程内 `Dockerfile`）：
```bash
docker build -t acslg-env -f ACSLG/Dockerfile ACSLG
docker run --rm -it -v "$(pwd)":/workspace acslg-env bash
```

### 3.2 构建 ACSLG
在容器内进入 `ACSLG/` 并执行：
```bash
cd /workspace/ACSLG
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j"$(nproc)"
```
构建产物为 `build/src/ACSLG`。

### 3.3 准备 OpenHiTLS 编译数据库
ACSLG 的工程化运行依赖 `compile_commands.json`，以便复用 OpenHiTLS 的真实编译选项、宏与 include 路径。脚本默认优先使用 `openHiTLS/compile_commands.json`，否则使用 `openHiTLS/build/compile_commands.json`。

若本地尚未生成 OpenHiTLS 的编译数据库，可在 `openHiTLS/` 下按其构建流程生成（关键点是开启 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 并确保最终产出 `compile_commands.json`）。在本项目的默认目录布局中，编译数据库通常保存在 `openHiTLS/build/`。

以下给出一种典型生成方式（以 Linux + 64 位静态库构建为例；具体配置参数以 OpenHiTLS 的 `configure.py`/文档为准）：
```bash
cd ../openHiTLS
mkdir -p build && cd build
python3 ../configure.py --enable hitls_bsl hitls_crypto hitls_tls hitls_pki hitls_auth --lib_type static --bits=64 --system=linux
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build . -j"$(nproc)"
```

### 3.4 ACSLG 命令行使用
ACSLG 为基于 Clang Tooling 的命令行工具。其基本形式为：
```bash
./build/src/ACSLG [ACSLG选项] <source.c> [-- clang额外参数]
```

常用运行方式如下：

1) 单文件运行（不使用编译数据库；适用于简单场景）：
```bash
./build/src/ACSLG path/to/source.c
```

2) 工程化运行（使用编译数据库；适用于 OpenHiTLS）：
```bash
./build/src/ACSLG -p ../openHiTLS/build ../openHiTLS/crypto/bn/src/bn_basic.c --func BN_Create --out-dir out/
```

选项说明（来自工具实现与 `--help` 输出）：
| 选项 | 含义 |
|---|---|
| `-p <dir>` | 指定 `compile_commands.json` 所在目录 |
| `--func <name1,name2,...>` | 仅分析指定函数；省略则分析文件内所有可分析函数 |
| `--out-dir <dir>` | 将 `*_acsl.c` 输出写入指定目录（默认写回源文件同目录） |
| `--log-level off|error|warn|info|debug` | 设置日志级别 |
| `--ast-only` | 仅输出 AST（调试解析） |
| `--no-output` | 不写输出文件（分析仍执行） |
| `--extra-arg <arg>` | 在编译命令末尾追加额外 clang 参数 |
| `--extra-arg-before <arg>` | 在编译命令开头追加额外 clang 参数 |

输出文件命名规则为：保持原始扩展名不变，在文件名末尾插入 `_acsl`（例如 `foo.c` 生成 `foo_acsl.c`）。重写阶段会移除“非顶层 `requires`”的既有 ACSL 注释，以避免与生成结果冲突。

### 3.5 批量实验（ACSLG + Frama-C WP）
工程提供 `scripts/experiment.sh` 以批量执行“生成—验证—汇总”流水线：对每个函数运行 ACSLG 生成 `*_acsl.c`，并调用 Frama-C WP（Qed）统计证明目标与证明比例。入口为：
```bash
./scripts/experiment.sh
```
运行结果写入 `runlogs/<run_tag>/`，逐函数记录为 `results.csv`。

### 3.6 单函数运行与定位
工程提供 `scripts/run_single.sh` 以单函数粒度运行同一条流水线（生成 + WP + 日志落盘），便于定位阻塞点与回归问题：
```bash
./scripts/run_single.sh <FunctionName> [path/to/source.c]
```
未指定源文件时，脚本默认使用 OpenHiTLS 的 `crypto/bn/src/bn_basic.c`。

---

## 4. 实验设计与结果

本节给出实验指标、benchmark 选择与本次运行的结果汇总与分析。

### 4.1 评价指标与数据采集
实验以“函数粒度”记录 ACSLG 与 Frama-C WP 的运行状态与证明统计，关键字段包括：
- `acslg_error`：ACSLG 是否成功生成目标文件（`*_acsl.c`）
- `acslg_time_sec`：单函数 ACSLG 耗时
- `wp_result`：WP 结果（`all/partial/none/timeout`）
- `wp_proved/wp_total`：当 WP 可解析输出时的目标证明统计

### 4.2 Benchmarks 与函数集合
本次运行使用 `scripts/experiment.sh` 的默认 suites（BN + 可跑通的 PQC 子集）：
- BN：`bn_basic.c`（31 函数）、`bn_bincal.c`（14 函数）、`noasm_bn_bincal.c`（4 函数，包含 `BinAdd`）
- PQC：`frodokem.c`（2 函数：`CRYPT_FRODOKEM_EncapsInit/DecapsInit`）+ `quantum` 子集（6 函数：SLH-DSA/XMSS/FrodoKEM 的小粒度函数集合）

说明：PQC 子集以“ACSLG 成功 + WP 可跑完（允许 partial）”为准做了收敛，用于形成稳定的端到端验证闭环；更大范围的 PQC 接口在当前实现与建模策略下仍存在较多阻塞点，属于探索性评估范围。

### 4.3 总体结果汇总
本次运行的统计结果如下：
- 总函数数：57
- ACSLG 成功/失败：46/11（80.7%）
- WP all/partial/none/timeout：0/44/13/0
- 可解析 WP 目标的函数数：44/57（77.2%）
- 可解析目标合计证明：3783/4401（86.0%）

按 suite 汇总（函数数、ACSLG 成功率与 WP 运行情况）：

| suite | funcs | acslg_ok | acslg_fail | wp_all | wp_partial | wp_none | wp_timeout |
|---|---:|---:|---:|---:|---:|---:|---:|
| basic | 31 | 25 | 6 | 0 | 24 | 7 | 0 |
| bincal | 14 | 9 | 5 | 0 | 8 | 6 | 0 |
| noasm | 4 | 4 | 0 | 0 | 4 | 0 | 0 |
| frodokem | 2 | 2 | 0 | 0 | 2 | 0 | 0 |
| quantum | 6 | 6 | 0 | 0 | 6 | 0 | 0 |

按 suite 汇总 WP 目标证明数量（仅统计 `wp_proved/wp_total` 可解析的函数）：

| suite | funcs_with_goals | proved_goals | total_goals | proved_ratio |
|---|---:|---:|---:|---:|
| basic | 24 | 2478 | 2676 | 92.6% |
| bincal | 8 | 521 | 677 | 77.0% |
| noasm | 4 | 98 | 137 | 71.5% |
| frodokem | 2 | 118 | 184 | 64.1% |
| quantum | 6 | 568 | 727 | 78.1% |

PQC 子集（`frodokem` + `quantum`）的逐函数 WP 统计如下（均为 partial）：

| suite | function | wp_proved | wp_total |
|---|---|---:|---:|
| frodokem | CRYPT_FRODOKEM_EncapsInit | 59 | 92 |
| frodokem | CRYPT_FRODOKEM_DecapsInit | 59 | 92 |
| quantum | UCAdrsGetAdrsLen | 127 | 156 |
| quantum | CAdrsGetAdrsLen | 127 | 156 |
| quantum | CRYPT_FRODOKEM_EncapsInit | 59 | 92 |
| quantum | CRYPT_FRODOKEM_DecapsInit | 59 | 92 |
| quantum | XAdrsGetAdrsLen | 94 | 111 |
| quantum | CheckNotXmssAlgId | 102 | 120 |

### 4.4 结果分析与主要失败类型
1) BN 基础函数（`basic`）整体更稳定：ACSLG 成功 25/31，且在可解析 WP 的 24 个函数上目标证明比例达到 92.6%。该套件的主要阻塞点集中在少数结构体/回调相关函数与个别未建模行为（例如结构体状态缺失、LLVM cast 断言、回调函数类型等）。

2) BN 二进制运算（`bincal`）存在更集中且更“工程化”的阻塞点：包括路径上下文断言失败（`setStmtCtx`）、表达式类型未实现、以及少量 WP 侧的用户错误。该类函数往往包含更复杂的位运算、宏与平台相关路径，导致分析更易触发边界条件。

3) PQC 子集在当前收敛配置下可形成稳定闭环：8/8 函数 ACSLG 成功且 WP 均可运行并产出目标统计（partial）。从工程角度，这类子集适合用于持续集成式回归：当工具链或建模策略变化时，可以快速观察“端到端可运行性”是否回退。

### 4.5 AutoDeduct 对照实验与工具对比
为更客观评估“自动生成 ACSL + 自动验证”的可行性，本项目额外整理了一组 **AutoDeduct** 的对照实验产物（位于 `docs/auto_deduct_runs/`）。AutoDeduct 的流水线为 Saida→ISP→WP（按文件运行），该实验在复制出的 openHiTLS 源码上执行，原始源码未改动，并对每个阶段设置 30 秒超时。

AutoDeduct 在 6 个源文件上的运行结果（按文件粒度）可概括为：
| 分组 | 文件 | Saida/ISP | WP | 现象摘要 |
|---|---|---|---|---|
| bn | `bn_bincal.c` | 失败 | - | Saida 内部错误，未产出可用合同 |
| bn | `bn_basic.c` | 成功 | 失败 | WP 报错（如 “Invalid infinite range”），且存在外部函数缺规格问题 |
| bn | `noasm_bn_bincal.c` | 成功 | 超时 | WP 30 秒超时 |
| pq | `ml_dsa.c` | 失败 | - | Saida 内部错误 |
| pq | `ml_kem.c` | 失败 | - | Saida 内部错误（含函数指针类型告警） |
| pq | `slh_dsa.c` | 成功 | 超时 | WP 30 秒超时（大量外部函数无规格） |

AutoDeduct 在这些目标上生成的合同多接近“空合同”（例如入口插入 `requires \true; ensures \true;`，且缺少有约束力的 `assigns`/边界条件），因此即使进入 WP，验证也更容易失败或超时。

与之对比，本项目的 **ACSLG（ACSLGAutoGen）** 采用“按函数”粒度运行：对每个函数单独生成 `*_acsl.c` 并单独跑 WP 统计。这种切分使得：
1) 验证规模更可控（单函数目标规模通常远小于整文件），更容易得到 `partial` 的可证明结果；  
2) 失败更易定位到具体函数与具体规格片段（便于工程化迭代修复）。

在本报告前述的 ACSLG 实验设置下（见 4.3/4.4），BN 的 `noasm` 套件 4/4 函数均可进入 WP 并得到 `partial`；`basic/bincal` 中也有较大比例函数可生成目标并得到证明统计。这与 AutoDeduct 在同类 BN 文件上“文件级 WP 易失败/易超时”的现象形成对照。
