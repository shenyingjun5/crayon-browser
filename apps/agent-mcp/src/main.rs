//! CAAP MCP read-only Developer Preview (AGT-14).
//!
//! stdio MCP server bridging initialize/tools-list/tools-call to the
//! product's CAAP agent-host over UDS. Loopback only; no external network.
//! Tool descriptions come from the host registry (read-only R0/R1 set).

use std::collections::BTreeMap;
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;

use crayon_domain::{AgentCapability, AgentTarget};
use crayon_ipc_schema::{CaapHello, CaapRequest, CaapWelcome, SchemaVersion};

const DEFAULT_PURPOSE: &str = "agent-caap";
const MCP_PROTOCOL_VERSION: &str = "2024-11-05";

fn json_response(id: serde_json::Value, result: serde_json::Value) -> String {
    format!(
        "{}\n",
        serde_json::json!({"jsonrpc": "2.0", "id": id, "result": result})
    )
}

fn json_error(id: serde_json::Value, code: i64, message: &str) -> String {
    format!(
        "{}\n",
        serde_json::json!({"jsonrpc": "2.0", "id": id, "error": {"code": code, "message": message}})
    )
}

const ERR_METHOD_NOT_FOUND: i64 = -32601;
const ERR_INTERNAL: i64 = -32603;

fn caap_tools() -> serde_json::Value {
    serde_json::json!([
        {"name": "page.list_targets", "description": "List page targets", "inputSchema": {"type": "object"}},
        {"name": "page.get_title", "description": "Read active tab title", "inputSchema": {"type": "object"}},
        {"name": "page.get_selection", "description": "Read selection", "inputSchema": {"type": "object"}},
        {"name": "page.snapshot", "description": "Take a bounded snapshot", "inputSchema": {"type": "object", "properties": {"format": {"type": "string"}, "max_bytes": {"type": "integer"}}}},
        {"name": "page.markdown", "description": "Extract markdown", "inputSchema": {"type": "object", "properties": {"max_bytes": {"type": "integer"}}}},
        {"name": "cast.list_receivers", "description": "List cast receivers", "inputSchema": {"type": "object"}},
        {"name": "cast.get_state", "description": "Read cast state", "inputSchema": {"type": "object"}},
    ])
}

struct CaapClient {
    stream: UnixStream,
    next_id: u64,
}

impl CaapClient {
    fn connect(purpose: &str) -> Result<Self, String> {
        let path = format!("/tmp/crayon-agent-{purpose}.sock");
        let stream = UnixStream::connect(&path).map_err(|e| format!("connect {path}: {e}"))?;
        stream
            .set_read_timeout(Some(std::time::Duration::from_secs(10)))
            .map_err(|e| e.to_string())?;
        Ok(Self { stream, next_id: 1 })
    }

    fn handshake(&mut self) -> Result<CaapWelcome, String> {
        let hello = CaapHello::new(
            SchemaVersion::CURRENT,
            "agent-mcp",
            vec![AgentCapability::PageRead],
        )
        .map_err(|e| e.to_string())?;
        let payload = serde_json::to_vec(&hello).map_err(|e| e.to_string())?;
        let header = (payload.len() as u32).to_be_bytes();
        self.stream
            .write_all(&header)
            .and_then(|_| self.stream.write_all(&payload))
            .map_err(|e| format!("send hello: {e}"))?;
        let bytes = self.read_frame()?;
        let welcome: CaapWelcome =
            serde_json::from_slice(&bytes).map_err(|e| format!("decode welcome: {e}"))?;
        Ok(welcome)
    }

