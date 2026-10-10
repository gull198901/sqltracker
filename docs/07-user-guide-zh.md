# SqlTracker Windows x86 功能说明

## 1. 工具定位

SqlTracker 是一个面向 Oracle OCI 客户端的 SQL 跟踪工具。当前版本通过注入
`sqltracker_oci_agent_win32.dll` 到目标 Oracle 客户端进程中，捕获 OCI 调用，
并在 Windows 图形界面中实时显示 SQL、绑定变量、执行耗时、结束时间和错误信息。

当前主流程保持老工具的实时流式输出方式，不采用“每条 SQL 一行表格”的主视图。

## 2. 当前支持范围

- 操作系统：Windows 10 / Windows 11。
- 目标进程：32 位 Oracle OCI 客户端进程。
- Oracle 条件：目标进程已加载 `oci.dll`。
- UI 程序：WPF Windows 桌面程序。
- Agent：Windows x86 native DLL。
- 输出方式：实时追加 SQL 文本块。
- 数据保存：默认只保存在当前内存会话中，手动点击 `Save` 才写入本地文件。

当前不支持：

- 64 位 Oracle 客户端进程。
- Linux 版本。
- MySQL、PostgreSQL、SQL Server 等其他数据库。
- 历史数据库缓存或持久化存储。

## 3. 运行方式

开发调试版本：

```text
artifacts-net10\bin\SqlTracker.Windows\debug\SqlTracker.Windows.exe
```

可拷贝发布包：

```text
dist\SqlTracker.Windows-win-x86-self-contained.zip
```

发布包解压后直接运行：

```text
SqlTracker.Windows.exe
```

自包含发布包已经带 .NET 运行时，目标电脑通常不需要单独安装 .NET Runtime。

如果目标 Oracle 客户端程序是管理员权限启动，SqlTracker 也需要以管理员权限启动。

## 4. 主界面功能

主界面由三部分组成：

- 顶部工具栏：操作按钮和过滤条件。
- 左侧目标进程列表：显示可跟踪的 Oracle OCI 进程。
- 右侧输出窗口：实时显示 SQL 跟踪内容。

顶部按钮说明：

- `Refresh`：重新扫描当前机器上的 Oracle OCI 进程。
- `Start`：对选中的目标进程开始跟踪。
- `Pause`：停止当前跟踪，保留右侧已输出内容。
- `Stop`：停止当前跟踪并卸载 agent。
- `Clear`：清空当前会话输出，计数从 1 重新开始。
- `Save`：把当前右侧跟踪内容保存到本地文本文件。
- `Help`：显示内置使用说明。

按钮状态会根据当前运行状态自动变化。开始跟踪后，`Start` 会变灰，避免重复点击；
停止或暂停后，`Start` 会恢复可用。

右下角显示版本标识：

```text
vibe coding  gll1989 V1.0
```

## 5. 目标进程列表

点击 `Refresh` 后，左侧会列出当前检测到的 Oracle OCI 进程。

列表中重点关注：

- PID。
- 进程名称。
- 进程位数。
- 程序路径。

当前只能选择 `x86` 目标进程。选择 64 位进程时，程序会拒绝开始跟踪。

## 6. 跟踪输出格式

每次 SQL 执行完成后，右侧输出一个完整 SQL 块。格式示例：

```text
1-----------------------------------------------------
select * from dba_objects where object_id=:object_id and owner=:owner
[1] object_id=123
[2] owner='SYS'
costs:[1.2] ms  end:[2026-05-17 10:30:15.123]
```

说明：

- 最前面的 `1-----------------------------------------------------` 是当前会话内递增计数。
- SQL 语句保持原始文本输出。
- 绑定变量按 `[序号] 名称=值` 输出。
- 字符类型绑定变量会加单引号。
- 数字类型不加单引号。
- `costs` 表示本次 OCI 执行耗时，单位毫秒。
- `end` 表示本次执行完成时间。

Commit / Rollback 示例：

```text
2-----------------------------------------------------
Commit;
costs:[0.5] ms  end:[2026-05-17 10:31:00.456]
```

```text
3-----------------------------------------------------
Rollback;
costs:[0.6] ms  end:[2026-05-17 10:31:03.789]
```

## 7. 绑定变量显示规则

当前已处理的常见绑定变量类型：

- 字符：`SQLT_CHR`、`SQLT_STR`、`SQLT_VCS`、`SQLT_AFC`、`SQLT_AVC`。
- 整数：`SQLT_INT`。
- 浮点：`SQLT_FLT`。
- Oracle NUMBER / VARNUM：`SQLT_NUM`、`SQLT_VNU`。
- 日期：`SQLT_DAT`。

字符示例：

```text
[1] branchCode='7180183200'
```

数字示例：

```text
[1] object_id=123
[2] amount=99.5
```

日期示例：

