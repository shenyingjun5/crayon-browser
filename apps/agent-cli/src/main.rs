//! CAAP CLI Developer Preview (AGT-13).
//!
//! Connects to the product's CAAP agent-host over macOS UDS. Supports
//! R0/R1 commands and `cancel`. R2+ invocations fail with `CapabilityDenied`
//! (the host requires AGT-05 confirmation which has no CLI surface).
//! `--json` outputs machine-readable results.

use std::collections::BTreeMap;
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;

use crayon_domain::{AgentCapability, AgentTarget};
use crayon_ipc_schema::{CaapChunk, CaapHello, CaapRequest, CaapWelcome, SchemaVersion};

const DEFAULT_PURPOSE: &str = "agent-caap";
const CLIENT_NAME: &str = "agent-cli";

struct Client {
    stream: UnixStream,
}

impl Client {
    fn connect(purpose: &str) -> Result<Self, String> {
        let path = format!("/tmp/crayon-agent-{purpose}.sock");
        let stream = UnixStream::connect(&path).map_err(|e| format!("connect {path}: {e}"))?;
        stream
            .set_read_timeout(Some(std::time::Duration::from_secs(10)))
            .map_err(|e| e.to_string())?;
        Ok(Self { stream })
    }

    fn send(&mut self, payload: &[u8]) -> Result<(), String> {
        let header = (payload.len() as u32).to_be_bytes();
        self.stream.write_all(&header).map_err(|e| e.to_string())?;
        self.stream.write_all(payload).map_err(|e| e.to_string())
    }

    fn recv(&mut self) -> Result<Vec<u8>, String> {
        let mut header = [0u8; 4];
        self.stream
            .read_exact(&mut header)
            .map_err(|e| format!("read: {e}"))?;
        let len = u32::from_be_bytes(header) as usize;
        let mut body = vec![0u8; len];
        self.stream
            .read_exact(&mut body)
            .map_err(|e| format!("read: {e}"))?;
        Ok(body)
    }
}

fn request_json(id: u64, result: &serde_json::Value) -> String {
    format!("{{\"id\":{id},\"result\":{result}}}")
}

fn error_json(id: u64, code: &str) -> String {
    format!("{{\"id\":{id},\"error\":\"{code}\"}}")
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() {
        eprintln!("usage: agent-cli [--purpose <token>] <command> [args]");
        eprintln!("commands: version targets get-title get-selection snapshot invoke cancel");
        std::process::exit(2);
    }

    let mut purpose = DEFAULT_PURPOSE.to_owned();
    let mut json = false;
    let mut rest = Vec::new();
    let mut skip = false;
    for arg in &args {
        if skip {
            skip = false;
            continue;
        }
        match arg.as_str() {
            "--json" => json = true,
            "--purpose" => {
                skip = true;
            }
            _ => rest.push(arg.clone()),
        }
    }

    let command = rest.first().map(String::as_str).unwrap_or("");
    let tool = match command {
        "version" | "targets" | "get-title" | "get-selection" | "snapshot" => {
            // These map to page.read tools.
            match command {
                "targets" => "page.list_targets",
                "get-title" => "page.get_title",
                "get-selection" => "page.get_selection",
                "snapshot" => "page.snapshot",
                _ => "page.get_title",
            }
        }
        "invoke" => {
            // Semantic action — R4, requires confirmation. Will fail with
            // CapabilityDenied (no CLI surface for AGT-05 confirmation).
            rest.get(1).map(String::as_str).unwrap_or("")
        }
        _ => {
            eprintln!("unknown command: {command}");
            std::process::exit(2);
        }
    };

    let mut client = match Client::connect(&purpose) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("error: {e}");
            std::process::exit(1);
        }
    };

    // Handshake.
    let hello = CaapHello::new(
        SchemaVersion::CURRENT,
        CLIENT_NAME,
        vec![AgentCapability::PageRead],
    )
    .expect("hello");
    client
        .send(&serde_json::to_vec(&hello).expect("ser hello"))
        .expect("send hello");
    let welcome_bytes = client.recv().expect("read welcome");
    let welcome: crayon_ipc_schema::CaapWelcome =
        serde_json::from_slice(&welcome_bytes).expect("decode welcome");

    // Issue grant via a side-channel is not available in CLI (no AGT-05
    // confirmation surface). The host grants PageRead on confirmation.
    // For the preview, we proceed and expect either success or a stable
    // error code from the gateway.

    // Build request.
    let params: BTreeMap<String, String> = rest
        .iter()
        .skip(1)
        .filter_map(|pair| {
            pair.split_once('=')
                .map(|(k, v)| (k.to_owned(), v.to_owned()))
        })
        .collect();

    let id = 1u64;
    let now_ms = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0);
    let request = CaapRequest::new(
        id,
        tool,
        AgentTarget::ActiveTab,
        now_ms + 60_000,
        &format!("cli-{id}"),
        params,
    )
    .expect("request");

    client
        .send(&serde_json::to_vec(&request).expect("ser request"))
        .expect("send request");

    // Read response (chunk or error).
    let response_bytes = client.recv().expect("read response");
    if let Ok(chunk) = serde_json::from_slice::<crayon_ipc_schema::CaapChunk>(&response_bytes) {
        if json {
            println!(
                "{{\"id\":{},\"data\":{:?},\"final\":{}}}",
                chunk.id(),
                chunk.data(),
                chunk.is_final()
            );
        } else {
            print!("{}", chunk.data());
            if chunk.is_final() {
                println!();
            }
        }
    } else if let Ok(error) =
        serde_json::from_slice::<crayon_ipc_schema::CaapErrorReply>(&response_bytes)
    {
        let code = format!("{:?}", error.error()).to_lowercase();
        if json {
            println!("{{\"id\":{},\"error\":\"{}\"}}", error.id(), code);
        } else {
            println!("error: {code}");
        }
    } else {
        eprintln!("unexpected response type");
        std::process::exit(1);
    }

    let _ = welcome;
}
