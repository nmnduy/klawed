# Free Search Engine APIs for AI Agents

This guide covers free search APIs that can be used programmatically, along with best practices for respecting rate limits.

**Last tested: 2026-01-24**

---

## Quick Reference

| Service | Auth Required | Free Limit | Full Web Results | Status |
|---------|--------------|------------|------------------|--------|
| DuckDuckGo Instant API | No | Unlimited* | No (instant answers) | ✅ Working |
| DuckDuckGo Search (ddgs) | No | Unlimited* | Yes | ✅ Working |
| Brave Search | API Key | 2,000/month | Yes | ✅ Working |
| Mojeek | API Key | 1,000/month | Yes | ✅ Working |

*Unlimited but abuse will get you blocked. Respect rate limits.

---

## 1. DuckDuckGo Instant Answer API (No Auth)

Returns structured instant answers, summaries, and related topics - **not full web search results**. Best for quick facts and definitions.

### Endpoint

```
https://api.duckduckgo.com/
```

### Usage

```bash
# Basic query
curl "https://api.duckduckgo.com/?q=python+programming&format=json"

# With options
curl "https://api.duckduckgo.com/?q=python+programming&format=json&no_html=1&skip_disambig=1"
```

### Parameters

| Parameter | Description |
|-----------|-------------|
| `q` | Search query (URL encoded) |
| `format` | `json` or `xml` |
| `no_html` | `1` to strip HTML from responses |
| `skip_disambig` | `1` to skip disambiguation pages |
| `no_redirect` | `1` to prevent redirects |

### Response Structure

```json
{
  "Abstract": "Short topic summary",
  "AbstractText": "Plain text summary",
  "AbstractSource": "Source name (e.g., Wikipedia)",
  "AbstractURL": "URL to source",
  "Image": "URL to related image",
  "Heading": "Topic name",
  "RelatedTopics": [
    {
      "Text": "Related topic description",
      "FirstURL": "URL to related topic"
    }
  ],
  "Results": []
}
```

### Python Example

```python
import requests
import time

def duckduckgo_instant(query):
    """
    Query DuckDuckGo Instant Answer API.
    Returns summaries/facts, NOT full web search results.
    """
    url = "https://api.duckduckgo.com/"
    params = {
        "q": query,
        "format": "json",
        "no_html": 1,
        "skip_disambig": 1
    }
    
    response = requests.get(url, params=params, timeout=10)
    response.raise_for_status()
    
    time.sleep(1)  # Rate limit: 1 request per second
    return response.json()

# Usage
result = duckduckgo_instant("Python programming language")
print(result.get("AbstractText"))
```

### Rate Limits

- **Recommended**: Maximum 1 request per second
- **Daily**: Keep under 1000 requests/day to be safe
- No published official limits, but be respectful

---

## 2. DuckDuckGo Web Search (ddgs library) ✅ RECOMMENDED

For **actual web search results**, use the `ddgs` Python library. This is the most reliable free option for full web search.

### Installation

```bash
pip install ddgs
```

> **Note**: The old `duckduckgo-search` package has been renamed to `ddgs`.

### Python Example

```python
from ddgs import DDGS
import time

def search_web(query, max_results=5):
    """
    Perform web search via DuckDuckGo.
    
    IMPORTANT: Respect rate limits - max 1 request per second.
    """
    with DDGS() as ddgs:
        results = list(ddgs.text(query, max_results=max_results))
    
    time.sleep(1)  # Rate limit: 1 request per second
    return results

# Usage - returns list of dicts with 'title', 'href', 'body'
results = search_web("best python libraries 2024")
for r in results:
    print(f"Title: {r['title']}")
    print(f"URL: {r['href']}")
    print(f"Snippet: {r['body']}\n")
```

### Additional Search Types

```python
from ddgs import DDGS

with DDGS() as ddgs:
    # Web search
    web_results = list(ddgs.text("python tutorials", max_results=5))
    
    # News search
    news_results = list(ddgs.news("python release", max_results=5))
    
    # Image search
    image_results = list(ddgs.images("python logo", max_results=5))
    
    # Video search
    video_results = list(ddgs.videos("python tutorial", max_results=5))
    
    # Answers (instant answers)
    answers = list(ddgs.answers("python"))
```

### Rate Limits

- **Recommended**: 1 request per second
- **Burst avoidance**: Space requests evenly
- **Daily**: Stay under 1000 requests/day
- **Warning**: Excessive use may result in temporary blocks

---

## 3. Brave Search API (Requires Free API Key)

High-quality search results with a generous free tier.

### Setup

1. Create account at https://brave.com/search/api/
2. Get free API key (2,000 queries/month)

### Usage

