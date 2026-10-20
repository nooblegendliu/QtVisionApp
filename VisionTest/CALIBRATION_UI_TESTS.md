# 标定 UI 测试

已统一到 `run_tests.ps1 -Module App -Group Ui`。
自动组覆盖中心和四角 5×5 标定、配置操作及窗口切换；默认回归仍不弹窗。
`run_calibration_ui.ps1 -Mode Auto/Interactive` 是兼容转发入口。
运行条件、输出及验收边界见 README.md 与 AGENTS.md。
