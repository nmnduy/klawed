// Simplified Template Browser
class TemplateBrowser {
    constructor() {
        // Core elements
        this.panel = document.getElementById('template-browser-panel');
        this.loadingEl = document.getElementById('template-browser-loading');
        this.emptyEl = document.getElementById('template-browser-empty');
        this.listWrapper = document.getElementById('template-browser-list');
        this.listEl = this.listWrapper?.querySelector('ul');

        // Preview (mobile modal)
        this.previewModal = document.getElementById('template-preview-modal');
        this.previewClose = document.getElementById('template-preview-close');
        this.previewContent = document.getElementById('template-preview-content');
        this.previewName = document.getElementById('template-preview-name');

        // Preview (desktop sidebar)
        this.previewSidebar = document.getElementById('template-preview-sidebar');
        this.previewSidebarContent = document.getElementById('template-preview-sidebar-content');
        this.previewSidebarClose = document.getElementById('template-preview-sidebar-close');

        // State
        this.templates = [];
        this.sessionId = null;
        this.currentTemplate = null;
        this.pdfCache = new Map();

        this.init();
    }

    init() {
        // Close handlers
        this.previewClose?.addEventListener('click', () => this.hidePreview());
        this.previewModal?.addEventListener('click', (e) => {
            if (e.target === this.previewModal) this.hidePreview();
        });
        this.previewSidebarClose?.addEventListener('click', () => this.hidePreview());

    }

    setSession(sessionId, userId) {
        this.sessionId = sessionId;
        this.pdfCache.clear();
        if (this.sessionId) {
            this.loadTemplates();
        }
    }

    async loadTemplates() {
        this.showLoading();
        try {
            if (!this.sessionId) throw new Error('No session');

            const resp = await fetch(`/invoice-chat/explorer/list?path=TEMPLATES`, {
                method: 'GET',
                headers: { 'X-Session-ID': this.sessionId || '' },
                credentials: 'include'
            });

            if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${await resp.text()}`);

            const data = await resp.json();
            this.processTemplateFiles(data.items || []);
        } catch (err) {
            console.error('Failed to load templates:', err);
            this.templates = [];
        }

        this.renderList();
        this.hideLoading();
    }

    processTemplateFiles(files) {
        this.templates = files
            .filter((file) => file.name.toLowerCase().endsWith('.tex'))
            .map((file) => this.createTemplateFromFile(file));
    }

    createTemplateFromFile(file) {
        const name = file.name.replace('.tex', '').replace(/_/g, ' ');
        const prettyName = name
            .split(' ')
            .map((p) => p.charAt(0).toUpperCase() + p.slice(1))
            .join(' ');

        return {
            id: file.name,
            name: prettyName,
            fileName: file.name,
            filePath: file.path,
            size: file.size,
            modified: file.modified,
            type: 'template'
        };
    }

    renderList() {
        if (!this.listEl) return;
        this.listEl.innerHTML = '';

        if (!this.templates.length) {
            this.showEmpty();
            return;
        }

        this.hideEmpty();

        this.templates.forEach((tpl) => {
            const li = document.createElement('li');
            li.className = 'cursor-pointer px-4 py-3 hover:bg-layout-surface border border-transparent hover:border-layout-border rounded-lg transition-colors flex items-center justify-between gap-3';
            li.dataset.templateId = tpl.id;
            li.innerHTML = `
                <div class="flex flex-col gap-0.5">
                    <span class="text-body-m font-medium text-layout-content-high">${tpl.name}</span>
                    <span class="text-caption-s text-layout-content-low">${tpl.fileName}</span>
                </div>
                <svg class="w-4 h-4 text-layout-content-low" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
                </svg>
            `;
            li.addEventListener('click', () => this.showPreview(tpl));
            this.listEl.appendChild(li);
        });
    }

    showLoading() {
        this.loadingEl?.classList.remove('hidden');
        this.emptyEl?.classList.add('hidden');
        this.listWrapper?.classList.add('hidden');
    }

    hideLoading() {
        this.loadingEl?.classList.add('hidden');
        if (this.templates.length) {
            this.listWrapper?.classList.remove('hidden');
        }
    }

    showEmpty() {
        this.emptyEl?.classList.remove('hidden');
        this.listWrapper?.classList.add('hidden');
    }

    hideEmpty() {
        this.emptyEl?.classList.add('hidden');
    }

    isDesktop() {
        return window.matchMedia('(min-width: 1024px)').matches;
    }

    async showPreview(template) {
        this.currentTemplate = template;
        const desktop = this.isDesktop();

        // Reset both containers
        this.previewContent.innerHTML = '';
        this.previewSidebarContent.innerHTML = '';

        if (desktop) {
            this.previewSidebar?.classList.remove('hidden');
            this.previewModal?.classList.add('hidden');
            this.previewSidebarContent.innerHTML = this.loadingMarkup();
        } else {
            this.previewModal?.classList.remove('hidden');
            this.previewSidebar?.classList.add('hidden');
            document.body.style.overflow = 'hidden';
            this.previewName.textContent = template.name;
            this.previewContent.innerHTML = this.loadingMarkup();
        }

        try {
            await this.loadTemplateContent(template, desktop ? this.previewSidebarContent : this.previewContent);
        } catch (err) {
            console.error('Preview failed:', err);
            const target = desktop ? this.previewSidebarContent : this.previewContent;
            target.innerHTML = `<div class="p-4 text-caption-m text-layout-content-low">Unable to load preview.</div>`;
        }
    }

    hidePreview() {
        this.previewModal?.classList.add('hidden');
        this.previewSidebar?.classList.add('hidden');
        document.body.style.overflow = '';
        this.currentTemplate = null;
    }

    loadingMarkup() {
        return `
            <div class="h-full flex items-center justify-center">
                <div class="text-center">
                    <div class="inline-block animate-spin rounded-full h-8 w-8 border-b-2 border-semantic-link-unvisited mb-3"></div>
                    <p class="text-caption-m text-layout-content-low">Loading preview...</p>
                </div>
            </div>
        `;
    }

    async loadTemplateContent(template, targetEl) {
        if (!this.sessionId) throw new Error('Session not ready');
        try {
            await this.compileAndDisplayPdf(template, targetEl);
        } catch (err) {
            console.error('PDF preview failed, showing source:', err);
            await this.loadLaTeXSource(template, targetEl);
        }
    }

    async compileAndDisplayPdf(template, targetEl) {
        const cacheKey = `${template.fileName}_${this.sessionId}`;
        if (this.pdfCache.has(cacheKey)) {
            const cached = this.pdfCache.get(cacheKey);
            if (Date.now() - cached.timestamp < 60 * 60 * 1000) {
                return this.displayPdfPreview(template, cached.pdfPath, targetEl);
            }
            this.pdfCache.delete(cacheKey);
        }

        const compileUrl = `/invoice-chat/explorer/compile-latex?path=TEMPLATES/${encodeURIComponent(template.fileName)}&engine=xelatex`;
        const resp = await fetch(compileUrl, {
            method: 'POST',
            headers: { 'X-Session-ID': this.sessionId },
            credentials: 'include'
        });

        if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${await resp.text()}`);
        const result = await resp.json();
        if (!result.success) throw new Error(result.error || 'Compilation failed');

        this.pdfCache.set(cacheKey, { pdfPath: result.pdfPath, timestamp: Date.now() });
        await this.displayPdfPreview(template, result.pdfPath, targetEl);
    }