    fn send_request(
        &mut self,
        tool: &str,
        params: BTreeMap<String, String>,
    ) -> Result<serde_json::Value, String> {
        let id = self.next_id;
        self.next_id += 1;
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis() as u64)
            .unwrap_or(0);
        let request = CaapRequest::new(
            id,
            tool,
            AgentTarget::ActiveTab,
            now + 60_000,
            &format!("mcp-{id}"),
            params,
        )
        .map_err(|e| e.to_string())?;
        let payload = serde_json::to_vec(&request).map_err(|e| e.to_string())?;
        let header = (payload.len() as u32).to_be_bytes();
        self.stream
            .write_all(&header)
            .and_then(|_| self.stream.write_all(&payload))
            .map_err(|e| format!("send request: {e}"))?;
        let bytes = self.read_frame()?;
        // Try chunk first.
        if let Ok(chunk) = serde_json::from_slice::<crayon_ipc_schema::CaapChunk>(&bytes) {
            if chunk.is_final() {
                return Ok(
                    serde_json::json!({"content": [{"type": "text", "text": chunk.data()}]}),
                );
            }
            // Multi-chunk: accumulate (simplified — read until final).
            let mut text = chunk.data().to_owned();
            loop {
                let bytes = self.read_frame()?;
                let chunk: crayon_ipc_schema::CaapChunk =
                    serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
                text.push_str(chunk.data());
                if chunk.is_final() {
                    return Ok(serde_json::json!({"content": [{"type": "text", "text": text}]}));
                }
                text.push_str(chunk.data());
            }
        }
        if let Ok(error) = serde_json::from_slice::<crayon_ipc_schema::CaapErrorReply>(&bytes) {
            return Ok(
                serde_json::json!({"isError": true, "error": format!("{:?}", error.error())}),
            );
        }
        Err("unexpected response type".to_owned())
    }

    fn read_frame(&mut self) -> Result<Vec<u8>, String> {
        let mut header = [0u8; 4];
        self.stream
            .read_exact(&mut header)
            .map_err(|e| format!("read header: {e}"))?;
        let len = u32::from_be_bytes(header) as usize;
        let mut payload = vec![0u8; len];
        self.stream
            .read_exact(&mut payload)
            .map_err(|e| format!("read body: {e}"))?;
        Ok(payload)
    }
}

fn main() {
    let purpose = std::env::var("CAAP_PURPOSE").unwrap_or_else(|_| DEFAULT_PURPOSE.to_owned());

    let mut client = match CaapClient::connect(&purpose) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("agent-mcp: CAAP connect failed: {e}");
            std::process::exit(1);
        }
    };

    match client.handshake() {
        Ok(_) => {}
        Err(e) => {
            eprintln!("agent-mcp: handshake failed: {e}");
            std::process::exit(1);
        }
    };

    let tools = caap_tools();
    let mut input = String::new();
    loop {
        input.clear();
        match std::io::stdin().read_line(&mut input) {
            Ok(0) | Err(_) => break,
            Ok(_) => {}
        }
        let request: serde_json::Value = match serde_json::from_str(input.trim()) {
            Ok(v) => v,
            Err(_) => continue,
        };
        let id = request
            .get("id")
            .cloned()
            .unwrap_or(serde_json::Value::Null);
        let method = request.get("method").and_then(|m| m.as_str()).unwrap_or("");
        let params = request.get("params").cloned().unwrap_or_default();

        let response = match method {
            "initialize" => json_response(
                id,
                serde_json::json!({
                    "protocolVersion": MCP_PROTOCOL_VERSION,
                    "capabilities": {"tools": {}},
                    "serverInfo": {
                        "name": "crayon-agent-mcp",
                        "version": "0.1.0"
                    }
                }),
            ),
            "tools/list" => json_response(id, serde_json::json!({"tools": tools})),
            "tools/call" => {
                let tool = params
                    .get("name")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_owned();
                let args = params
                    .get("arguments")
                    .and_then(|v| v.as_object())
                    .cloned()
                    .unwrap_or_default();
                let btree: BTreeMap<String, String> = args
                    .iter()
                    .filter_map(|(k, v)| v.as_str().map(|s| (k.clone(), s.to_owned())))
                    .collect();
                match client.send_request(&tool, btree) {
                    Ok(result) => {
                        let content = result
                            .get("content")
                            .cloned()
                            .unwrap_or(serde_json::json!([]));
                        json_response(id, serde_json::json!({"content": content}))
                    }
                    Err(e) => json_error(id, ERR_INTERNAL, &e),
                }
            }
            _ => json_error(id, ERR_METHOD_NOT_FOUND, "method not found"),
        };
        print!("{response}");
        let _ = std::io::stdout().flush();
    }
}
