# Release|x64 验收记录

## 最终结果

- 唯一 VisionTest 工程及 filters 静态审计通过；生产实现、界面、工程与依赖配置共 101 个文件的 SHA256 与改动前一致。
- 完整解决方案 Release|x64 构建成功；未构建 Debug。构建过程有非致命第三方/Qt部署警告，详见日志。
- Default 全集：实际执行 418，失败 0。
- 独立 Ui：实际执行 15，失败 0。
- MVS 环境、ManualUi、真实设备、模板成功链路和生产精度未验收。

最终两个组的可执行文件 SHA256 相同：`3C0289E2A64C2B8E60DCFDA7873EFEAD2C6D88D8C678C28D26C095ADD5E84045`。

| 验证 | 实际结果 | 原始报告 |
| --- | --- | --- |
| Core 分模块 | passed；执行 108，失败 0 | [summary](../x64/test-rewrite/results/20260908-094708-Core-Default-372299f78d074877858e4d478d6417c3/summary.json) |
| Algorithm 分模块 | passed；执行 134，失败 0 | [summary](../x64/test-rewrite/results/20260908-094902-Algorithm-Default-e2a4b4e781cf4f43bd1217a227bd59cd/summary.json) |
| Hardware 分模块 | passed；执行 110，失败 0 | [summary](../x64/test-rewrite/results/20260908-095345-Hardware-Default-9e87d9dccd6b4017aff859b27553c3de/summary.json) |
| Tcp 分模块 | passed；执行 15，失败 0 | [summary](../x64/test-rewrite/results/20260908-095401-Tcp-Default-122eceb51cb543899e30b2fb4f9b9abf/summary.json) |
| App 分模块 | passed；执行 54，失败 0 | [summary](../x64/test-rewrite/results/20260908-095417-App-Default-5d335334178844b0887c7aaefdb18790/summary.json) |
| 最终 Default 全集 | passed；执行 418 | [summary](../x64/test-rewrite/results/20260908-100634-All-Default-a38f624692c046e19cd6c0bf3019026d/summary.json) |
| 最终 Ui | passed；执行 15 | [summary](../x64/test-rewrite/results/20260908-100751-App-Ui-1954777fa90e4cb6a7b03aaea77d897e/summary.json) |

[构建日志](../x64/test-rewrite/build.log)；各 summary 同目录保存 XML、stdout/stderr、必要图像和截图。

## 执行方式与过程问题

运行入口为 README 中的 audit_project.ps1、Release MSBuild 与 run_tests.ps1。已验证脚本语法及 Core+Ui、App+MvsEnvironment、All+ManualUi 三种非法组合被拒绝。脚本不隐式构建，零执行用例和缺失XML不能被报告为通过。

受限执行中，Qt部署的子进程管道以及C++临时目录创建曾被环境拒绝；相关失败报告保留，正式验收在正常文件访问/进程环境完成。首次UI运行因测试未处理“应用配置”确认弹窗而停止；已修复测试确认，并增加未处理弹窗5秒失败保护，后续UI通过。未把这些问题归为生产缺陷，也没有修改生产行为。

Core 去重及后续快照/目录隔离修正后的最终结果以全集为准；早期分模块报告的可执行文件哈希和测试数量可能不同，不能替代最终全集。

具体覆盖与未覆盖路径见 [COVERAGE.md](COVERAGE.md) 和 [DEFECTS.md](DEFECTS.md)。
