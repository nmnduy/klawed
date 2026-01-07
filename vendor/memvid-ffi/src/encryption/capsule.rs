use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use atomic_write_file::AtomicWriteFile;
use rand::RngCore;
use rand::rngs::OsRng;
use zeroize::Zeroize;

use crate::encryption::constants::{MV2E_MAGIC, MV2E_VERSION, NONCE_SIZE, SALT_SIZE};
use crate::encryption::crypto::{decrypt, derive_key, encrypt};
use crate::encryption::error::EncryptionError;
use crate::encryption::types::{CipherAlgorithm, KdfAlgorithm, Mv2eHeader};

/// Lock (encrypt) an `.mv2` file into a `.mv2e` capsule.
pub fn lock_file(
    input: impl AsRef<Path>,
    output: Option<&Path>,
    password: &[u8],
) -> Result<PathBuf, EncryptionError> {
    let input = input.as_ref();
    validate_mv2_file(input)?;

    let plaintext = std::fs::read(input).map_err(|source| EncryptionError::Io {
        source,
        path: Some(input.to_path_buf()),
    })?;

    let mut salt = [0u8; SALT_SIZE];
    let mut nonce = [0u8; NONCE_SIZE];
    OsRng.fill_bytes(&mut salt);
    OsRng.fill_bytes(&mut nonce);

    let mut key = derive_key(password, &salt)?;
    let ciphertext = encrypt(&plaintext, &key, &nonce)?;

    let header = Mv2eHeader {
        magic: MV2E_MAGIC,
        version: MV2E_VERSION,
        kdf_algorithm: KdfAlgorithm::Argon2id,
        cipher_algorithm: CipherAlgorithm::Aes256Gcm,
        salt,
        nonce,
        original_size: plaintext.len() as u64,
        reserved: [0u8; 4],
    };

    let output_path = output
        .map(PathBuf::from)
        .unwrap_or_else(|| input.with_extension("mv2e"));

    write_atomic(&output_path, |writer| {
        writer.write_all(&header.encode())?;
        writer.write_all(&ciphertext)?;
        Ok(())
    })?;

    key.zeroize();

    Ok(output_path)
}

/// Unlock (decrypt) an `.mv2e` capsule into an `.mv2` file.
pub fn unlock_file(
    input: impl AsRef<Path>,
    output: Option<&Path>,
    password: &[u8],
) -> Result<PathBuf, EncryptionError> {
    let input = input.as_ref();

    let mut file = File::open(input).map_err(|source| EncryptionError::Io {
        source,
        path: Some(input.to_path_buf()),
    })?;

    let mut header_bytes = [0u8; Mv2eHeader::SIZE];
    file.read_exact(&mut header_bytes)
        .map_err(|source| EncryptionError::Io {
            source,
            path: Some(input.to_path_buf()),
        })?;
    let header = Mv2eHeader::decode(&header_bytes)?;

    let mut ciphertext = Vec::new();
    file.read_to_end(&mut ciphertext)
        .map_err(|source| EncryptionError::Io {
            source,
            path: Some(input.to_path_buf()),
        })?;

    let mut key = derive_key(password, &header.salt)?;
    let plaintext = decrypt(&ciphertext, &key, &header.nonce)?;
    key.zeroize();

    if plaintext.len() as u64 != header.original_size {
        return Err(EncryptionError::SizeMismatch {
            expected: header.original_size,
            actual: plaintext.len() as u64,
        });
    }

    validate_mv2_bytes(&plaintext)?;

    let output_path = output
        .map(PathBuf::from)
        .unwrap_or_else(|| input.with_extension("mv2"));

    write_atomic(&output_path, |writer| {
        writer.write_all(&plaintext)?;
        Ok(())
    })?;

    Ok(output_path)
}

fn validate_mv2_file(path: &Path) -> Result<(), EncryptionError> {
    let mut file = File::open(path).map_err(|source| EncryptionError::Io {
        source,
        path: Some(path.to_path_buf()),
    })?;

    let mut magic = [0u8; 4];
    file.read_exact(&mut magic)
        .map_err(|source| EncryptionError::Io {
            source,
            path: Some(path.to_path_buf()),
        })?;

    // Plain `.mv2` files start with "MV2\0".
    if magic != *b"MV2\0" {
        return Err(EncryptionError::NotMv2File {
            path: path.to_path_buf(),
        });
    }

    Ok(())
}

fn validate_mv2_bytes(bytes: &[u8]) -> Result<(), EncryptionError> {
    if bytes.len() < 4 || &bytes[0..4] != b"MV2\0" {
        return Err(EncryptionError::CorruptedDecryption);
    }
    Ok(())
}

fn write_atomic<F>(path: &Path, write_fn: F) -> Result<(), EncryptionError>
where
    F: FnOnce(&mut File) -> std::io::Result<()>,
{
    let mut options = AtomicWriteFile::options();
    options.read(false);
    let mut atomic = options.open(path).map_err(|source| EncryptionError::Io {
        source,
        path: Some(path.to_path_buf()),
    })?;

    let file = atomic.as_file_mut();
    file.set_len(0).map_err(|source| EncryptionError::Io {
        source,
        path: Some(path.to_path_buf()),
    })?;
    file.seek(SeekFrom::Start(0))
        .map_err(|source| EncryptionError::Io {
            source,
            path: Some(path.to_path_buf()),
        })?;
    write_fn(file).map_err(|source| EncryptionError::Io {
        source,
        path: Some(path.to_path_buf()),
    })?;
    file.flush().map_err(|source| EncryptionError::Io {
        source,
        path: Some(path.to_path_buf()),
    })?;
    file.sync_all().map_err(|source| EncryptionError::Io {
        source,
        path: Some(path.to_path_buf()),
    })?;
    atomic.commit().map_err(|source| EncryptionError::Io {
        source,
        path: Some(path.to_path_buf()),
    })?;
    Ok(())
}
