//! PDF ingestion example demonstrating how to ingest and search PDF documents.
//!
//! This example uses the "Attention Is All You Need" paper (1706.03762v7.pdf)
//! to demonstrate PDF text extraction, chunking, and semantic search.
//!
//! Run with: cargo run --example pdf_ingestion

use std::path::PathBuf;
use tempfile::tempdir;

use memvid_core::{Memvid, PutOptions, Result, SearchRequest};

fn main() -> Result<()> {
    // Create a temporary directory for our memory file
    let dir = tempdir().expect("failed to create temp dir");
    let mv2_path: PathBuf = dir.path().join("paper.mv2");

    // Path to the PDF file (relative to the examples directory)
    let pdf_path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("examples")
        .join("1706.03762v7.pdf");

    println!("=== Memvid PDF Ingestion Example ===\n");

    // ========================================
    // 1. CREATE a new memory file
    // ========================================
    println!("1. Creating memory file...");
    let mut mem = Memvid::create(&mv2_path)?;
    println!("   Memory created at {:?}\n", mv2_path);

    // ========================================
    // 2. INGEST the PDF file
    // ========================================
    println!("2. Ingesting PDF: {:?}", pdf_path);

    if !pdf_path.exists() {
        eprintln!("   ERROR: PDF file not found at {:?}", pdf_path);
        eprintln!("   Make sure you're running from the project root.");
        return Ok(());
    }

    // Read the PDF file
    let pdf_bytes = std::fs::read(&pdf_path)?;
    println!("   PDF size: {} bytes", pdf_bytes.len());

    // Put the PDF with metadata
    let options = PutOptions::builder()
        .title("Attention Is All You Need")
        .uri("mv2://papers/transformer.pdf")
        .tag("author", "Vaswani et al.")
        .tag("year", "2017")
        .tag("topic", "transformer")
        .build();

    let frame_id = mem.put_bytes_with_options(&pdf_bytes, options)?;
    println!("   Ingested as frame: {}", frame_id);

    // Commit changes
    mem.commit()?;
    println!("   Committed successfully!\n");

    // ========================================
    // 3. CHECK memory statistics
    // ========================================
    println!("3. Memory statistics:");
    let stats = mem.stats()?;
    println!("   Frame count: {}", stats.frame_count);
    println!("   Has lexical index: {}", stats.has_lex_index);
    println!();

    // ========================================
    // 4. SEARCH the ingested PDF
    // ========================================
    println!("4. Searching the paper...\n");

    // Search for "attention"
    let queries = [
        "attention mechanism",
        "transformer architecture",
        "self-attention",
        "encoder decoder",
        "positional encoding",
    ];

    for query in queries {
        let request = SearchRequest {
            query: query.to_string(),
            top_k: 3,
            snippet_chars: 150,
            uri: None,
            scope: None,
            cursor: None,
            #[cfg(feature = "temporal_track")]
            temporal: None,
            as_of_frame: None,
            as_of_ts: None,
            no_sketch: false,
        };

        let response = mem.search(request)?;
        println!("   Query: '{}'", query);
        println!(
            "   Hits: {} ({}ms)",
            response.total_hits, response.elapsed_ms
        );

        for (i, hit) in response.hits.iter().take(2).enumerate() {
            let snippet = hit
                .text
                .chars()
                .take(100)
                .collect::<String>()
                .replace('\n', " ");
            println!("   {}. {}...", i + 1, snippet);
        }
        println!();
    }

    // ========================================
    // 5. VERIFY file integrity
    // ========================================
    println!("5. Verifying file integrity...");
    drop(mem);
    let report = Memvid::verify(&mv2_path, false)?;
    println!("   Status: {:?}", report.overall_status);
    println!();

    println!("=== PDF ingestion example completed! ===");

    Ok(())
}
