//! CAAP CLI Developer Preview (AGT-13).
//!
//! Connects to the product's CAAP agent-host over macOS UDS and issues
//! read-only commands (R0/R1). Machine-readable `--json` output.

use std::io::{Read, Write};
use std::os::unix::net::UnixStream;

use crayon_ipc_schema::{CaapHello, SchemaVersion};
use crayon_domain::AgentCapability;

const DEFAULT_PURPOSE: &str = "agent-caap";

struct Client {
    stream: UnixStream,
}

impl Client {
    fn connect(purpose: &str) -> Result<Self, String> {
        let socket_path = format!("/tmp/crayon-agent-{purpose}.sock");
        let stream = UnixStream::connect(&socket_path)
            .map_err(|e| format!("connect to {socket_path}: {e}"))?;
        stream
            .set_read_timeout(Some(std::time::Duration::from_secs(10)))
            .map_err(|e| format!("set timeout: {e}"))?;
        Ok(Self { stream })
    }

    fn send_frame(&mut self, payload: &[u8]) -> Result<(), String> {
        let header = (payload.len() as u32).to_be_bytes();
        self.stream.write_all(&header).map_err(|e| e.to_string())?;
        self.stream.write_all(payload).map_err(|e| e.to_string())
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
    let args: Vec<String> = std::env::args().skip(1).collect();
    let purpose = DEFAULT_PURPOSE.to_owned();
    let _ = &args;

    let mut client = match Client::connect(&purpose) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("error: {e}");
            std::process::exit(1);
        }
    };

    // Send hello.
    let hello = CaapHello::new(
        SchemaVersion::CURRENT,
        "agent-cli",
        vec![AgentCapability::PageRead],
    )
    .expect("hello");
    let payload = serde_json::to_vec(&hello).expect("serialize hello");
    let header = (payload.len() as u32).to_be_bytes();
    if let Err(e) = client.stream.write_all(&header) {
        eprintln!("error: write header: {e}");
        std::process::exit(1);
    }
    if let Err(e) = client.stream.write_all(&payload) {
        eprintln!("error: write hello: {e}");
        std::process::exit(1);
    }

    // Read welcome.
    let welcome_bytes = match client.read_frame() {
        Ok(data) => data,
        Err(e) => {
            eprintln!("error: read welcome: {e}");
            std::process::exit(1);
        }
    };
    let welcome: crayon_ipc_schema::CaapWelcome =
        serde_json::from_slice(&welcome_bytes).expect("decode welcome");

    println!(
        "connected: schema={} capabilities={:?}",
        welcome.schema().get(),
        welcome.capabilities()
    );
}
