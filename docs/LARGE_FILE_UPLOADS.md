# Large File Upload Handling

## Overview

FileSurf v2 supports two upload methods:
1. **Standard Upload** - For files up to 100 MB (simple, synchronous)
2. **Chunked Upload** - For files up to 1 GB (resumable, efficient)

## Standard Upload (< 100 MB)

### Configuration

The standard upload endpoint handles files in a single request:

```properties
# application.properties
quarkus.http.limits.max-body-size=110M
quarkus.http.limits.max-form-attribute-size=110M
```

```java
// FileUploadResource.java
private static final long MAX_FILE_SIZE = 100 * 1024 * 1024; // 100 MB
```

### Usage

```javascript
const formData = new FormData();
formData.append('files', file);

const response = await fetch('/file-chat/upload', {
    method: 'POST',
    credentials: 'include',
    body: formData
});
```

### Pros & Cons

**Pros:**
- Simple implementation
- Single HTTP request
- No state management needed

**Cons:**
- Loads entire file into memory
- No progress tracking for partial uploads
- Cannot resume if connection fails
- Server timeout for very large files

## Chunked Upload (100 MB - 1 GB)

### Configuration

Chunked uploads split large files into smaller pieces:

```java
// ChunkedUploadResource.java
private static final long MAX_FILE_SIZE = 1024L * 1024 * 1024; // 1 GB
private static final long CHUNK_SIZE = 5 * 1024 * 1024; // 5 MB chunks
```

### API Endpoints

#### 1. Initialize Upload
```http
POST /file-chat/upload/chunked/init
Content-Type: application/json

{
  "sessionId": "session-uuid",
  "fileName": "large-video.mp4",
  "totalSize": 524288000
}

Response:
{
  "uploadId": "upload-uuid",
  "chunkSize": 5242880,
  "totalChunks": 100
}
```

#### 2. Upload Chunk
```http
POST /file-chat/upload/chunked/chunk
Content-Type: multipart/form-data

uploadId: upload-uuid
chunkIndex: 0
chunk: <binary data>

Response:
{
  "uploadedSize": 5242880,
  "totalSize": 524288000,
  "complete": false
}
```

#### 3. Get Status
```http
GET /file-chat/upload/chunked/status/{uploadId}

Response:
{
  "uploadId": "upload-uuid",
  "uploadedSize": 52428800,
  "totalSize": 524288000,
  "complete": false
}
```

#### 4. Cancel Upload
```http
DELETE /file-chat/upload/chunked/{uploadId}

Response:
{
  "message": "Upload cancelled"
}
```

### JavaScript Client

```javascript
// Import the ChunkedUploader class
const uploader = new ChunkedUploader({
    chunkSize: 5 * 1024 * 1024, // 5 MB chunks
    maxRetries: 3,
    retryDelay: 1000,
    
    onProgress: (progress) => {
        console.log(`Progress: ${progress.progress.toFixed(2)}%`);
        console.log(`Uploaded: ${progress.uploadedSize} / ${progress.totalSize} bytes`);
        console.log(`Chunk: ${progress.chunkIndex} / ${progress.totalChunks}`);
    },
    
    onComplete: (result) => {
        console.log('Upload complete:', result.fileName);
    },
    
    onError: (error) => {
        console.error('Upload failed:', error.message);
    }
});

// Upload a file
const file = document.getElementById('fileInput').files[0];
const sessionId = getCurrentSessionId();

await uploader.upload(file, sessionId);
```

### Features

#### 1. **Progress Tracking**
- Real-time progress updates
- Chunk-level granularity
- Bytes uploaded vs total size

#### 2. **Automatic Retry**
- Failed chunks are automatically retried
- Configurable retry count (default: 3)
- Exponential backoff between retries

#### 3. **Resumable Uploads**
- Upload sessions persist for 24 hours
- Can check status and resume from last successful chunk
- Network interruptions don't lose progress

#### 4. **MD5 Verification**
- Server calculates MD5 hash during upload
- Returned in completion response
- Can be used to verify integrity

#### 5. **Memory Efficient**
- Server writes chunks directly to disk
- Minimal memory footprint
- Handles multiple concurrent uploads

### Example: Complete Upload Flow

