# tools

浏览器端内容编辑工具。

| 文件 | 说明 |
|------|------|
| `editor.html` | 通用编辑器 |
| `editor_server.py` | 本地 API，直接读写 `server_content/` |

## 推荐：直连 server_content 写入

在项目根目录运行：

```bash
python3 tools/editor_server.py
```

浏览器打开 **http://127.0.0.1:8765/** ，编辑器会自动加载并连接 API。  
修改后点 **💾 保存** 或 `Ctrl+S`，变更会直接写入 `server_content/` 对应 JSON 文件。

可选参数：`--port 8765`、`--host 127.0.0.1`

## 离线模式（备选）

双击 `editor.html` 或通过「📂 打开」选择 `server_content/` 文件夹（需 Chrome/Edge 的目录读写权限）。  
若浏览器不支持目录写入，保存时会触发文件下载，需手动覆盖原文件。