    async displayPdfPreview(template, pdfPath, targetEl) {
        const fileUrl = `/invoice-chat/explorer/open?path=${encodeURIComponent(pdfPath)}`;
        const resp = await fetch(fileUrl, { headers: { 'X-Session-ID': this.sessionId } });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${await resp.text()}`);
        const blob = await resp.blob();
        const objectUrl = URL.createObjectURL(blob);

        targetEl.innerHTML = `
            <div class="space-y-3">
                <div>
                    <p class="text-body-m font-semibold text-layout-content-high">${template.name}</p>
                    <p class="text-caption-s text-layout-content-low">${template.fileName}</p>
                </div>
                <div class="rounded-lg border border-layout-border overflow-hidden">
                    <iframe src="${objectUrl}" class="w-full h-[500px]" title="Preview of ${template.name}"></iframe>
                </div>
            </div>
        `;

        // Revoke URL after some time
        setTimeout(() => URL.revokeObjectURL(objectUrl), 30000);
    }

    async loadLaTeXSource(template, targetEl) {
        const resp = await fetch(`/invoice-chat/explorer/preview?path=TEMPLATES/${encodeURIComponent(template.fileName)}`, {
            method: 'GET',
            headers: { 'X-Session-ID': this.sessionId || '' },
            credentials: 'include'
        });

        if (!resp.ok) throw new Error(`HTTP ${resp.status}: ${await resp.text()}`);
        const content = await resp.text();

        const escaped = content
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#039;');

        targetEl.innerHTML = `
            <div class="space-y-3">
                <div>
                    <p class="text-body-m font-semibold text-layout-content-high">${template.name}</p>
                    <p class="text-caption-s text-layout-content-low">${template.fileName}</p>
                </div>
                <pre class="p-3 bg-layout-surface rounded border border-layout-border overflow-auto text-sm font-mono text-layout-content-medium whitespace-pre-wrap max-h-[60vh]">${escaped}</pre>
            </div>
        `;
    }

}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = TemplateBrowser;
}

// Auto-init
if (typeof window !== 'undefined') {
    window.TemplateBrowser = TemplateBrowser;
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => {
            if (document.getElementById('template-browser-panel')) {
                window.templateBrowser = new TemplateBrowser();
            }
        });
    } else {
        if (document.getElementById('template-browser-panel')) {
            window.templateBrowser = new TemplateBrowser();
        }
    }
}
