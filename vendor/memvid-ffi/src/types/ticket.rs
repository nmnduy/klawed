//! Ticket metadata exchanged with the control plane.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TicketRef {
    pub issuer: String,
    pub seq_no: i64,
    pub expires_in_secs: u64,
    #[serde(default)]
    pub capacity_bytes: u64,
}

/// Ticket information provided by the control plane.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Ticket {
    pub issuer: String,
    pub seq_no: i64,
    pub expires_in_secs: u64,
    pub capacity_bytes: Option<u64>,
}

impl Ticket {
    #[must_use]
    pub fn new<I: Into<String>>(issuer: I, seq_no: i64) -> Self {
        Self {
            issuer: issuer.into(),
            seq_no,
            expires_in_secs: 0,
            capacity_bytes: None,
        }
    }

    #[must_use]
    pub fn expires_in_secs(mut self, value: u64) -> Self {
        self.expires_in_secs = value;
        self
    }

    #[must_use]
    pub fn capacity_bytes(mut self, value: u64) -> Self {
        self.capacity_bytes = Some(value);
        self
    }
}
