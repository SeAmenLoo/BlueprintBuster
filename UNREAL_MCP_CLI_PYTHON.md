# Unreal MCP（UE 5.8 内置 ModelContextProtocol）CLI / Python 调用操作文档

本文档面向“直接通过 HTTP 调用 UE 的 MCP Server（默认 `http://127.0.0.1:8000/mcp`）”的场景，覆盖：
- 启动与验证 UE MCP Server
- HTTP + JSON-RPC 调用流程（session / protocol version）
- 命令行（curl）示例
- 纯标准库 Python 脚本示例（支持 tools/call 的 SSE 响应）

---

## 1. 前置条件：在 UE 中启用并启动 MCP Server

UE 5.8 自带 Experimental 插件：ModelContextProtocol（FriendlyName: Unreal MCP）。

1) 在 UE Editor 中启用插件
- Edit → Plugins → 搜索 “Model Context Protocol / Unreal MCP”
- 启用后重启 Editor

2) 配置监听地址（默认即可）
- Project Settings / Editor Preferences（取决于你放置配置的层级）中找到：
  - ServerUrlPath：默认 `/mcp`
  - ServerPortNumber：默认 `8000`
  - bAutoStartServer：默认 `false`

3) 启动方式
- 推荐：把 bAutoStartServer 设为 `true`，让 Editor 启动时自动注册路由并启动监听
- 或者：由你自己的模块在运行时调用 `StartServer(8000, "/mcp")`

4) 验证服务可达（最简单方式）
- 直接发一个 JSON-RPC `ping`（无需 session）：

```bash
curl -s -X POST "http://127.0.0.1:8000/mcp" ^
  -H "Content-Type: application/json" ^
  -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"
```

预期返回：
- HTTP 200
- JSON 形如：`{"jsonrpc":"2.0","id":1,"result":{}}`

---

## 2. 协议要点（UE 5.8 实现细节）

