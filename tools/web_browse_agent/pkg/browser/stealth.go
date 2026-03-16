package browser

import (
	"os"

	"github.com/playwright-community/playwright-go"
)

// stealthEnabled returns true when WEB_AGENT_STEALTH env var is not set to "0" or "false".
// Stealth is enabled by default to work in datacenter environments where Cloudflare
// and other bot-detection systems would otherwise block headless browser traffic.
func stealthEnabled() bool {
	v := os.Getenv("WEB_AGENT_STEALTH")
	return v != "0" && v != "false" && v != "no"
}

// stealthArgs returns the Chromium launch arguments that help evade bot detection.
// These flags spoof or disable features that fingerprint headless browsers.
func stealthArgs() []string {
	return []string{
		// Disable automation flags that expose headless mode
		"--disable-blink-features=AutomationControlled",

		// Disable the infobars that Playwright adds (e.g. "Chrome is being controlled...")
		"--disable-infobars",

		// Use a real-looking window size so viewport fingerprinting passes
		"--window-size=1920,1080",

		// Disable the component extensions that fingerprint automation
		"--disable-extensions",

		// No sandbox in container environments (required)
		"--no-sandbox",
		"--disable-setuid-sandbox",

		// Disable GPU (not available in headless datacenter)
		"--disable-gpu",

		// Prevent Chromium from showing its build/version in navigator.appVersion
		"--disable-dev-shm-usage",

		// Suppress useless noise
		"--log-level=3",
		"--silent-debugger-extension-api",

		// Language spoofing - use a neutral en-US locale
		"--lang=en-US,en",
		"--accept-lang=en-US,en;q=0.9",

		// Override User-Agent Client Hints (sec-ch-ua) so "HeadlessChrome" is not sent.
		// The brand list must be a valid structured header (JSON array of objects).
		// This spoofs a regular Chrome build with a greased brand.
		`--user-agent=Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36`,
	}
}

// stealthUserAgent returns a desktop Chrome user-agent string that does not
// contain "HeadlessChrome", which is the primary tell for bot detection.
// We match the Playwright-bundled Chromium version.
func stealthUserAgent() string {
	// Match chromium version shipped with playwright-go v0.5200.1 (Playwright 1.52)
	// Chromium 136.x
	return "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"
}

// stealthContextOptions returns BrowserNewContextOptions that help evade detection.
func stealthContextOptions(proxy *playwright.Proxy) playwright.BrowserNewContextOptions {
	opts := playwright.BrowserNewContextOptions{
		UserAgent: playwright.String(stealthUserAgent()),
		Locale:    playwright.String("en-US"),
		// Mimic a real desktop viewport
		Viewport: &playwright.Size{
			Width:  1920,
			Height: 1080,
		},
		// Expose a real-looking timezone
		TimezoneId: playwright.String("America/New_York"),
		// Override sec-ch-ua headers to remove HeadlessChrome brand
		ExtraHttpHeaders: map[string]string{
			"sec-ch-ua":          `"Chromium";v="136", "Not.A/Brand";v="8", "Google Chrome";v="136"`,
			"sec-ch-ua-mobile":   "?0",
			"sec-ch-ua-platform": `"Linux"`,
		},
	}
	if proxy != nil {
		opts.Proxy = proxy
	}
	return opts
}

// stealthPersistentContextOptions returns launch options for a persistent context
// with stealth settings applied.
func stealthPersistentContextOptions(
	headless bool,
	execPath string,
	proxy *playwright.Proxy,
) playwright.BrowserTypeLaunchPersistentContextOptions {
	opts := playwright.BrowserTypeLaunchPersistentContextOptions{
		Headless:  playwright.Bool(headless),
		Args:      stealthArgs(),
		UserAgent: playwright.String(stealthUserAgent()),
		Locale:    playwright.String("en-US"),
		Viewport: &playwright.Size{
			Width:  1920,
			Height: 1080,
		},
		TimezoneId: playwright.String("America/New_York"),
		ExtraHttpHeaders: map[string]string{
			"sec-ch-ua":          `"Chromium";v="136", "Not.A/Brand";v="8", "Google Chrome";v="136"`,
			"sec-ch-ua-mobile":   "?0",
			"sec-ch-ua-platform": `"Linux"`,
		},
	}
	if execPath != "" {
		opts.ExecutablePath = playwright.String(execPath)
	}
	if proxy != nil {
		opts.Proxy = proxy
	}
	return opts
}

