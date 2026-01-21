/** @type {import('tailwindcss').Config} */
export default {
  darkMode: ["class"],
  content: [
    "./src/main/resources/templates/**/*.{html,qute}",
    "./src/main/resources/META-INF/resources/**/*.html",
    "./src/main/resources/META-INF/resources/**/*.js",
    "./src/main/resources/css/**/*.css",  // Scans source CSS for @apply and class references
  ],
  safelist: [
    // No longer needed - classes are referenced in css/class-reference.html
    // which Tailwind scans via the content glob pattern
  ],
  theme: {
    container: {
      center: true,
      padding: "2rem",
      screens: {
        "2xl": "1400px",
      },
    },
    extend: {
      colors: {
        // Monochrome (Slate - Modern neutral with blue undertones)
        "slate": {
          950: "#0A0E14",
          900: "#0F172A",
          800: "#1E293B",
          700: "#334155",
          600: "#475569",
          500: "#64748B",
          400: "#94A3B8",
          300: "#CBD5E1",
          200: "#E2E8F0",
          100: "#F1F5F9",
          50: "#F8FAFC",
          25: "#FCFCFD",
        },
        // Primary - Vibrant Orange (Energetic, modern)
        "orange": {
          950: "#431407",
          900: "#7C2D12",
          800: "#9A3412",
          700: "#C2410C",
          600: "#EA580C",
          500: "#F97316",
          400: "#FB923C",
          300: "#FDBA74",
          200: "#FED7AA",
          100: "#FFEDD5",
          50: "#FFF7ED",
          25: "#FFFBF5",
        },
        // Secondary - Cyber Cyan (Modern tech aesthetic)
        "cyan": {
          950: "#083344",
          900: "#0E4C5F",
          800: "#155E75",
          700: "#0E7490",
          600: "#0891B2",
          500: "#06B6D4",
          400: "#22D3EE",
          300: "#67E8F9",
          200: "#A5F3FC",
          100: "#CFFAFE",
          50: "#ECFEFF",
          25: "#F0FDFF",
        },
        // Accent - Coral Sunset (Warm, inviting, trendy)
        "coral": {
          950: "#7C1D20",
          900: "#991B1B",
          800: "#B91C1C",
          700: "#DC2626",
          600: "#EF4444",
          500: "#F87171",
          400: "#FB923C",
          300: "#FCA5A5",
          200: "#FED7AA",
          100: "#FEE2E2",
          50: "#FFF1F2",
          25: "#FFF5F5",
        },
        // Success - Emerald Green (Rich, sophisticated green)
        "emerald": {
          950: "#022C22",
          900: "#064E3B",
          800: "#065F46",
          700: "#047857",
          600: "#059669",
          500: "#10B981",
          400: "#34D399",
          300: "#6EE7B7",
          200: "#A7F3D0",
          100: "#D1FAE5",
          50: "#ECFDF5",
          25: "#F0FDF9",
        },
        // Warning - Amber Glow (Sophisticated golden tone)
        "amber": {
          950: "#451A03",
          900: "#78350F",
          800: "#92400E",
          700: "#B45309",
          600: "#D97706",
          500: "#F59E0B",
          400: "#FBBF24",
          300: "#FCD34D",
          200: "#FDE68A",
          100: "#FEF3C7",
          50: "#FFFBEB",
          25: "#FFFDF7",
        },
        // Tertiary - Fuchsia Dream (Bold, modern magenta)
        "fuchsia": {
          950: "#4A044E",
          900: "#701A75",
          800: "#86198F",
          700: "#A21CAF",
          600: "#C026D3",
          500: "#D946EF",
          400: "#E879F9",
          300: "#F0ABFC",
          200: "#F5D0FE",
          100: "#FAE8FF",
          50: "#FDF4FF",
          25: "#FEF9FF",
        },
        // Cool Purple - Lavender Mist (Soft, elegant)
        "lavender": {
          950: "#2E1065",
          900: "#4C1D95",
          800: "#5B21B6",
          700: "#6D28D9",
          600: "#7C3AED",
          500: "#8B5CF6",
          400: "#A78BFA",
          300: "#C4B5FD",
          200: "#DDD6FE",
          100: "#EDE9FE",
          50: "#F5F3FF",
          25: "#FAF9FF",
        },
        // Teal Evolution - Modern Sea (Deeper, richer teal)
        "teal": {
          950: "#042F2E",
          900: "#134E4A",
          800: "#115E59",
          700: "#0F766E",
          600: "#0D9488",
          500: "#14B8A6",
          400: "#2DD4BF",
          300: "#5EEAD4",
          200: "#99F6E4",
          100: "#CCFBF1",
          50: "#F0FDFA",
          25: "#F5FFFE",
        },
        // Light Theme - Layout Colors (Modern, sophisticated)
        "layout": {
          "page-background": "#F8FAFC", // Slate 50 - Softer, more modern
          "main-container": "#FFFFFF", // Pure White
          "surface": "#F1F5F9", // Slate 100 - Elevated surface
          "surface-raised": "#FCFCFD", // Slate 25 - Subtle elevation
          "emphasis-high": "#0F172A", // Slate 900 - Deep, rich black
          "emphasis-medium": "#334155", // Slate 700 - Medium gray
          "emphasis-low": "#64748B", // Slate 500 - Light gray
          "disabled": "#94A3B8", // Slate 400
          "content-high": "#0F172A", // Slate 900
          "content-medium": "#475569", // Slate 600
          "content-low": "#64748B", // Slate 500
          "border": "#E2E8F0", // Slate 200 - Subtle borders
          "border-strong": "#CBD5E1", // Slate 300 - Defined borders
          "selected": "#F97316", // Orange 500 - Modern primary
          "hover": "#EA580C", // Orange 600 - Darker on hover
        },
        // Semantic Colors - Light Theme (Vibrant, accessible)
        "semantic": {
          "link-unvisited": "#EA580C", // Orange 600 - Modern, accessible
          "link-hover": "#C2410C", // Orange 700 - Darker on hover
          "link-pressed": "#9A3412", // Orange 800 - Even darker pressed
          "link-visited": "#7C3AED", // Lavender 600 - Distinct but harmonious
          "info": "#06B6D4", // Cyan 500 - Fresh, modern info color
          "info-bg": "#ECFEFF", // Cyan 50
          "success": "#10B981", // Emerald 500 - Rich success
          "success-bg": "#ECFDF5", // Emerald 50
          "warning": "#F59E0B", // Amber 500 - Sophisticated warning
          "warning-bg": "#FFFBEB", // Amber 50
          "error": "#EF4444", // Coral 600 - Strong but not harsh
          "error-bg": "#FFF1F2", // Coral 50
          "critical": "#DC2626", // Coral 700 - Critical actions
          "critical-bg": "#FEE2E2", // Coral 100
          // Blog-specific semantic colors
          "blog-heading": "#F97316", // Orange 500 - Primary brand color
          "blog-accent": "#0F172A", // Slate 900 - Strong contrast
        },
        // Dark Theme - Layout Colors (Deep, rich, modern)
        "dark": {
          "page-background": "#0A0E14", // Slate 950 - Deep, rich dark
          "main-container": "#1E293B", // Slate 800 - Elevated dark
          "surface": "#0F172A", // Slate 900 - Dark surface
          "surface-raised": "#334155", // Slate 700 - Raised surface
          "emphasis-high": "#F8FAFC", // Slate 50 - Bright text
          "emphasis-medium": "#CBD5E1", // Slate 300 - Medium text
          "emphasis-low": "#94A3B8", // Slate 400 - Subtle text
          "disabled": "#64748B", // Slate 500
          "content-high": "#F8FAFC", // Slate 50
          "content-medium": "#E2E8F0", // Slate 200
          "content-low": "#94A3B8", // Slate 400
          "border": "#334155", // Slate 700 - Subtle dark borders
          "border-strong": "#475569", // Slate 600 - Defined dark borders
          "selected": "#FB923C", // Orange 400 - Bright in dark mode
          "hover": "#FDBA74", // Orange 300 - Lighter on dark hover
        },
        // Legacy/Compatibility colors (keep for existing components)
        border: "hsl(var(--border))",
        input: "hsl(var(--input))",
        ring: "hsl(var(--ring))",
        background: "hsl(var(--background))",
        foreground: "hsl(var(--foreground))",
        primary: {
          DEFAULT: "hsl(var(--primary))",
          foreground: "hsl(var(--primary-foreground))",
        },
        secondary: {
          DEFAULT: "hsl(var(--secondary))",
          foreground: "hsl(var(--secondary-foreground))",
        },
        destructive: {
          DEFAULT: "hsl(var(--destructive))",
          foreground: "hsl(var(--destructive-foreground))",
        },
        muted: {
          DEFAULT: "hsl(var(--muted))",
          foreground: "hsl(var(--muted-foreground))",
        },
        accent: {
          DEFAULT: "hsl(var(--accent))",
          foreground: "hsl(var(--accent-foreground))",
        },
        popover: {
          DEFAULT: "hsl(var(--popover))",
          foreground: "hsl(var(--popover-foreground))",
        },
        card: {
          DEFAULT: "hsl(var(--card))",
          foreground: "hsl(var(--card-foreground))",
        },
      },
      fontFamily: {
        "sf-pro-display": ['"SF Pro Display"', "system-ui", "sans-serif"],
        "sf-pro-text": ['"SF Pro Text"', "system-ui", "sans-serif"],
        sans: [
          "system-ui",
          "-apple-system",
          "BlinkMacSystemFont",
          '"Segoe UI"',
          "Roboto",
          '"Helvetica Neue"',
          "Arial",
          "sans-serif",
        ],
      },
      fontSize: {
        // Display
        "display-xl": ["64px", { lineHeight: "80px", fontWeight: "700", letterSpacing: "-0.02em" }],
        "display-l": ["48px", { lineHeight: "60px", fontWeight: "700", letterSpacing: "-0.01em" }],
        "display-m": ["40px", { lineHeight: "52px", fontWeight: "600", letterSpacing: "0" }],
        "display-s": ["32px", { lineHeight: "44px", fontWeight: "600", letterSpacing: "0" }],
        // Headline
        "h1-headline-xl": ["32px", { lineHeight: "44px", fontWeight: "600", letterSpacing: "0" }],
        "h2-headline-l": ["28px", { lineHeight: "40px", fontWeight: "600", letterSpacing: "0" }],
        "h3-headline-m": ["24px", { lineHeight: "36px", fontWeight: "600", letterSpacing: "0" }],
        "h4-headline-s": ["20px", { lineHeight: "28px", fontWeight: "600", letterSpacing: "0" }],
        "h5-headline-xs": ["18px", { lineHeight: "28px", fontWeight: "600", letterSpacing: "0" }],
        // Body
        "body-xl": ["18px", { lineHeight: "28px", fontWeight: "400", letterSpacing: "0" }],
        "body-xl-bold": ["18px", { lineHeight: "28px", fontWeight: "600", letterSpacing: "0" }],
        "body-l": ["16px", { lineHeight: "24px", fontWeight: "400", letterSpacing: "0" }],
        "body-l-bold": ["16px", { lineHeight: "24px", fontWeight: "600", letterSpacing: "0" }],
        "body-m": ["15px", { lineHeight: "24px", fontWeight: "400", letterSpacing: "0" }],
        "body-m-bold": ["15px", { lineHeight: "24px", fontWeight: "600", letterSpacing: "0" }],
        "body-s": ["14px", { lineHeight: "20px", fontWeight: "400", letterSpacing: "0" }],
        "body-s-bold": ["14px", { lineHeight: "20px", fontWeight: "600", letterSpacing: "0" }],
        "body-xs": ["13px", { lineHeight: "18px", fontWeight: "400", letterSpacing: "0.1px" }],
        "body-xs-bold": ["13px", { lineHeight: "18px", fontWeight: "600", letterSpacing: "0.1px" }],
        // Caption
        "caption-m": ["12px", { lineHeight: "16px", fontWeight: "400", letterSpacing: "0.1px" }],
        "caption-m-bold": ["12px", { lineHeight: "16px", fontWeight: "600", letterSpacing: "0.1px" }],
        "caption-s": ["11px", { lineHeight: "16px", fontWeight: "400", letterSpacing: "0.2px" }],
        "caption-s-bold": ["11px", { lineHeight: "16px", fontWeight: "600", letterSpacing: "0.2px" }],
      },
      spacing: {
        "xxs": "4px",
        "xs": "8px",
        "sm": "12px",
        "md": "16px",
        "lg": "20px",
        "xl": "24px",
        "2xl": "32px",
        "3xl": "40px",
        "4xl": "48px",
        "5xl": "64px",
        "6xl": "80px",
        "7xl": "96px",
        "8xl": "128px",
      },
      borderRadius: {
        "none": "0",
        "xs": "4px",
        "sm": "6px",
        "md": "8px",
        "lg": "12px",
        "xl": "16px",
        "2xl": "20px",
        "3xl": "24px",
        "full": "9999px",
        lg: "var(--radius)",
        md: "calc(var(--radius) - 2px)",
        sm: "calc(var(--radius) - 4px)",
      },
      boxShadow: {
        "xs": "0px 1px 2px rgba(0, 0, 0, 0.05)",
        "sm": "0px 1px 3px rgba(0, 0, 0, 0.1), 0px 1px 2px rgba(0, 0, 0, 0.06)",
        "md": "0px 4px 6px -1px rgba(0, 0, 0, 0.1), 0px 2px 4px -1px rgba(0, 0, 0, 0.06)",
        "lg": "0px 10px 15px -3px rgba(0, 0, 0, 0.1), 0px 4px 6px -2px rgba(0, 0, 0, 0.05)",
        "xl": "0px 20px 25px -5px rgba(0, 0, 0, 0.1), 0px 10px 10px -5px rgba(0, 0, 0, 0.04)",
        "2xl": "0px 25px 50px -12px rgba(0, 0, 0, 0.25)",
        "inner": "inset 0px 2px 4px rgba(0, 0, 0, 0.06)",
      },
      keyframes: {
        "accordion-down": {
          from: { height: "0" },
          to: { height: "var(--radix-accordion-content-height)" },
        },
        "accordion-up": {
          from: { height: "var(--radix-accordion-content-height)" },
          to: { height: "0" },
        },
        "fade-in": {
          "0%": { opacity: "0" },
          "100%": { opacity: "1" },
        },
        "fade-out": {
          "0%": { opacity: "1" },
          "100%": { opacity: "0" },
        },
        "slide-in-from-top": {
          "0%": { transform: "translateY(-100%)" },
          "100%": { transform: "translateY(0)" },
        },
        "slide-in-from-bottom": {
          "0%": { transform: "translateY(100%)" },
          "100%": { transform: "translateY(0)" },
        },
        "slide-in-from-left": {
          "0%": { transform: "translateX(-100%)" },
          "100%": { transform: "translateX(0)" },
        },
        "slide-in-from-right": {
          "0%": { transform: "translateX(100%)" },
          "100%": { transform: "translateX(0)" },
        },
        "spin-around": {
          "0%": {
            transform: "translateZ(0) rotate(0)",
          },
          "15%, 35%": {
            transform: "translateZ(0) rotate(90deg)",
          },
          "65%, 85%": {
            transform: "translateZ(0) rotate(270deg)",
          },
          "100%": {
            transform: "translateZ(0) rotate(360deg)",
          },
        },
        "shimmer-slide": {
          to: {
            transform: "translate(calc(100cqw - 100%), 0)",
          },
        },
        "bounce-subtle": {
          "0%, 100%": {
            transform: "translateY(0)",
          },
          "50%": {
            transform: "translateY(-6px)",
          },
        },
        "pulse-subtle": {
          "0%, 100%": {
            opacity: "1",
            transform: "scale(1)",
          },
          "50%": {
            opacity: "0.9",
            transform: "scale(1.02)",
          },
        },
      },
      animation: {
        "accordion-down": "accordion-down 0.2s ease-out",
        "accordion-up": "accordion-up 0.2s ease-out",
        "fade-in": "fade-in 0.3s ease-in-out",
        "fade-out": "fade-out 0.3s ease-in-out",
        "slide-in-from-top": "slide-in-from-top 0.3s ease-out",
        "slide-in-from-bottom": "slide-in-from-bottom 0.3s ease-out",
        "slide-in-from-left": "slide-in-from-left 0.3s ease-out",
        "slide-in-from-right": "slide-in-from-right 0.3s ease-out",
        "shimmer-slide":
          "shimmer-slide var(--speed) ease-in-out infinite alternate",
        "spin-around": "spin-around calc(var(--speed) * 2) infinite linear",
        "bounce-subtle": "bounce-subtle 2s ease-in-out infinite",
        "pulse-subtle": "pulse-subtle 2s ease-in-out infinite",
      },
    },
  },
  plugins: [require("tailwindcss-animate")],
}
