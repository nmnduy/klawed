/**
 * Fuzzy Search Module using Fuse.js
 * 
 * This module provides fast fuzzy search capabilities for file explorer.
 * It uses Fuse.js library for efficient fuzzy matching with optimized settings.
 * 
 * Features:
 * - Fuzzy matching (e.g., "helo" matches "hello.txt")
 * - Fast performance with optimized algorithms
 * - Configurable thresholds and search patterns
 * - Supports searching in file names
 */

// We'll use a CDN for Fuse.js to avoid bundling issues
// This is loaded dynamically to ensure it's available

class FuzzySearchEngine {
    constructor() {
        this.fuse = null;
        this.items = [];
        this.Fuse = null;
        this.ready = false;
        
        // Fuse.js configuration optimized for file name search
        this.fuseOptions = {
            // Keys to search in
            keys: ['name'],
            
            // Threshold for fuzzy matching (0.0 = perfect match, 1.0 = match anything)
            // 0.5 provides better fuzzy matching for typos and abbreviations
            threshold: 0.5,
            
            // Distance to search (characters away from match location)
            distance: 100,
            
            // Minimum character length before fuzzy matching kicks in
            minMatchCharLength: 1,
            
            // Include score in results for debugging/ranking
            includeScore: true,
            
            // Include matches for highlighting (if needed in future)
            includeMatches: false,
            
            // Use extended search for more powerful queries
            // Allows operators like: 'hello | world' (OR), 'hello world' (AND)
            useExtendedSearch: false,
            
            // Ignore location for searching (search entire string)
            ignoreLocation: true,
            
            // Ignore field length normalization
            ignoreFieldNorm: true,
            
            // Field normalization factor
            fieldNormWeight: 1
        };
        
        // Initialize Fuse.js
        this.init();
    }
    
    async init() {
        try {
            // Import Fuse from local copy (more reliable than CDN)
            const FuseModule = await import('./fuse.min.mjs');
            this.Fuse = FuseModule.default;
            this.ready = true;
            console.log('[fuzzy-search] Fuse.js loaded successfully');
        } catch (error) {
            console.error('[fuzzy-search] Failed to load Fuse.js:', error);
            this.ready = false;
        }
    }
    
    /**
     * Index the items for fuzzy search
     * @param {Array} items - Array of file/folder objects with 'name' property
     */
    setItems(items) {
        this.items = items;
        if (this.ready && this.Fuse) {
            this.fuse = new this.Fuse(items, this.fuseOptions);
        }
    }
    
    /**
     * Perform fuzzy search on indexed items
     * @param {string} searchTerm - The search query
     * @returns {Array} - Filtered items matching the search term
     */
    search(searchTerm) {
        // If no search term, return all items
        if (!searchTerm || searchTerm.trim() === '') {
            return this.items;
        }
        
        // Fallback to simple substring search if Fuse.js isn't ready
        if (!this.ready || !this.fuse) {
            console.warn('[fuzzy-search] Fuse.js not ready, falling back to substring search');
            const term = searchTerm.toLowerCase();
            return this.items.filter(item => 
                (item.name || '').toLowerCase().includes(term)
            );
        }
        
        // Perform fuzzy search
        const results = this.fuse.search(searchTerm);
        
        // Extract items from Fuse.js result format
        // Fuse.js returns: [{ item: {...}, score: 0.xx }, ...]
        return results.map(result => result.item);
    }
    
    /**
     * Get search statistics for debugging
     * @param {string} searchTerm - The search query
     * @returns {Object} - Statistics about the search
     */
    getSearchStats(searchTerm) {
        if (!searchTerm || searchTerm.trim() === '' || !this.fuse) {
            return {
                totalItems: this.items.length,
                matchedItems: this.items.length,
                searchTerm: searchTerm
            };
        }
        
        const results = this.fuse.search(searchTerm);
        
        return {
            totalItems: this.items.length,
            matchedItems: results.length,
            searchTerm: searchTerm,
            averageScore: results.length > 0 
                ? results.reduce((sum, r) => sum + r.score, 0) / results.length 
                : 0
        };
    }
    
    /**
     * Update threshold for fuzzy matching
     * Lower = more strict, Higher = more lenient
     * @param {number} threshold - Value between 0.0 and 1.0
     */
    setThreshold(threshold) {
        this.fuseOptions.threshold = Math.max(0, Math.min(1, threshold));
        if (this.fuse && this.items.length > 0 && this.ready && this.Fuse) {
            this.fuse = new this.Fuse(this.items, this.fuseOptions);
        }
    }
}

// Create a singleton instance
const fuzzySearch = new FuzzySearchEngine();

// Export the instance
export default fuzzySearch;
