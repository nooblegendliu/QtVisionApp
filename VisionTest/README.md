# VisionTest 当前功能回归

保留一个 GoogleTest 可执行程序，按 Core、Algorithm、Hardware、Tcp、App 分模块运行。
Default 不创建窗口、不调用 MVS SDK；统一进程仍链接 MVS 运行库，进程加载要求不等于真机覆盖。

## 构建与运行

在已配置 Qt/MSBuild、OpenCV 和 MVS 开发环境的 PowerShell 7 中，从仓库根目录执行。
保持当前依赖版本，使用 VS 开发环境可找到的 MSBuild；`QtMsBuild` 应指向实际安装目录。

```powershell
./VisionTest/audit_project.ps1
MSBuild QtVisionApp.slnx /m:1 /nr:false /p:Configuration=Release /p:Platform=x64
./VisionTest/run_tests.ps1 -Module Core
./VisionTest/run_tests.ps1 -Module Algorithm
./VisionTest/run_tests.ps1 -Module Hardware
./VisionTest/run_tests.ps1 -Module Tcp
./VisionTest/run_tests.ps1 -Module App
./VisionTest/run_tests.ps1
./VisionTest/run_tests.ps1 -Module App -Group Ui
```

运行脚本不构建。默认 Module=All、Group=Default；Ui/ManualUi 仅支持 App，MvsEnvironment 仅支持 Hardware。
Ui 为真实 Qt Widgets 自动操作，需要可用的 Windows 桌面；ManualUi 为人工观察，不能当作自动验收。
以下命令仅在明确要求该组时执行：

```powershell
./VisionTest/run_tests.ps1 -Module Hardware -Group MvsEnvironment
./VisionTest/run_tests.ps1 -Module App -Group ManualUi
```

## 输出及隔离

可通过 `-OutputDirectory` 指定结果根目录；默认位于系统临时目录 `QtVisionApp-VisionTest-Results`。
每次创建含时间和 GUID 的目录，输出 `results.xml`、`stdout.log`、`stderr.log`、`summary.json`，
以及 Edge 图像和 UI 截图/事件。summary 包含可执行文件 SHA256、分组、过滤器和各模块实际执行数。
成功/失败/环境组报告独立，历史结果不自动合并。

TEMP/TMP 指向本次 scratch；测试再创建唯一资源子目录，退出时清理本次资源。
运行结果保留供诊断，不自动删除历史运行。使用者按磁盘容量手动清理已归档的运行目录。
测试不得使用生产配置作写入目标，也不得清空固定临时目录。

退出码：0 通过；1 测试失败；3 启动、报告、零用例或进程超时等基础设施失败。
脚本在600秒后终止其拥有的独立测试进程并保留日志，不对单个视觉线程执行强制终止。
Qt 事件、Fake 门闩和正常关闭等待为5秒。失败清理先释放 Fake，再等线程自然退出。

## 测试边界

用例前缀 `Core_`、`Algorithm_`、`Hardware_`、`Tcp_`、`App_` 用于模块选择。
独立组采用 `DISABLED_App_Ui_`、`DISABLED_App_ManualUi_`、`DISABLED_Hardware_Mvs_`。
Environment 拒绝跨组混跑。新增测试必须同步更新项目、filters 与覆盖说明。

Algorithm 的模板匹配成功路径及完整四图 MarkPoint 图像定位继续暂缓。
Ui 和 App 后端的四角标定使用真实标定算法，不调用生产 Mark 匹配。
合成数据容差是功能基线，不代表工件精度、硬件时序或长期稳定性验收。
生产代码缺陷不得通过改预期、扩大容差或禁用测试掩盖；记录在 DEFECTS.md。

详见 AGENTS.md、COVERAGE.md 和验收报告 VALIDATION.md。
