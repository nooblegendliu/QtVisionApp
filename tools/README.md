# 第一阶段无硬件联调

先构建 `Release|x64`，再从仓库根目录启动模拟运行时：

```powershell
.\x64\Release\VisionApp.exe --config .\config\vision_app.simulation.example.json --mode production --backend simulation
```

另开一个 PowerShell 窗口运行协议验证客户端：

```powershell
.\tools\demo_tcp_client.ps1
```

脚本会验证状态查询、首次采集失败与恢复、Busy 拒绝、Busy 期间状态查询、Edge OK、MarkPoint NG、心跳和结果 ACK。任何协议不一致都会以非零状态结束。

服务端使用 `Ctrl+C` 安全关闭。模拟后端只用于第一阶段流程联调；真实运行默认使用 `hik`，且不存在从 HIK 自动回退到模拟后端的行为。
