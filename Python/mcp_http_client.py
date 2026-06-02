
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
        resp_headers = {k.lower(): v for (k, v) in resp.getheaders()}
        conn.close()

        content_type = resp_headers.get("content-type", "").lower()
        if "application/json" in content_type:
            return resp.status, resp_headers, json.loads(resp_body.decode("utf-8"))
        try:
            text = resp_body.decode("utf-8")
            if text.lstrip().startswith("{") or text.lstrip().startswith("["):
                return resp.status, resp_headers, json.loads(text)
        except Exception:
            pass
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
        session_id = headers.get("mcp-session-id")
        if isinstance(body, dict):
            protocol_version = body.get("result", {}).get("protocolVersion")
        else:
            protocol_version = None
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