```bash
curl -H "X-Subscription-Token: YOUR_API_KEY" \
  "https://api.search.brave.com/res/v1/web/search?q=python+tutorial"
```

### Python Example

```python
import requests
import time
import os

def brave_search(query, count=5):
    """
    Search using Brave Search API.
    Free tier: 2,000 queries/month (~66/day)
    """
    api_key = os.environ.get("BRAVE_API_KEY")
    if not api_key:
        raise ValueError("BRAVE_API_KEY environment variable not set")
    
    url = "https://api.search.brave.com/res/v1/web/search"
    headers = {
        "X-Subscription-Token": api_key,
        "Accept": "application/json"
    }
    params = {"q": query, "count": count}
    
    response = requests.get(url, headers=headers, params=params, timeout=10)
    response.raise_for_status()
    
    time.sleep(1)  # Rate limit safety
    return response.json()

# Usage
results = brave_search("machine learning tutorials")
for item in results.get("web", {}).get("results", []):
    print(f"{item['title']}: {item['url']}")
```

### Rate Limits

- **Free tier**: 2,000 queries/month, 1 request/second
- **Recommendation**: Track daily usage (~66/day max)

---

## 4. Mojeek API (Requires Free API Key)

UK-based independent search engine with its own index (not Google/Bing based).

### Setup

Register at https://www.mojeek.com/services/api.html

### Free Tier

- 1,000 queries/month free
- Independent index

### Python Example

```python
import requests
import time
import os

def mojeek_search(query, count=5):
    """
    Search using Mojeek API.
    Free tier: 1,000 queries/month (~33/day)
    """
    api_key = os.environ.get("MOJEEK_API_KEY")
    if not api_key:
        raise ValueError("MOJEEK_API_KEY environment variable not set")
    
    url = "https://www.mojeek.com/search"
    params = {
        "q": query,
        "api_key": api_key,
        "fmt": "json",
        "t": count
    }
    
    response = requests.get(url, params=params, timeout=10)
    response.raise_for_status()
    
    time.sleep(1)
    return response.json()
```

---

## Rate Limiting Best Practices

### 1. Always Add Delays

```python
import time
from functools import wraps

def rate_limit(seconds=1):
    """Decorator to enforce rate limiting."""
    def decorator(func):
        last_called = [0]
        
        @wraps(func)
        def wrapper(*args, **kwargs):
            elapsed = time.time() - last_called[0]
            if elapsed < seconds:
                time.sleep(seconds - elapsed)
            
            result = func(*args, **kwargs)
            last_called[0] = time.time()
            return result
        
        return wrapper
    return decorator

@rate_limit(seconds=1)
def search(query):
    # Your search code here
    pass
```

### 2. Implement Exponential Backoff

```python
import time
import requests

def search_with_backoff(search_func, query, max_retries=3):
    """Search with exponential backoff on failures."""
    for attempt in range(max_retries):
        try:
            return search_func(query)
        except requests.exceptions.HTTPError as e:
            if e.response.status_code == 429:  # Too Many Requests
                wait_time = (2 ** attempt) * 5  # 5, 10, 20 seconds
                print(f"Rate limited. Waiting {wait_time}s...")
                time.sleep(wait_time)
            else:
                raise
    
    raise Exception("Max retries exceeded")
```

### 3. Track Daily Usage

```python
import json
from datetime import date
from pathlib import Path

USAGE_FILE = Path("~/.search_usage.json").expanduser()

def track_usage(service_name):
    """Track daily API usage."""
    today = str(date.today())
    
    if USAGE_FILE.exists():
        usage = json.loads(USAGE_FILE.read_text())
    else:
        usage = {}
    
    if today not in usage:
        usage[today] = {}
    
    usage[today][service_name] = usage[today].get(service_name, 0) + 1
    USAGE_FILE.write_text(json.dumps(usage, indent=2))
    
    return usage[today][service_name]

def can_search(service_name, daily_limit):
    """Check if we're under the daily limit."""
    today = str(date.today())
    
    if USAGE_FILE.exists():
        usage = json.loads(USAGE_FILE.read_text())
        current = usage.get(today, {}).get(service_name, 0)
        return current < daily_limit
    
    return True
```

### 4. Cache Results

```python
import hashlib
import json
from pathlib import Path
from datetime import datetime, timedelta

CACHE_DIR = Path("~/.search_cache").expanduser()
CACHE_DIR.mkdir(exist_ok=True)

def cached_search(query, search_func, cache_hours=24):
    """Cache search results to reduce API calls."""
    cache_key = hashlib.md5(query.encode()).hexdigest()
    cache_file = CACHE_DIR / f"{cache_key}.json"
    
    # Check cache
    if cache_file.exists():
        cached = json.loads(cache_file.read_text())
        cached_time = datetime.fromisoformat(cached["timestamp"])
        
        if datetime.now() - cached_time < timedelta(hours=cache_hours):
            return cached["results"]
    
    # Perform search
    results = search_func(query)
    
    # Save to cache
    cache_file.write_text(json.dumps({
        "timestamp": datetime.now().isoformat(),
        "query": query,
        "results": results
    }, indent=2))
    
    return results
```