```text
[1] created=2026-05-17
[2] updated=2026-05-17 10:33:18
```

暂不识别的类型会输出类型号：

```text
[3] value=[dty:187]
```

如果仍看到类似 `[dty:6]` 的输出，需要重点确认目标程序传入的实际绑定内存格式，
因为当前已按 Oracle `SQLT_VNU` 做了解析。

## 8. 错误 SQL 输出

如果 SQL 执行失败，会在 SQL 块中输出 Oracle 错误信息，例如：

```text
4-----------------------------------------------------
select * from dba_object
ORA-00942: 表或视图不存在
costs:[1.5] ms  end:[2026-05-17 10:35:20.111]
```

错误信息来自 OCI 错误句柄。

## 9. 搜索和过滤

当前支持两类过滤：

- `costs >=`：按执行耗时过滤，单位毫秒。
- `SQL keyword`：按 SQL 文本关键字过滤，大小写不敏感。

过滤时会显示完整 SQL 块，不会只显示其中一行。

过滤不会写入历史数据库，只针对当前内存会话中的数据重建右侧显示内容。

## 10. 保存和清空

`Save`：

- 保存当前右侧可见内容。
- 默认文件名类似 `sqltracker-20260517-103500.txt`。
- 默认保存位置是当前用户的文档目录。
- 文件编码为 UTF-8。

`Clear`：

- 清空当前内存会话中的跟踪内容。
- 清空右侧显示。
- SQL 计数从 `1` 重新开始。

## 11. 性能优化策略

当前版本已经做了两层性能优化：

- agent 侧使用后台队列异步写管道，避免目标业务线程直接阻塞在输出管道上。
- UI 侧使用批量接收和增量追加显示，避免每条 SQL 都全量重绘输出窗口。

当 SQL 极高频输出、UI 来不及消费时，agent 会优先保护目标程序性能。极端情况下，
可能跳过部分绑定变量明细输出，但会尽量保留 SQL 执行结果。

## 12. 编码处理

SQL 和绑定变量文本会统一转换为 UTF-8 后发送到 UI。

当前处理顺序包括：

- ASCII。
- UTF-8。
- UTF-16LE。
- 本机 ANSI 代码页回退。

这用于兼容 SQLPlus、Toad、以及不同 Oracle 客户端字符集下的中文输出。

## 13. 发布包内容

自包含发布包目录中重点文件：

```text
SqlTracker.Windows.exe
SqlTracker.Windows.dll
SqlTracker.Core.dll
SqlTracker.Windows.deps.json
SqlTracker.Windows.runtimeconfig.json
sqltracker_oci_agent_win32.dll
README-RUN.txt
```

其中 `sqltracker_oci_agent_win32.dll` 必须和 `SqlTracker.Windows.exe` 放在同一目录，
否则无法注入目标进程。

旧版 `ociagent.dll` 不需要随发布包携带。

## 14. 常见问题

### 14.1 找不到目标进程

可能原因：

- 目标程序还没有连接 Oracle，尚未加载 `oci.dll`。
- 目标程序是 64 位进程。
- 权限不足，SqlTracker 无法读取目标进程模块。

处理方式：

- 先启动目标程序并完成 Oracle 连接。
- 点击 `Refresh`。
- 确认目标程序是 32 位。
- 必要时以管理员权限运行 SqlTracker。

### 14.2 点击 Start 后目标程序没有输出

可能原因：

- 目标进程已经注入过旧 DLL。
- 当前 SQL 没有通过 OCI 执行路径。
- 目标程序动态加载方式或 OCI 调用方式仍有未覆盖场景。

处理方式：

- 重启目标 Oracle 客户端进程。
- 重新点击 `Refresh` 和 `Start`。
- 在目标程序中重新执行 SQL。

### 14.3 更新 DLL 后为什么旧进程没变化

DLL 注入后已经加载到目标进程内存中。磁盘上的新 DLL 不会自动替换目标进程里的旧代码。

更新 agent 后，需要重启目标 Oracle 客户端进程，再重新注入。

### 14.4 跟踪后目标程序变慢

当前版本已经降低阻塞风险，但极高频 SQL 输出仍会带来额外开销。建议：

- 使用 `costs >=` 过滤减少 UI 显示压力。
- 避免长时间无限制跟踪高频批处理任务。
- 必要时分阶段跟踪关键操作。

## 15. 当前限制和后续方向

当前限制：

- 只支持 Windows x86 Oracle OCI。
- 暂不支持 Windows x64 Oracle OCI。
- 暂不支持 Linux。
- 暂不支持其他数据库客户端库。
- 绑定变量类型仍需根据真实业务场景继续补全。

后续方向：

- Windows x64 agent。
- Linux agent。
- 更多 Oracle 数据类型。
- 更强的大量输出渲染性能。
- MySQL、PostgreSQL、SQL Server 等其他数据库 agent。
