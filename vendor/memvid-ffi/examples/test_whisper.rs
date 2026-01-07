// Test script to run whisper transcription
// Run with: cargo run -p memvid-core --features whisper --example test_whisper --release

#[cfg(feature = "whisper")]
use memvid_core::{WhisperConfig, WhisperTranscriber};
#[cfg(feature = "whisper")]
use std::path::Path;
#[cfg(feature = "whisper")]
use std::time::Instant;

#[cfg(not(feature = "whisper"))]
fn main() {
    eprintln!(
        "This example requires the `whisper` feature.\n\
         Re-run with:\n\
         cargo run -p memvid-core --features whisper --example test_whisper --release"
    );
}

#[cfg(feature = "whisper")]
fn main() {
    let audio_path = Path::new("/Users/olow/Desktop/memvid-org/call_sale.mp3");

    println!("Creating Whisper transcriber...");
    let start = Instant::now();
    let config = WhisperConfig::default();
    println!("Model dir: {:?}", config.models_dir);
    println!("Model name: {}", config.model_name);

    let mut transcriber = WhisperTranscriber::new(&config).expect("Failed to create transcriber");
    println!("Transcriber created in {:?}", start.elapsed());

    println!("\nTranscribing audio file: {}", audio_path.display());
    let start = Instant::now();

    match transcriber.transcribe_file(audio_path) {
        Ok(result) => {
            println!("Transcription completed in {:?}", start.elapsed());
            println!("\n=== Transcription Result ===");
            println!("Duration: {:.2} seconds", result.duration_secs);
            println!("Language: {}", result.language);
            println!("\nText:\n{}", result.text);

            if !result.segments.is_empty() {
                println!("\n=== Segments ===");
                for seg in &result.segments {
                    println!("[{:.2}s - {:.2}s] {}", seg.start, seg.end, seg.text);
                }
            }
        }
        Err(e) => {
            eprintln!("Transcription failed: {}", e);
        }
    }
}
