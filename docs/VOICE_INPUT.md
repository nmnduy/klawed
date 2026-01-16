# Voice Input (Web Speech API)

## Overview
FileSurf v2 includes voice dictation using the Web Speech API. This feature allows users to speak their messages instead of typing them.

## Browser Support
- ✅ **Chrome/Edge**: Full support (uses Google's speech recognition servers)
- ✅ **Safari**: Full support (uses Apple's speech recognition)
- ❌ **Firefox**: Not supported (no Web Speech API implementation)

## HTTPS Requirement

⚠️ **Important**: The Web Speech API requires HTTPS in production environments.

### Why HTTPS?
The Web Speech API accesses the user's microphone, which browsers consider a sensitive permission. For security reasons, browsers only allow this over HTTPS connections.

### Exceptions
The API works over HTTP in these cases:
- Accessing from `http://localhost`
- Accessing from `http://127.0.0.1`
- Accessing from `http://[::1]` (IPv6 localhost)

### Common Errors

#### "network" Error
```
Speech recognition error: network
✗ Voice error: network
```

**Cause**: The browser cannot reach the speech recognition service, usually because:
1. You're accessing the app over HTTP (not HTTPS or localhost)
2. The browser cannot connect to Google's/Apple's speech servers
3. Firewall or network restrictions

**Solution**:
- **For local development**: Access via `http://localhost:9090` instead of IP address
- **For production**: Set up HTTPS with a valid SSL certificate

#### "not-allowed" Error
```
✗ Microphone access denied. Check browser permissions.
```

**Cause**: User denied microphone permission, or browser blocked microphone access.

**Solution**:
- Click the microphone icon in the browser's address bar
- Grant microphone permission
- Reload the page

#### "no-speech" Error
```
✗ No speech detected. Please try again.
```

**Cause**: The speech recognition API didn't detect any speech within the timeout period.

**Solution**:
- Try speaking closer to the microphone
- Check that your microphone is working
- Try again and speak immediately after clicking the voice button

## Setting Up HTTPS

### Option 1: Using a Reverse Proxy (Recommended)
Use nginx, Caddy, or Apache as a reverse proxy with Let's Encrypt SSL:

```nginx
server {
    listen 443 ssl http2;
    server_name filesurf.example.com;

    ssl_certificate /etc/letsencrypt/live/filesurf.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/filesurf.example.com/privkey.pem;

    location / {
        proxy_pass http://localhost:9090;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        # WebSocket support
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}
```

### Option 2: Quarkus Native HTTPS
Configure Quarkus to serve HTTPS directly:

```properties
# application.properties
quarkus.http.ssl-port=8443
quarkus.http.ssl.certificate.files=/path/to/cert.pem
quarkus.http.ssl.certificate.key-files=/path/to/key.pem
```

### Option 3: Tailscale HTTPS (For Private Networks)
If using Tailscale, enable HTTPS certificates:
```bash
tailscale cert filesurf-machine.tailnet-name.ts.net
```

## Usage

### Button States
- **Idle**: Gray microphone icon
- **Listening**: Orange pulsing microphone icon with ring
- **Recording**: Text appears in message input as you speak

### User Flow
1. Click the microphone button
2. Grant microphone permission (first time only)
3. Speak your message
4. The message automatically sends when you finish speaking
5. Click the button again to stop recording early

### Features
- **Interim Results**: See text appear as you speak
- **Auto-Submit**: Message automatically sends when speech is final
- **Safety Timeout**: Recording stops after 30 seconds
- **Visual Feedback**: Button pulses while listening

## Troubleshooting

### Voice button is disabled
**Cause**: Browser doesn't support Web Speech API (e.g., Firefox)

**Solution**: Use Chrome, Edge, or Safari

### "Voice input requires HTTPS or localhost access"
**Cause**: Accessing the app over HTTP from a non-localhost URL

**Solution**:
- Access via `http://localhost:9090`
- Or set up HTTPS (see "Setting Up HTTPS" above)

### Button works but no text appears
**Cause**: Microphone permission not granted, or microphone not working

**Solution**:
1. Check browser's microphone permission
2. Test microphone in browser settings
3. Check system microphone settings

## Implementation Details

### Code Location
- Main implementation: `src/main/resources/META-INF/resources/js/fileChat.js`
- Search for: "Voice input (Web Speech API)"

### Key Functions
- `supportsSpeech()`: Checks if browser supports Web Speech API
- `requiresHTTPS()`: Checks if HTTPS is required but not available
- `getRecognition()`: Creates and configures SpeechRecognition instance
- `startListening()`: Starts voice recording
- `stopListening()`: Stops voice recording
- `updateVoiceUI()`: Updates button visual state

### Configuration
```javascript
recognition.lang = 'en-US';           // Language (English US)
recognition.interimResults = true;    // Show text as you speak
recognition.continuous = false;       // Stop after one phrase
recognition.maxAlternatives = 1;      // Only return best match
```

### Safety Features
- 30-second timeout prevents runaway recordings
- Auto-cleanup on error or completion
- Graceful degradation when not supported