---

## Complete Example: Rate-Limited Search Utility

```python
#!/usr/bin/env python3
"""
Rate-limited search utility using DuckDuckGo.
Respects rate limits and caches results.
"""

import time
import json
import hashlib
from pathlib import Path
from datetime import datetime, timedelta
from ddgs import DDGS

class RateLimitedSearch:
    def __init__(self, cache_dir="~/.search_cache", cache_hours=24, 
                 requests_per_second=1, daily_limit=500):
        self.cache_dir = Path(cache_dir).expanduser()
        self.cache_dir.mkdir(exist_ok=True)
        self.cache_hours = cache_hours
        self.min_interval = 1.0 / requests_per_second
        self.daily_limit = daily_limit
        self.last_request = 0
        self.usage_file = self.cache_dir / "usage.json"
    
    def _wait_for_rate_limit(self):
        """Enforce rate limiting between requests."""
        elapsed = time.time() - self.last_request
        if elapsed < self.min_interval:
            time.sleep(self.min_interval - elapsed)
        self.last_request = time.time()
    
    def _get_cache_path(self, query):
        """Get cache file path for a query."""
        cache_key = hashlib.md5(query.lower().encode()).hexdigest()
        return self.cache_dir / f"{cache_key}.json"
    
    def _check_cache(self, query):
        """Check if we have a valid cached result."""
        cache_path = self._get_cache_path(query)
        if cache_path.exists():
            try:
                cached = json.loads(cache_path.read_text())
                cached_time = datetime.fromisoformat(cached["timestamp"])
                if datetime.now() - cached_time < timedelta(hours=self.cache_hours):
                    return cached["results"]
            except (json.JSONDecodeError, KeyError):
                pass
        return None
    
    def _save_cache(self, query, results):
        """Save results to cache."""
        cache_path = self._get_cache_path(query)
        cache_path.write_text(json.dumps({
            "timestamp": datetime.now().isoformat(),
            "query": query,
            "results": results
        }, indent=2))
    
    def _track_usage(self):
        """Track and check daily usage."""
        today = str(datetime.now().date())
        
        if self.usage_file.exists():
            usage = json.loads(self.usage_file.read_text())
        else:
            usage = {}
        
        count = usage.get(today, 0)
        if count >= self.daily_limit:
            raise Exception(f"Daily limit ({self.daily_limit}) reached")
        
        usage[today] = count + 1
        self.usage_file.write_text(json.dumps(usage, indent=2))
        return usage[today]
    
    def search(self, query, max_results=5, use_cache=True):
        """
        Search with rate limiting and caching.
        
        Args:
            query: Search query string
            max_results: Maximum number of results to return
            use_cache: Whether to use cached results
        
        Returns:
            List of search results with 'title', 'href', 'body'
        """
        # Check cache first
        if use_cache:
            cached = self._check_cache(query)
            if cached:
                return cached[:max_results]
        
        # Check daily limit
        self._track_usage()
        
        # Wait for rate limit
        self._wait_for_rate_limit()
        
        # Perform search
        with DDGS() as ddgs:
            results = list(ddgs.text(query, max_results=max_results))
        
        # Cache results
        if use_cache:
            self._save_cache(query, results)
        
        return results


# Usage
if __name__ == "__main__":
    searcher = RateLimitedSearch()
    
    results = searcher.search("python web frameworks 2024", max_results=5)
    
    for r in results:
        print(f"Title: {r['title']}")
        print(f"URL: {r['href']}")
        print(f"Snippet: {r['body'][:100]}...")
        print()
```

---

## Recommendations for AI Agents

1. **Use DuckDuckGo (ddgs library)** - Most reliable free option for web search
2. **Cache aggressively** - Same query usually = same results
3. **Respect rate limits strictly** - Getting blocked helps no one
4. **Track daily usage** - Stay well under limits
5. **Add delays between requests** - Minimum 1 second
6. **Use exponential backoff** - On any 429 or 5xx errors

### Choosing the Right Service

| Use Case | Recommended Service |
|----------|---------------------|
| Quick facts/definitions | DuckDuckGo Instant API |
| Full web search (free) | DuckDuckGo (ddgs library) |
| High-quality results (limited) | Brave Search API |
| Independent index | Mojeek |
