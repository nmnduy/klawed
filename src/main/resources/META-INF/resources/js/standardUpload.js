/**
 * Standard File Upload with Progress
 * Handles standard file uploads with progress tracking using XMLHttpRequest
 */

class StandardUploader {
    /**
     * Upload files with progress tracking
     * @param {File[]} files - Files to upload
     * @param {string} sessionId - Session ID
     * @param {string} path - Upload path (optional)
     * @param {Object} options - Additional options
     * @returns {Promise<object>} Upload result
     */
    static async upload(files, sessionId, path = '/', options = {}) {
        const useProgressUI = options.useProgressUI !== false;
        const uploadId = 'standard-' + Date.now() + '-' + Math.random().toString(36).substr(2, 9);
        
        // Calculate total size
        const totalSize = files.reduce((sum, file) => sum + file.size, 0);
        const fileNames = files.map(f => f.name).join(', ');

        return new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            
            // Show progress UI if enabled
            if (useProgressUI && typeof window !== 'undefined' && window.uploadProgress) {
                window.uploadProgress.showProgress(
                    uploadId,
                    files.length === 1 ? files[0].name : `${files.length} files`,
                    totalSize
                );
            }

            // Track upload progress
            xhr.upload.addEventListener('progress', (e) => {
                if (e.lengthComputable) {
                    const progress = (e.loaded / e.total) * 100;
                    
                    if (useProgressUI && typeof window !== 'undefined' && window.uploadProgress) {
                        window.uploadProgress.updateProgress(uploadId, progress, e.loaded);
                    }

                    // Call custom progress callback if provided
                    if (options.onProgress) {
                        options.onProgress({
                            loaded: e.loaded,
                            total: e.total,
                            progress: progress,
                            files: files.map(f => f.name)
                        });
                    }
                }
            });

            // Handle completion
            xhr.addEventListener('load', () => {
                if (xhr.status >= 200 && xhr.status < 300) {
                    try {
                        const result = JSON.parse(xhr.responseText);
                        
                        // Mark as complete in progress UI
                        if (useProgressUI && typeof window !== 'undefined' && window.uploadProgress) {
                            window.uploadProgress.completeUpload(uploadId, true);
                        }

                        resolve(result);
                    } catch (error) {
                        // Mark as failed in progress UI
                        if (useProgressUI && typeof window !== 'undefined' && window.uploadProgress) {
                            window.uploadProgress.completeUpload(uploadId, false);
                        }
                        reject(new Error('Failed to parse response'));
                    }
                } else {
                    // Mark as failed in progress UI
                    if (useProgressUI && typeof window !== 'undefined' && window.uploadProgress) {
                        window.uploadProgress.completeUpload(uploadId, false);
                    }
                    reject(new Error(`Upload failed with status ${xhr.status}: ${xhr.responseText}`));
                }
            });

            // Handle errors
            xhr.addEventListener('error', () => {
                // Mark as failed in progress UI
                if (useProgressUI && typeof window !== 'undefined' && window.uploadProgress) {
                    window.uploadProgress.completeUpload(uploadId, false);
                }
                reject(new Error('Upload failed due to network error'));
            });

            // Handle abort
            xhr.addEventListener('abort', () => {
                // Mark as failed in progress UI
                if (useProgressUI && typeof window !== 'undefined' && window.uploadProgress) {
                    window.uploadProgress.completeUpload(uploadId, false);
                }
                reject(new Error('Upload was aborted'));
            });

            // Prepare form data
            const formData = new FormData();
            files.forEach(file => formData.append('files', file));
            if (path) {
                formData.append('path', path);
            }

            // Send request
            xhr.open('POST', '/file-chat/upload');
            xhr.setRequestHeader('X-Session-ID', sessionId);
            xhr.send(formData);
        });
    }
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = StandardUploader;
}
