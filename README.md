# 听风 v70

Windows x64 / MinGW-w64 GCC / C++17。两份源码均已内联项目头文件，JSON 依赖随仓库提供。

| 源码 | 默认构建 | 低时限构建 |
|---|---|---|
| [v70-parallel.cpp](v70-parallel.cpp) | v70-parallel-3p3 | v70-parallel-1p7 |
| [v70-hp.cpp](v70-hp.cpp) | v70-hp-5p5 | v70-hp-3p5 |

在 PowerShell 执行以下命令，仅编译并链接四个程序，输出为 `build/<构建名>.exe`。

```powershell
./build.ps1 -Cxx 'D:/botzone/toolchains/w64devkit/bin/g++.exe'
```

g++ 在 PATH 中时可省略 -Cxx。单独构建可传 -Budget 3p3、1p7、5p5 或 3p5。低档分别定义 PARALLEL_LOW_BUDGET 和对应的 TINGFENG_SEARCH_SECONDS（1.70 或 3.50）。

只有 HP 有内存截止。两档共用 CPP 中的 SEARCH_MEMORY_BUDGET_BYTES，默认 24,000,000,000 字节。它基于整个进程的私有提交量及并发分配预留停止搜索；保留最后完整结果，第一层未完成时回退到合法动作。它不是操作系统硬上限，收尾或在途分配可能短暂越线。本次只做编译和静态核对，没有运行引擎。普通版保持冻结版本的策略和时限。

输入为逐行 JSON，stdout 返回动作，stderr 输出诊断。两系列支持 schema 2 对手暗手下界信息。src、SOURCE_HASHES.json 和 BUILD_VERIFICATION.json 保留原普通版历史记录。第三方 nlohmann/json 3.12.0 的 MIT 许可见其头文件。
