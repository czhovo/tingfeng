# 听风 v70

v70-parallel 冻结源码，基于 v67-parallel-soft，支持 schema 2 对手暗手下界信息。两档共用源码，分别编译为 3.3 秒、1.7 秒并行版本，无节点数上限。

## 编译

使用 Windows x64 MinGW-w64 GCC（原版本使用 w64devkit）、C++17 和 PowerShell。JSON 依赖已包含，无需下载。Windows 运行时负责 P 核绑定与 HIGH 进程优先级；本文构建针对 Windows，不承诺 MSVC 或其他平台兼容性。

```powershell
./build.ps1 -Cxx 'D:/botzone/toolchains/w64devkit/bin/g++.exe'
```

也可将 g++ 放入 PATH，直接运行 `./build.ps1`。可用 `-Budget 3p3` 或 `-Budget 1p7` 单独构建。

输出：`build/bot-3p3.exe`、`build/bot-1p7.exe`。

等效命令：

```text
g++ -std=c++17 -O2 -static -s -pthread -Ithird_party src/bot.cpp -o build/bot-3p3.exe
g++ -std=c++17 -O2 -static -s -pthread -Ithird_party -DTINGFENG_SEARCH_SECONDS=1.70 -DPARALLEL_LOW_BUDGET src/bot.cpp -o build/bot-1p7.exe
```

## 输入输出

标准输入每行一份 JSON 请求，标准输出为协议响应；诊断写入标准错误。默认关闭 analyze，`--analyze` 开启诊断，`--workers=1` 可用于固定线程数的复现。默认按可用物理 P 核并行。

```powershell
Get-Content examples/input.json -Raw | ./build/bot-3p3.exe --workers=1 --analyze
```

`examples/input.json` 是构造的协议示例。`SOURCE_HASHES.json` 记录原版源码 SHA-256，源码未修改算法。`BUILD_VERIFICATION.json` 记录本次独立代码包的双档编译及冒烟验证。

第三方 `nlohmann/json` 3.12.0 按其头文件中的 MIT 许可提供，保留原始版权和许可声明。
