(function() {
    const loadingEl = document.getElementById('demos-loading');
    const emptyEl = document.getElementById('demos-empty');
    const errorEl = document.getElementById('demos-error');
    const errorMessageEl = document.getElementById('demos-error-message');
    const gridEl = document.getElementById('demos-grid');
    const statsEl = document.getElementById('demo-stats');
    const retryBtn = document.getElementById('demos-retry');
    
    const videoModal = document.getElementById('video-modal');
    const videoPlayer = document.getElementById('video-player');
    const videoModalTitle = document.getElementById('video-modal-title');
    const videoModalClose = document.getElementById('video-modal-close');

    function showState(state) {
        loadingEl.classList.add('hidden');
        emptyEl.classList.add('hidden');
        errorEl.classList.add('hidden');
        gridEl.classList.add('hidden');
        
        if (state === 'loading') loadingEl.classList.remove('hidden');
        else if (state === 'empty') emptyEl.classList.remove('hidden');
        else if (state === 'error') errorEl.classList.remove('hidden');
        else if (state === 'grid') gridEl.classList.remove('hidden');
    }

    function formatTitle(name) {
        // Convert kebab-case or snake_case to Title Case
        return name
            .replace(/[-_]/g, ' ')
            .replace(/\b\w/g, c => c.toUpperCase());
    }

    function createDemoCard(demo) {
        const card = document.createElement('div');
        card.className = 'demo-card rounded-xl border border-border bg-card shadow-sm hover:shadow-md overflow-hidden cursor-pointer';
        card.innerHTML = `
            <div class="aspect-video bg-slate-100 flex items-center justify-center relative group">
                <svg class="w-16 h-16 text-slate-300 group-hover:text-orange-400 transition-colors duration-200" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M14.752 11.168l-3.197-2.132A1 1 0 0010 9.87v4.263a1 1 0 001.555.832l3.197-2.132a1 1 0 000-1.664z" />
                    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                </svg>
                <div class="absolute inset-0 bg-black/0 group-hover:bg-black/10 transition-colors duration-200 flex items-center justify-center">
                    <span class="opacity-0 group-hover:opacity-100 transition-opacity duration-200 px-3 py-1.5 bg-orange-500 text-white text-caption-s font-medium rounded-full">
                        Play Video
                    </span>
                </div>
            </div>
            <div class="p-4">
                <h3 class="text-body-m font-semibold text-foreground mb-1">${formatTitle(demo.name)}</h3>
                <p class="text-caption-s text-muted-foreground">${demo.sizeFormatted}</p>
            </div>
        `;
        
        card.addEventListener('click', () => openVideo(demo));
        return card;
    }

    function openVideo(demo) {
        videoModalTitle.textContent = formatTitle(demo.name);
        videoPlayer.src = `/demo/${encodeURIComponent(demo.name)}`;
        videoModal.classList.remove('hidden');
        document.body.style.overflow = 'hidden';
        videoPlayer.play().catch(() => {
            // Autoplay may be blocked, user can click play
        });
    }

    function closeVideo() {
        videoPlayer.pause();
        videoPlayer.src = '';
        videoModal.classList.add('hidden');
        document.body.style.overflow = '';
    }

    // Close modal on backdrop click
    videoModal.addEventListener('click', (e) => {
        if (e.target === videoModal) closeVideo();
    });

    // Close modal on button click
    videoModalClose.addEventListener('click', closeVideo);

    // Close modal on Escape key
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && !videoModal.classList.contains('hidden')) {
            closeVideo();
        }
    });

    async function loadDemos() {
        showState('loading');
        statsEl.textContent = 'Loading...';
        
        try {
            const response = await fetch('/demo/list');
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            
            const data = await response.json();
            
            if (!data.demos || data.demos.length === 0) {
                showState('empty');
                statsEl.textContent = '0 videos';
                return;
            }
            
            gridEl.innerHTML = '';
            data.demos.forEach(demo => {
                gridEl.appendChild(createDemoCard(demo));
            });
            
            showState('grid');
            statsEl.textContent = `${data.count} video${data.count === 1 ? '' : 's'}`;
            
        } catch (error) {
            console.error('Failed to load demos:', error);
            showState('error');
            errorMessageEl.textContent = 'Failed to load demo videos';
            statsEl.textContent = 'Error';
        }
    }

    retryBtn.addEventListener('click', loadDemos);
    
    // Load demos on page load
    loadDemos();
})();