// stealthLaunchOptions returns BrowserTypeLaunchOptions with stealth flags.
func stealthLaunchOptions(headless bool, execPath string, proxy *playwright.Proxy) playwright.BrowserTypeLaunchOptions {
	opts := playwright.BrowserTypeLaunchOptions{
		Headless: playwright.Bool(headless),
		Args:     stealthArgs(),
	}
	if execPath != "" {
		opts.ExecutablePath = playwright.String(execPath)
	}
	if proxy != nil {
		opts.Proxy = proxy
	}
	return opts
}

// stealthInitScript returns JavaScript that is injected into every page before
// any other script runs. It patches the navigator and window objects to hide
// common headless/automation fingerprints.
const stealthInitScript = `
(function() {
  // 1. Remove navigator.webdriver flag completely.
  //    Simply setting it to undefined still leaves the property in navigator
  //    (detectable via "webdriver" in navigator).
  //    We must delete the property from the prototype chain entirely.
  try {
    const navigatorProto = Object.getPrototypeOf(navigator);
    const descriptor = Object.getOwnPropertyDescriptor(navigatorProto, 'webdriver');
    if (descriptor) {
      // Delete from prototype, then re-define as always-undefined on the instance
      delete navigatorProto.webdriver;
    }
    // Also clear the own property if set directly on navigator
    if (Object.getOwnPropertyDescriptor(navigator, 'webdriver')) {
      delete navigator.webdriver;
    }
  } catch (e) {
    // Fallback: redefine to be non-existent-looking
    Object.defineProperty(navigator, 'webdriver', {
      get: () => false,
      configurable: true,
      enumerable: false,
    });
  }

  // 2. Restore window.chrome object that headless Chrome lacks
  if (!window.chrome) {
    window.chrome = {
      app: {
        isInstalled: false,
        InstallState: { DISABLED: 'disabled', INSTALLED: 'installed', NOT_INSTALLED: 'not_installed' },
        RunningState: { CANNOT_RUN: 'cannot_run', READY_TO_RUN: 'ready_to_run', RUNNING: 'running' },
      },
      runtime: {
        OnInstalledReason: {
          CHROME_UPDATE: 'chrome_update',
          INSTALL: 'install',
          SHARED_MODULE_UPDATE: 'shared_module_update',
          UPDATE: 'update',
        },
        OnRestartRequiredReason: {
          APP_UPDATE: 'app_update',
          OS_UPDATE: 'os_update',
          PERIODIC: 'periodic',
        },
        PlatformArch: {
          ARM: 'arm',
          ARM64: 'arm64',
          MIPS: 'mips',
          MIPS64: 'mips64',
          X86_32: 'x86-32',
          X86_64: 'x86-64',
        },
        PlatformNaclArch: {
          ARM: 'arm',
          MIPS: 'mips',
          MIPS64: 'mips64',
          X86_32: 'x86-32',
          X86_64: 'x86-64',
        },
        PlatformOs: {
          ANDROID: 'android',
          CROS: 'cros',
          LINUX: 'linux',
          MAC: 'mac',
          OPENBSD: 'openbsd',
          WIN: 'win',
        },
        RequestUpdateCheckStatus: {
          NO_UPDATE: 'no_update',
          THROTTLED: 'throttled',
          UPDATE_AVAILABLE: 'update_available',
        },
      },
    };
  }

  // 3. Spoof plugins array (real Chrome ships with ~5 plugins)
  const pluginData = [
    { name: 'Chrome PDF Plugin',          filename: 'internal-pdf-viewer',  description: 'Portable Document Format' },
    { name: 'Chrome PDF Viewer',          filename: 'mhjfbmdgcfjbbpaeojofohoefgiehjai', description: '' },
    { name: 'Native Client',             filename: 'internal-nacl-plugin',  description: '' },
  ];
  const makePlugin = (data) => {
    const plugin = Object.create(Plugin.prototype);
    Object.defineProperty(plugin, 'name',        { get: () => data.name });
    Object.defineProperty(plugin, 'filename',    { get: () => data.filename });
    Object.defineProperty(plugin, 'description', { get: () => data.description });
    Object.defineProperty(plugin, 'length',      { get: () => 0 });
    return plugin;
  };
  const pluginArray = Object.create(PluginArray.prototype);
  pluginData.forEach((data, i) => {
    pluginArray[i] = makePlugin(data);
  });
  Object.defineProperty(pluginArray, 'length', { get: () => pluginData.length });
  Object.defineProperty(navigator, 'plugins', { get: () => pluginArray, configurable: true });

  // 4. Spoof languages
  Object.defineProperty(navigator, 'languages', {
    get: () => ['en-US', 'en'],
    configurable: true,
  });

  // 5. Spoof permissions query (headless returns 'denied' for notifications)
  const originalQuery = window.navigator.permissions && window.navigator.permissions.query;
  if (originalQuery) {
    window.navigator.permissions.query = (parameters) => (
      parameters.name === 'notifications'
        ? Promise.resolve({ state: Notification.permission })
        : originalQuery(parameters)
    );
  }

  // 6. Spoof WebGL renderer and vendor strings.
  //    gl.getParameter() for RENDERER/VENDOR returns "SwiftShader" in headless
  //    Chrome because there's no GPU. We intercept getParameter on both
  //    WebGLRenderingContext and WebGL2RenderingContext and return realistic
  //    NVIDIA strings for the four relevant enum values.
  (function() {
    const VENDOR_ENUM            = 0x1F00; // GL_VENDOR
    const RENDERER_ENUM          = 0x1F01; // GL_RENDERER
    const UNMASKED_VENDOR_WEBGL  = 0x9245; // from WEBGL_debug_renderer_info
    const UNMASKED_RENDERER_WEBGL = 0x9246;

    const FAKE_VENDOR   = 'Google Inc. (NVIDIA)';
    const FAKE_RENDERER = 'ANGLE (NVIDIA, NVIDIA GeForce RTX 3080 Direct3D11 vs_5_0 ps_5_0, D3D11)';

    function patchGetParameter(ctx) {
      const original = ctx.getParameter.bind(ctx);
      ctx.getParameter = function(param) {
        if (param === VENDOR_ENUM || param === UNMASKED_VENDOR_WEBGL)   return FAKE_VENDOR;
        if (param === RENDERER_ENUM || param === UNMASKED_RENDERER_WEBGL) return FAKE_RENDERER;
        return original(param);
      };
    }

    // Patch HTMLCanvasElement.getContext so any future context is also patched
    const origGetContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(type, ...args) {
      const ctx = origGetContext.call(this, type, ...args);
      if (ctx && (type === 'webgl' || type === 'experimental-webgl' || type === 'webgl2')) {
        patchGetParameter(ctx);
      }
      return ctx;
    };
  })();

  // 7. Prevent iframe contentWindow detection
  const originalContentWindow = Object.getOwnPropertyDescriptor(HTMLIFrameElement.prototype, 'contentWindow');
  if (originalContentWindow) {
    Object.defineProperty(HTMLIFrameElement.prototype, 'contentWindow', {
      get: function() {
        const win = originalContentWindow.get.call(this);
        if (win && win.navigator) {
          Object.defineProperty(win.navigator, 'webdriver', { get: () => undefined });
        }
        return win;
      },
    });
  }
})();
`