```javascript
// 1. Create uploader with progress bar
const uploader = new ChunkedUploader({
    onProgress: (progress) => {
        const percent = progress.progress.toFixed(1);
        document.getElementById('progressBar').style.width = percent + '%';
        document.getElementById('progressText').textContent = 
            `${percent}% (${formatBytes(progress.uploadedSize)} / ${formatBytes(progress.totalSize)})`;
    },
    
    onComplete: (result) => {
        showSuccess(`Upload complete: ${result.fileName}`);
        refreshFileList();
    },
    
    onError: (error) => {
        showError(`Upload failed: ${error.message}`);
    }
});

// 2. Handle file selection
document.getElementById('fileInput').addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    
    // Show upload UI
    document.getElementById('uploadSection').style.display = 'block';
    document.getElementById('fileName').textContent = file.name;
    
    try {
        // Start upload
        await uploader.upload(file, currentSessionId);
    } catch (error) {
        console.error('Upload error:', error);
    }
});

// 3. Format bytes helper
function formatBytes(bytes) {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i];
}
```

### Resuming Interrupted Uploads

```javascript
// Save uploadId to localStorage or database
localStorage.setItem('pendingUpload', uploadId);

// Later, check status and resume
const uploadId = localStorage.getItem('pendingUpload');
if (uploadId) {
    const status = await uploader.getStatus(uploadId);
    if (!status.complete) {
        console.log(`Resuming upload: ${status.uploadedSize} / ${status.totalSize}`);
        // Resume upload logic here
    } else {
        localStorage.removeItem('pendingUpload');
    }
}
```

## Performance Considerations

### Network Bandwidth
- **Standard Upload**: Uses full bandwidth for single transfer
- **Chunked Upload**: May have slight overhead due to multiple HTTP requests
- **Recommendation**: Use standard for fast networks, chunked for unstable connections

### Server Memory
- **Standard Upload**: Memory usage = file size × concurrent uploads
- **Chunked Upload**: Memory usage = chunk size (5 MB) × concurrent uploads
- **Recommendation**: Use chunked for servers with limited memory

### Storage
- **Temporary Files**: Chunked uploads create temporary files during upload
- **Cleanup**: Stale uploads (>24 hours) are automatically cleaned up
- **Location**: System temp directory (configurable)

## Security Considerations

### Authentication
- All upload endpoints require valid `filesurf_userId` cookie
- Upload sessions are tied to specific user and session
- Cannot access or resume another user's upload

### Validation
- File size limits enforced at both init and chunk level
- Filename sanitization prevents path traversal
- Upload sessions expire after 24 hours

### Rate Limiting
Consider adding rate limiting for:
- Upload initialization (prevent session spam)
- Chunk uploads (prevent bandwidth abuse)
- Concurrent uploads per user

Example rate limit configuration (future enhancement):
```properties
# Limit: 5 uploads per minute per user
upload.rate-limit.requests=5
upload.rate-limit.window=60s
```

## Troubleshooting

### Upload Fails with 413 Error
```bash
# Increase HTTP body size limits
# application.properties:
quarkus.http.limits.max-body-size=110M
```

### Chunks Fail Randomly
- Check network stability
- Increase `maxRetries` in client
- Reduce `chunkSize` for unstable networks

### Memory Issues
- Monitor JVM heap usage: `jconsole` or `jvisualvm`
- Reduce concurrent upload limit
- Ensure chunked upload is being used for large files

### Stale Upload Sessions
```java
// Manual cleanup (if automatic cleanup isn't working)
ChunkedUploadResource.cleanupStaleUploads();
```

## Future Enhancements

1. **Parallel Chunk Upload**
   - Upload multiple chunks simultaneously
   - Faster transfers for high-bandwidth connections

2. **Client-Side Compression**
   - Compress chunks before upload
   - Reduce bandwidth usage

3. **Upload Queue**
   - Queue multiple files
   - Background upload with notifications

4. **Progress Persistence**
   - Save progress to database
   - Resume across browser sessions/devices

5. **Direct S3 Upload**
   - Generate presigned URLs
   - Upload directly to object storage
   - Bypass application server

## Monitoring

### Metrics to Track
- Upload success rate
- Average upload time per file size
- Chunk retry rate
- Active upload sessions
- Storage usage

### Logging
```java
// Upload started
LOGGER.info("Initialized chunked upload: " + uploadId + 
           ", file=" + fileName + 
           ", size=" + totalSize);

// Chunk uploaded
LOGGER.info("Uploaded chunk " + chunkIndex + 
           " for " + uploadId + 
           " (" + uploadedSize + "/" + totalSize + " bytes)");

// Upload completed
LOGGER.info("Completed upload: " + uploadId + 
           ", file=" + fileName + 
           ", size=" + uploadedSize + 
           ", md5=" + md5Hash);
```

## References

- Quarkus HTTP Configuration: https://quarkus.io/guides/http-reference
- RESTEasy Reactive Multipart: https://quarkus.io/guides/rest-json#multipart-support
- File Upload Best Practices: https://www.owasp.org/index.php/Unrestricted_File_Upload
