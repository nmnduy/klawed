/**
 * Chunked File Upload Client
 * Handles large file uploads by splitting them into chunks
 */

class ChunkedUploader {
    constructor(options = {}) {
        this.chunkSize = options.chunkSize || 5 * 1024 * 1024; // 5 MB default
        this.maxRetries = options.maxRetries || 3;
        this.retryDelay = options.retryDelay || 1000; // 1 second
        this.onProgress = options.onProgress || (() => {});
        this.onComplete = options.onComplete || (() => {});
        this.onError = options.onError || ((error) => console.error(error));
        this.baseUrl = options.baseUrl || '/file-chat/upload/chunked';
    }

    /**
     * Upload a file using chunked upload
     * @param {File} file - The file to upload
     * @param {string} sessionId - The session ID
     * @returns {Promise<object>} Upload result
     */
    async upload(file, sessionId) {
        try {
            // Initialize upload
            const initResponse = await fetch(`${this.baseUrl}/init`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                credentials: 'include',
                body: JSON.stringify({
                    sessionId: sessionId,
                    fileName: file.name,
                    totalSize: file.size
                })
            });

            if (!initResponse.ok) {
                const error = await initResponse.json();
                throw new Error(error.error || 'Failed to initialize upload');
            }

            const { uploadId, chunkSize, totalChunks } = await initResponse.json();
            this.chunkSize = chunkSize;

            console.log(`Starting chunked upload: ${file.name} (${totalChunks} chunks)`);

            // Upload chunks
            for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
                const start = chunkIndex * this.chunkSize;
                const end = Math.min(start + this.chunkSize, file.size);
                const chunk = file.slice(start, end);

                await this.uploadChunkWithRetry(uploadId, chunkIndex, chunk, file.name);

                // Update progress
                const progress = ((chunkIndex + 1) / totalChunks) * 100;
                this.onProgress({
                    fileName: file.name,
                    uploadedSize: end,
                    totalSize: file.size,
                    progress: progress,
                    chunkIndex: chunkIndex + 1,
                    totalChunks: totalChunks
                });
            }

            // Upload complete
            const result = {
                fileName: file.name,
                size: file.size,
                uploadId: uploadId
            };

            this.onComplete(result);
            console.log(`Upload complete: ${file.name}`);
            return result;

        } catch (error) {
            this.onError(error);
            throw error;
        }
    }

    /**
     * Upload a single chunk with retry logic
     */
    async uploadChunkWithRetry(uploadId, chunkIndex, chunk, fileName) {
        let lastError;

        for (let attempt = 0; attempt < this.maxRetries; attempt++) {
            try {
                const formData = new FormData();
                formData.append('uploadId', uploadId);
                formData.append('chunkIndex', chunkIndex);
                formData.append('chunk', new Blob([chunk]), `${fileName}.chunk${chunkIndex}`);

                const response = await fetch(`${this.baseUrl}/chunk`, {
                    method: 'POST',
                    credentials: 'include',
                    body: formData
                });

                if (!response.ok) {
                    const error = await response.json();
                    throw new Error(error.error || 'Failed to upload chunk');
                }

                return await response.json();

            } catch (error) {
                lastError = error;
                console.warn(`Chunk ${chunkIndex} upload failed (attempt ${attempt + 1}/${this.maxRetries}):`, error);

                if (attempt < this.maxRetries - 1) {
                    await this.sleep(this.retryDelay * (attempt + 1)); // Exponential backoff
                }
            }
        }

        throw new Error(`Failed to upload chunk ${chunkIndex} after ${this.maxRetries} attempts: ${lastError.message}`);
    }

    /**
     * Get upload status
     */
    async getStatus(uploadId) {
        const response = await fetch(`${this.baseUrl}/status/${uploadId}`, {
            method: 'GET',
            credentials: 'include'
        });

        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.error || 'Failed to get upload status');
        }

        return await response.json();
    }

    /**
     * Cancel an upload
     */
    async cancel(uploadId) {
        const response = await fetch(`${this.baseUrl}/${uploadId}`, {
            method: 'DELETE',
            credentials: 'include'
        });

        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.error || 'Failed to cancel upload');
        }

        return await response.json();
    }

    /**
     * Helper: sleep for specified milliseconds
     */
    sleep(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = ChunkedUploader;
}
