//! Encryption capsule tests (.mv2e).

#[cfg(feature = "encryption")]
use memvid_core::encryption::{EncryptionError, Mv2eHeader, lock_file, unlock_file};
#[cfg(feature = "encryption")]
use memvid_core::{Memvid, PutOptions};

#[cfg(feature = "encryption")]
use std::fs::read;
#[cfg(feature = "encryption")]
use tempfile::TempDir;

#[test]
#[cfg(feature = "encryption")]
fn mv2e_header_roundtrip() {
    let header = Mv2eHeader {
        magic: memvid_core::encryption::MV2E_MAGIC,
        version: memvid_core::encryption::MV2E_VERSION,
        kdf_algorithm: memvid_core::encryption::KdfAlgorithm::Argon2id,
        cipher_algorithm: memvid_core::encryption::CipherAlgorithm::Aes256Gcm,
        salt: [1u8; memvid_core::encryption::SALT_SIZE],
        nonce: [2u8; memvid_core::encryption::NONCE_SIZE],
        original_size: 1024,
        reserved: [0u8; 4],
    };

    let encoded = header.encode();
    let decoded = Mv2eHeader::decode(&encoded).expect("decode");

    assert_eq!(decoded.magic, header.magic);
    assert_eq!(decoded.version, header.version);
    assert_eq!(decoded.salt, header.salt);
    assert_eq!(decoded.nonce, header.nonce);
    assert_eq!(decoded.original_size, header.original_size);
}

#[test]
#[cfg(feature = "encryption")]
fn lock_unlock_roundtrip_preserves_bytes() {
    let dir = TempDir::new().expect("tmp");
    let mv2_path = dir.path().join("test.mv2");
    let mv2e_path = dir.path().join("test.mv2e");
    let restored_path = dir.path().join("restored.mv2");

    {
        let mut mem = Memvid::create(&mv2_path).expect("create");
        mem.put_bytes_with_options(
            b"hello",
            PutOptions {
                title: Some("doc".to_string()),
                labels: vec!["note".to_string()],
                ..Default::default()
            },
        )
        .expect("put");
        mem.commit().expect("commit");
    }

    lock_file(&mv2_path, Some(mv2e_path.as_path()), b"test-password-123").expect("lock");
    unlock_file(
        &mv2e_path,
        Some(restored_path.as_path()),
        b"test-password-123",
    )
    .expect("unlock");

    let original = read(&mv2_path).expect("read original");
    let restored = read(&restored_path).expect("read restored");
    assert_eq!(original, restored);
}

#[test]
#[cfg(feature = "encryption")]
fn wrong_password_fails() {
    let dir = TempDir::new().expect("tmp");
    let mv2_path = dir.path().join("test.mv2");
    let mv2e_path = dir.path().join("test.mv2e");

    {
        let mut mem = Memvid::create(&mv2_path).expect("create");
        mem.put_bytes(b"hello").expect("put");
        mem.commit().expect("commit");
    }

    lock_file(&mv2_path, Some(mv2e_path.as_path()), b"password-a").expect("lock");
    let err = unlock_file(&mv2e_path, None, b"password-b").expect_err("should fail");
    assert!(matches!(err, EncryptionError::Decryption { .. }));
}
