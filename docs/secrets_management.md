# Discussion about secrets mangement

## Message 2 - USER

survey this app and tell me how we can do secrets management.

requirements: we can't read the secrets without the user. it must be safely retrieved when the user is connected. i think tools like lastpass makes it so that they can't even read their user password.

we want users to be able to achieve tasks, but also provide security and privacy for them. allowing them to trust us when they hand over some form of their secrets for filesurf to use and enable them to do more.

---

## Message 3 - ASSISTANT

Here’s a practical, “can’t-read-without-the-user” plan tailored for FileSurf:

## Core principle
- **Zero-knowledge to the server:** The backend only ever stores **ciphertext**. Decryption keys live in the user’s browser (or user’s hardware key store) and are never persisted on the server or logs.

## High-level flow
1. **Client-side encryption:**  
   - Use WebCrypto `SubtleCrypto` in the browser.  
   - Generate a **per-user master key** on first secret save. Protect it with a **key-encryption-key (KEK)** derived from the user’s passphrase (PBKDF2/Argon2id with high iterations + salt) or, even better, **WebAuthn/passkey** for hardware-backed protection.  
   - Store only the **wrapped master key** server-side (in DB). The unwrapped key exists only in memory in the browser while the user is present.

2. **Per-secret envelope:**  
   - For each secret: generate a random data-encryption-key (DEK), encrypt the secret with AEAD (AES-GCM/ChaCha20-Poly1305), and wrap the DEK with the user master key.  
   - Store `{ciphertext, iv/nonce, wrappedDEK, auth tag, created/rotated timestamps, usage policy}` in DB. No plaintext leaves the client.

3. **Retrieval (only when user is connected):**  
   - User unlocks with passphrase or WebAuthn assertion. Browser unwraps master key, unwraps DEK, and only then sends the **plaintext secret over a mutually authenticated channel to the backend** for immediate use—or keeps it in-browser if the action can stay client-side.  
   - The backend **never persists** plaintext; it only uses it in-memory for the immediate task, then discards.

4. **Transport & session binding:**  
   - Use HTTPS/WSS only. Bind a short-lived “secret session” to the existing FileSurf session.  
   - Consider an additional per-secret nonce/one-time token so a replay without the user’s presence can’t pull the secret again.

5. **Policies & UX:**  
   - **Ephemeral unlock:** Require re-unlock after inactivity (e.g., 10–30 minutes) or per sensitive action.  
   - **Scoped usage:** Let users tag secrets (e.g., “GitHub token”, “S3 upload”), set allowed domains/endpoints, and set expiry/rotation reminders.  
   - **No logs of plaintext:** Scrub/disable logging around secret-handling paths.

6. **Rotation & revocation:**  
   - Re-wrap secrets when passphrase/credential changes (re-derive KEK, unwrap master, re-wrap).  
   - Allow “burn after N uses” and explicit revoke.

7. **Device trust & backup:**  
   - Offer an optional **recovery key** (client-generated, user-stored).  
   - If WebAuthn-only and the user loses the authenticator, they’ll need recovery flow; be explicit in UX.

8. **Auditing:**  
   - Log *metadata only* (who, when, which secret ID, which action), never content.  
   - Alert on anomalous access (unusual IP, rapid repeats).

9. **Server-side safeguards for in-memory use:**  
   - Zeroize buffers after use.  
   - Avoid caching secrets in JVM logs/stack traces.  
   - Keep secret-handling code paths narrow and reviewed.

10. **Threat model fit to your ask (“we can’t read secrets without user”):**  
    - Server holds ciphertext + wrapped keys only.  
    - Decryption requires user presence (passphrase or WebAuthn challenge).  
    - Without that, the server operators cannot decrypt.  
    - If you need to run automated jobs, require a scheduled “unlock window” where the user explicitly approves (push/WebAuthn), or use a separate “automation key” the user consents to store—kept distinct and clearly labeled as less private.

## Implementation sketch for FileSurf
- Add a `secrets` table: `id, user_id, ciphertext, wrapped_dek, iv, alg, created_at, rotated_at, usage_policy`.
- Add a `user_keys` table: `user_id, wrapped_master_key, kdf_params`.
- Frontend: small crypto helper using WebCrypto for wrap/unwrap, AEAD encrypt/decrypt, passphrase/WebAuthn flows.
- Backend (Quarkus): APIs to store/retrieve ciphertext blobs, but **never** accept plaintext storage. Enforce HTTPS, short-lived secret sessions, and redact logging.
- Optional: For AI/agent actions, require an explicit per-use grant so the secret is fetched/decrypted only while the user is online.

If you want, I can draft:
- The Quarkus API contract (store/retrieve with metadata-only logging).
- The WebCrypto flow (passphrase and WebAuthn variants).
- A migration script for the new tables and a minimal Java service to enforce “no-plaintext persistence” checks.

---