### 2.1 Endpoint 与请求方法
- Endpoint：`http://127.0.0.1:8000/mcp`
- 支持：
  - `POST`：JSON-RPC 调用（ping / initialize / tools/list / tools/call / resources/list / resources/read / notifications/*）
  - `DELETE`：关闭 session（需要 `Mcp-Session-Id` header）
- `GET`（SSE 独立通道）在 UE 5.8 当前实现中返回 405（不支持独立 SSE endpoint）

### 2.2 Header
- `Mcp-Session-Id`：`initialize` 成功后，服务端会在响应 header 中下发；后续请求必须带上
- `Mcp-Protocol-Version`：建议在 `initialize` 后的所有请求都带上，值为协商后的协议版本（UE 支持：`2025-11-25` / `2025-06-18` / `2024-11-05`）

### 2.3 tools/call 的返回是 EventStream（SSE 格式）

UE 的 `tools/call` 返回 `Content-Type: text/event-stream`，并在同一个 HTTP 响应上进行多次写入：
- 首包：空 body 的 event-stream 响应头（保持连接）
- 后续：可能推送 `notifications/progress`（如果你传了 progressToken）
- 最终：推送一条包含 JSON-RPC `result` 的 SSE message（其中 `id` 与请求 `id` 相同）

SSE message 形如：
```
event: message
data: {"jsonrpc":"2.0","id":123,"result":{...}}
```

---

## 3. curl 调用示例（完整握手 + list + call）

PowerShell 注意事项：
- PowerShell 里 `curl` 默认是 `Invoke-WebRequest` 的别名；请使用 `curl.exe`（Windows 自带）或改用 `Invoke-RestMethod`
- PowerShell 的换行续写符是反引号 `` ` ``（不是 `^`）
- PowerShell 的环境变量写法是 `$env:MCP_SESSION_ID`（不是 `%MCP_SESSION_ID%`）

下面示例以 PowerShell 为准，把 session id 存到环境变量 `$env:MCP_SESSION_ID` 里。

### 3.1 initialize（创建 session）

```powershell
curl.exe -i -s -X POST "http://127.0.0.1:8000/mcp" `
  -H "Content-Type: application/json" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"cli\",\"title\":\"cli\",\"version\":\"0.0\"}}}"
```

从响应头里取：
- `Mcp-Session-Id: <id>`

从响应 body 里取：
- `result.protocolVersion`（后续写入 `Mcp-Protocol-Version` header）

### 3.2 notifications/initialized（完成握手）

```powershell
curl.exe -s -X POST "http://127.0.0.1:8000/mcp" `
  -H "Content-Type: application/json" `
  -H "Mcp-Session-Id: $env:MCP_SESSION_ID" `
  -H "Mcp-Protocol-Version: 2025-11-25" `
  -d "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\",\"params\":{}}"
```

预期：HTTP 202（Accepted）

### 3.3 tools/list

```powershell
curl.exe -s -X POST "http://127.0.0.1:8000/mcp" `
  -H "Content-Type: application/json" `
  -H "Mcp-Session-Id: $env:MCP_SESSION_ID" `
  -H "Mcp-Protocol-Version: 2025-11-25" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"
```

### 3.4 tools/call（读取 SSE 输出）

`tools/call` 是长连接 event-stream，建议用 `curl -N`：

```powershell
curl.exe -N -s -X POST "http://127.0.0.1:8000/mcp" `
  -H "Content-Type: application/json" `
  -H "Mcp-Session-Id: $env:MCP_SESSION_ID" `
  -H "Mcp-Protocol-Version: 2025-11-25" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"YourToolName\",\"arguments\":{},\"_meta\":{\"progressToken\":\"p1\"}}}"
```

你会在输出里看到一到多条 `event: message` / `data: ...`：
- 可能先出现：`notifications/progress`
- 最后出现：`{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":...}`

### 3.5 resources/list 与 resources/read

```powershell
curl.exe -s -X POST "http://127.0.0.1:8000/mcp" `
  -H "Content-Type: application/json" `
  -H "Mcp-Session-Id: $env:MCP_SESSION_ID" `
  -H "Mcp-Protocol-Version: 2025-11-25" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"resources/list\",\"params\":{}}"
```

```powershell
curl.exe -s -X POST "http://127.0.0.1:8000/mcp" `
  -H "Content-Type: application/json" `
  -H "Mcp-Session-Id: $env:MCP_SESSION_ID" `
  -H "Mcp-Protocol-Version: 2025-11-25" `
  -d "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"resources/read\",\"params\":{\"uri\":\"<resource-uri>\"}}"
```

### 3.6 关闭 session（DELETE）

```powershell
curl.exe -s -X DELETE "http://127.0.0.1:8000/mcp" `
  -H "Mcp-Session-Id: $env:MCP_SESSION_ID"
```

预期：HTTP 202（Accepted）

---

## 4. Python 脚本示例（纯标准库，可直接当 CLI 用）

脚本文件位置（本项目已放好）：
- [mcp_http_client.py](file:///e:/Projects/UE/VRGame/git/PluginExample/Plugins/BlueprintBuster/Python/mcp_http_client.py)

### 4.1 环境变量

- `UNREAL_MCP_URL`
  - 含义：MCP Server 的 HTTP 地址（包含 path）
  - 默认：`http://127.0.0.1:8000/mcp`
- `UNREAL_MCP_STATE`
  - 含义：脚本保存 session 信息的状态文件路径
  - 默认：`.unreal_mcp_session.json`（在你运行脚本的当前目录下）

### 4.2 命令行参数（CLI）

通用参数：
- `--url <http(s)://host:port/path>`
  - 覆盖 `UNREAL_MCP_URL`，指定 MCP Server 地址

子命令：
- `ping`
  - 作用：发送 `ping`（无需 session），用于检测服务可达
- `init`
  - 作用：发送 `initialize`，并把返回的 `Mcp-Session-Id` 与 `protocolVersion` 写入状态文件
  - 输出：`{"session_id": "...", "protocol_version": "..."}`
- `initialized`
  - 作用：发送 `notifications/initialized`（需要已 `init` 的 session）
  - 输出：HTTP status（一般为 202）
- `tools-list`
  - 作用：发送 `tools/list`（需要已 `init` 的 session）
- `tools-call <name> <arguments_json> [--progress-token <token>]`
  - 作用：发送 `tools/call` 并以 SSE 方式等待最终结果
  - 参数：
    - `name`：工具名（`tools/list` 返回的 `tools[].name`）
    - `arguments_json`：工具参数 JSON（字符串形式），示例：`'{}'`、`'{"path":"..."}'`
    - `--progress-token`：可选；启用服务端 `notifications/progress` 心跳/进度通知
- `resources-list`
  - 作用：发送 `resources/list`（需要已 `init` 的 session）
- `resources-read <uri>`
  - 作用：发送 `resources/read`（需要已 `init` 的 session）
  - 参数：
    - `uri`：资源 URI（`resources/list` 返回的 `resources[].uri`）
- `close`
  - 作用：对当前 session 发送 `DELETE` 关闭，并删除状态文件

### 4.3 PowerShell 运行示例（推荐）

在项目根目录运行（确保状态文件写到项目根目录，方便管理）：

```powershell
cd e:\Projects\UE\VRGame\git\PluginExample

python .\Plugins\BlueprintBuster\Python\mcp_http_client.py ping
python .\Plugins\BlueprintBuster\Python\mcp_http_client.py init
python .\Plugins\BlueprintBuster\Python\mcp_http_client.py initialized
python .\Plugins\BlueprintBuster\Python\mcp_http_client.py tools-list

# 调用某个工具（示例参数是空对象）
python .\Plugins\BlueprintBuster\Python\mcp_http_client.py tools-call YourToolName '{}'

python .\Plugins\BlueprintBuster\Python\mcp_http_client.py resources-list
python .\Plugins\BlueprintBuster\Python\mcp_http_client.py resources-read "your://resource/uri"

python .\Plugins\BlueprintBuster\Python\mcp_http_client.py close
```

如果你的 MCP Server 地址不是默认值：

```powershell
python .\Plugins\BlueprintBuster\Python\mcp_http_client.py --url "http://127.0.0.1:8000/mcp" init
```

```python
import argparse
import json
import os
import sys
import http.client
from urllib.parse import urlparse


DEFAULT_URL = os.environ.get("UNREAL_MCP_URL", "http://127.0.0.1:8000/mcp")
STATE_PATH = os.environ.get("UNREAL_MCP_STATE", ".unreal_mcp_session.json")


def _load_state():
    if not os.path.exists(STATE_PATH):
        return {}
    with open(STATE_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def _save_state(state):
    with open(STATE_PATH, "w", encoding="utf-8") as f:
        json.dump(state, f, ensure_ascii=False, indent=2)


class McpHttpClient:
    def __init__(self, base_url):
        self.base_url = base_url
        u = urlparse(base_url)
        if u.scheme not in ("http", "https"):
            raise ValueError("Only http/https is supported")
        self.scheme = u.scheme
        self.host = u.hostname
        self.port = u.port or (443 if u.scheme == "https" else 80)
        self.path = u.path or "/mcp"

    def _conn(self):
        if self.scheme == "https":
            return http.client.HTTPSConnection(self.host, self.port, timeout=120)
        return http.client.HTTPConnection(self.host, self.port, timeout=120)

    def jsonrpc(self, method, params=None, request_id=None, session_id=None, protocol_version=None):
        body = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            body["params"] = params
        if request_id is not None:
            body["id"] = request_id
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")

        headers = {"Content-Type": "application/json"}
        if session_id:
            headers["Mcp-Session-Id"] = session_id
        if protocol_version:
            headers["Mcp-Protocol-Version"] = protocol_version

        conn = self._conn()
        conn.request("POST", self.path, body=data, headers=headers)
        resp = conn.getresponse()
        resp_body = resp.read()
        resp_headers = {k: v for (k, v) in resp.getheaders()}
        conn.close()

        content_type = resp_headers.get("Content-Type", "")
        if "application/json" in content_type:
            return resp.status, resp_headers, json.loads(resp_body.decode("utf-8"))
        return resp.status, resp_headers, resp_body

    def tools_call_stream(self, name, arguments, request_id, session_id, protocol_version, progress_token=None):
        params = {"name": name}
        if arguments is not None:
            params["arguments"] = arguments
        if progress_token is not None:
            params["_meta"] = {"progressToken": progress_token}

        body = {"jsonrpc": "2.0", "id": request_id, "method": "tools/call", "params": params}
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        headers = {
            "Content-Type": "application/json",
            "Mcp-Session-Id": session_id,
            "Mcp-Protocol-Version": protocol_version,
        }

        conn = self._conn()
        conn.request("POST", self.path, body=data, headers=headers)
        resp = conn.getresponse()

        if resp.status >= 400:
            raw = resp.read()
            conn.close()
            raise RuntimeError(f"HTTP {resp.status}: {raw[:2000]!r}")

        target_id = str(request_id)
        buffer = []

        while True:
            line = resp.readline()
            if not line:
                break
            s = line.decode("utf-8", errors="replace").rstrip("\r\n")
            if s == "":
                data_lines = [x for x in buffer if x.startswith("data: ")]
                buffer = []
                if not data_lines:
                    continue
                payload = "\n".join(x[6:] for x in data_lines).strip()
                if not payload:
                    continue
                try:
                    msg = json.loads(payload)
                except Exception:
                    sys.stdout.write(payload + "\n")
                    sys.stdout.flush()
                    continue

                if msg.get("method") == "notifications/progress":
                    sys.stdout.write(json.dumps(msg, ensure_ascii=False) + "\n")
                    sys.stdout.flush()
                    continue

                if str(msg.get("id")) == target_id and "result" in msg:
                    conn.close()
                    return msg["result"]
                continue

            buffer.append(s)

        conn.close()
        raise RuntimeError("Stream closed before receiving final result")

    def close_session(self, session_id):
        headers = {"Mcp-Session-Id": session_id}
        conn = self._conn()
        conn.request("DELETE", self.path, headers=headers)
        resp = conn.getresponse()
        resp.read()
        conn.close()
        return resp.status


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default=DEFAULT_URL)

    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ping")
    sub.add_parser("init")
    sub.add_parser("initialized")
    sub.add_parser("tools-list")

    p_call = sub.add_parser("tools-call")
    p_call.add_argument("name")
    p_call.add_argument("arguments_json")
    p_call.add_argument("--progress-token", default=None)

    sub.add_parser("resources-list")
    p_read = sub.add_parser("resources-read")
    p_read.add_argument("uri")

    sub.add_parser("close")

    args = ap.parse_args()
    c = McpHttpClient(args.url)
    state = _load_state()

    if args.cmd == "ping":
        status, _, body = c.jsonrpc("ping", request_id=1)
        print(status)
        print(json.dumps(body, ensure_ascii=False, indent=2))
        return

    if args.cmd == "init":
        params = {
            "protocolVersion": "2025-11-25",
            "capabilities": {},
            "clientInfo": {"name": "python", "title": "python", "version": "0.0"},
        }
        status, headers, body = c.jsonrpc("initialize", params=params, request_id=1)
        session_id = headers.get("Mcp-Session-Id") or headers.get("MCP-SESSION-ID") or headers.get("mcp-session-id")
        protocol_version = body.get("result", {}).get("protocolVersion")
        if not session_id or not protocol_version:
            raise RuntimeError(f"initialize failed: status={status}, headers={headers}, body={body}")
        state["session_id"] = session_id
        state["protocol_version"] = protocol_version
        _save_state(state)
        print(json.dumps({"session_id": session_id, "protocol_version": protocol_version}, ensure_ascii=False, indent=2))
        return

    session_id = state.get("session_id")
    protocol_version = state.get("protocol_version")
    if not session_id or not protocol_version:
        raise RuntimeError("No session. Run: python mcp_http_client.py init")

    if args.cmd == "initialized":
        status, _, _ = c.jsonrpc(
            "notifications/initialized",
            params={},
            request_id=None,
            session_id=session_id,
            protocol_version=protocol_version,
        )
        print(status)
        return

    if args.cmd == "tools-list":
        status, _, body = c.jsonrpc(
            "tools/list",
            params={},
            request_id=2,
            session_id=session_id,
            protocol_version=protocol_version,
        )
        print(status)
        print(json.dumps(body, ensure_ascii=False, indent=2))
        return

    if args.cmd == "tools-call":
        arguments = json.loads(args.arguments_json)
        result = c.tools_call_stream(
            name=args.name,
            arguments=arguments,
            request_id=3,
            session_id=session_id,
            protocol_version=protocol_version,
            progress_token=args.progress_token,
        )
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return

    if args.cmd == "resources-list":
        status, _, body = c.jsonrpc(
            "resources/list",
            params={},
            request_id=4,
            session_id=session_id,
            protocol_version=protocol_version,
        )
        print(status)
        print(json.dumps(body, ensure_ascii=False, indent=2))
        return

    if args.cmd == "resources-read":
        status, _, body = c.jsonrpc(
            "resources/read",
            params={"uri": args.uri},
            request_id=5,
            session_id=session_id,
            protocol_version=protocol_version,
        )
        print(status)
        print(json.dumps(body, ensure_ascii=False, indent=2))
        return

    if args.cmd == "close":
        status = c.close_session(session_id)
        if os.path.exists(STATE_PATH):
            os.remove(STATE_PATH)
        print(status)
        return


if __name__ == "__main__":
    main()
```

---

## 5. 常见问题排查

### 5.1 403 Forbidden（Origin 校验失败）
- UE 会拒绝带有非 localhost Origin 的请求（防 DNS Rebinding）
- 解决：
  - CLI / Python 通常不会带 Origin，保持默认即可
  - 若你在浏览器环境里调用，确保 Origin 属于 `localhost / 127.0.0.1 / [::1]`

### 5.2 tools/call 卡住或没有 JSON 输出
- 这是正常的 event-stream 行为：结果不会作为普通 JSON body 返回
- 解决：
  - curl 用 `-N` 保持流式输出
  - Python 必须逐行读取并解析 `data: ...` 段，直到出现 `id == 请求 id` 的 `result`

### 5.3 “Missing session id” / “Invalid session id”
- 确保顺序是：
  - `initialize` → 从响应头拿到 `Mcp-Session-Id`
  - `notifications/initialized`
  - 再调用 `tools/*`、`resources/*`
