use crate::error::{MemvidError, Result};
use crate::memvid::lifecycle::Memvid;
use crate::types::{FrameStatus, Stats, Ticket, TicketRef};

impl Memvid {
    pub fn stats(&self) -> Result<Stats> {
        let metadata = self.file.metadata()?;
        let mut payload_bytes = 0u64;
        let mut logical_bytes = 0u64;
        let mut active_frames = 0u64;

        for frame in self
            .toc
            .frames
            .iter()
            .filter(|frame| frame.status == FrameStatus::Active)
        {
            active_frames = active_frames.saturating_add(1);
            let stored = frame.payload_length;
            payload_bytes = payload_bytes.saturating_add(stored);
            if stored > 0 {
                let logical = frame.canonical_length.unwrap_or(stored);
                logical_bytes = logical_bytes.saturating_add(logical);
            }
        }

        let saved_bytes = logical_bytes.saturating_sub(payload_bytes);
        let round2 = |value: f64| (value * 100.0).round() / 100.0;
        let compression_ratio_percent = if logical_bytes > 0 {
            round2((payload_bytes as f64 / logical_bytes as f64) * 100.0)
        } else {
            100.0
        };
        let savings_percent = if logical_bytes > 0 {
            round2((saved_bytes as f64 / logical_bytes as f64) * 100.0)
        } else {
            0.0
        };
        let storage_utilisation_percent = if self.capacity_limit() > 0 {
            round2((metadata.len() as f64 / self.capacity_limit() as f64) * 100.0)
        } else {
            0.0
        };
        let remaining_capacity_bytes = self.capacity_limit().saturating_sub(metadata.len());
        let average_payload = if active_frames > 0 {
            payload_bytes / active_frames
        } else {
            0
        };
        let average_logical = if active_frames > 0 {
            logical_bytes / active_frames
        } else {
            0
        };

        // PHASE 2: Calculate detailed overhead breakdown for observability
        let wal_bytes = self.header.wal_size;

        let mut lex_index_bytes = 0u64;
        if let Some(ref lex) = self.toc.indexes.lex {
            lex_index_bytes = lex_index_bytes.saturating_add(lex.bytes_length);
        }
        for seg in &self.toc.indexes.lex_segments {
            lex_index_bytes = lex_index_bytes.saturating_add(seg.bytes_length);
        }

        let mut vec_index_bytes = 0u64;
        let mut vector_count = 0u64;
        if let Some(ref vec) = self.toc.indexes.vec {
            vec_index_bytes = vec_index_bytes.saturating_add(vec.bytes_length);
            vector_count = vector_count.saturating_add(vec.vector_count);
        }
        for seg in &self.toc.segment_catalog.vec_segments {
            vec_index_bytes = vec_index_bytes.saturating_add(seg.common.bytes_length);
            vector_count = vector_count.saturating_add(seg.vector_count);
        }

        let mut time_index_bytes = 0u64;
        if let Some(ref time) = self.toc.time_index {
            time_index_bytes = time_index_bytes.saturating_add(time.bytes_length);
        }
        for seg in &self.toc.segment_catalog.time_segments {
            time_index_bytes = time_index_bytes.saturating_add(seg.common.bytes_length);
        }

        // CLIP image count from clip index manifest
        let clip_image_count = self
            .toc
            .indexes
            .clip
            .as_ref()
            .map(|c| c.vector_count)
            .unwrap_or(0);

        Ok(Stats {
            frame_count: self.toc.frames.len() as u64,
            size_bytes: metadata.len(),
            tier: self.tier(),
            // Use consolidated helper for consistent lex index detection
            has_lex_index: crate::memvid::lifecycle::has_lex_index(&self.toc),
            has_vec_index: self.toc.indexes.vec.is_some()
                || !self.toc.segment_catalog.vec_segments.is_empty(),
            has_clip_index: self.toc.indexes.clip.is_some(),
            has_time_index: self.toc.time_index.is_some()
                || !self.toc.segment_catalog.time_segments.is_empty(),
            seq_no: (self.toc.ticket_ref.seq_no != 0).then_some(self.toc.ticket_ref.seq_no),
            capacity_bytes: self.capacity_limit(),
            active_frame_count: active_frames,
            payload_bytes,
            logical_bytes,
            saved_bytes,
            compression_ratio_percent,
            savings_percent,
            storage_utilisation_percent,
            remaining_capacity_bytes,
            average_frame_payload_bytes: average_payload,
            average_frame_logical_bytes: average_logical,
            wal_bytes,
            lex_index_bytes,
            vec_index_bytes,
            time_index_bytes,
            vector_count,
            clip_image_count,
        })
    }

    pub fn apply_ticket(&mut self, ticket: Ticket) -> Result<()> {
        self.ensure_writable()?;
        let current_seq = self.toc.ticket_ref.seq_no;
        if ticket.seq_no <= current_seq {
            return Err(MemvidError::TicketSequence {
                expected: current_seq + 1,
                actual: ticket.seq_no,
            });
        }

        self.toc.ticket_ref.capacity_bytes = ticket.capacity_bytes.unwrap_or(0);
        self.toc.ticket_ref.issuer = ticket.issuer;
        self.toc.ticket_ref.seq_no = ticket.seq_no;
        self.toc.ticket_ref.expires_in_secs = ticket.expires_in_secs;

        self.generation = self.generation.wrapping_add(1);
        self.rewrite_toc_footer()?;
        self.header.toc_checksum = self.toc.toc_checksum;
        crate::persist_header(&mut self.file, &self.header)?;
        self.file.sync_all()?;
        Ok(())
    }

    pub fn current_ticket(&self) -> TicketRef {
        self.toc.ticket_ref.clone()
    }

    /// Returns a reference to the Logic-Mesh manifest, if present.
    pub fn logic_mesh_manifest(&self) -> Option<&crate::types::LogicMeshManifest> {
        self.toc.logic_mesh.as_ref()
    }
}
