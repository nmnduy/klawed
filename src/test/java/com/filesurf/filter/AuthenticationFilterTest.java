package com.filesurf.filter;

import com.filesurf.model.UserRecord;
import com.filesurf.service.UserService;
import jakarta.ws.rs.container.ContainerRequestContext;
import jakarta.ws.rs.core.Cookie;
import jakarta.ws.rs.core.Response;
import jakarta.ws.rs.core.UriInfo;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.mockito.ArgumentCaptor;

import java.net.URI;
import java.util.HashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

/**
 * Unit tests for AuthenticationFilter.
 * These tests use pure unit testing without starting the Quarkus container.
 */
class AuthenticationFilterTest {

    private AuthenticationFilter filter;
    private UserService userService;
    private ContainerRequestContext mockRequestContext;
    private UriInfo mockUriInfo;

    @BeforeEach
    void setUp() {
        filter = new AuthenticationFilter();
        userService = mock(UserService.class);
        filter.userService = userService;

        mockRequestContext = mock(ContainerRequestContext.class);
        mockUriInfo = mock(UriInfo.class);
        when(mockRequestContext.getUriInfo()).thenReturn(mockUriInfo);
    }

    @Nested
    @DisplayName("Path Normalization Tests")
    class PathNormalizationTests {

        @ParameterizedTest
        @DisplayName("Should handle empty and root paths correctly")
        @CsvSource({
            "'', /file-chat",
            "/, /file-chat",
        })
        void shouldHandleEmptyAndRootPaths(String input, String expected) {
            assertEquals(expected, filter.normalizeRedirectPath(input));
        }

        @ParameterizedTest
        @DisplayName("Should preserve valid paths")
        @ValueSource(strings = {"/file-chat", "/session/123", "/file-chat/upload"})
        void shouldPreserveValidPaths(String path) {
            assertEquals(path, filter.normalizeRedirectPath(path));
        }

        @ParameterizedTest
        @DisplayName("Should add leading slash to paths without one")
        @CsvSource({
            "file-chat, /file-chat",
            "session/123, /session/123",
            "some/path, /some/path"
        })
        void shouldAddLeadingSlash(String input, String expected) {
            assertEquals(expected, filter.normalizeRedirectPath(input));
        }

        @Test
        @DisplayName("Should handle null path")
        void shouldHandleNullPath() {
            assertEquals("/file-chat", filter.normalizeRedirectPath(null));
        }
    }

    @Nested
    @DisplayName("Redirect URL Building Tests")
    class RedirectUrlBuildingTests {

        @ParameterizedTest
        @DisplayName("Should build correct redirect URLs")
        @CsvSource({
            "/file-chat, /auth/login?redirect=%2Ffile-chat",
            "/session/123, /auth/login?redirect=%2Fsession%2F123",
            "/, /auth/login?redirect=%2F",
        })
        void shouldBuildCorrectRedirectUrls(String targetPath, String expectedUrl) {
            assertEquals(expectedUrl, filter.buildLoginRedirectUrl(targetPath));
        }

        @Test
        @DisplayName("Should not produce double slashes in redirect when processing root path")
        void shouldNotProduceDoubleSlashesForRoot() {
            // Test the full flow: normalize path + build URL
            String normalizedPath = filter.normalizeRedirectPath("/");
            String redirectUrl = filter.buildLoginRedirectUrl(normalizedPath);

            // The normalized path should be /file-chat (not / anymore)
            assertEquals("/file-chat", normalizedPath);
            // The redirect should not have // in the decoded form
            assertEquals("/auth/login?redirect=%2Ffile-chat", redirectUrl);
            assertFalse(redirectUrl.contains("//"),
                "Redirect URL should not contain double slashes: " + redirectUrl);
        }

        @Test
        @DisplayName("Should not produce double slashes in redirect when processing empty path")
        void shouldNotProduceDoubleSlashesForEmpty() {
            // Test the full flow: normalize path + build URL
            String normalizedPath = filter.normalizeRedirectPath("");
            String redirectUrl = filter.buildLoginRedirectUrl(normalizedPath);

            // The normalized path should be /file-chat (not // )
            assertEquals("/file-chat", normalizedPath);
            // The redirect should point to /file-chat
            assertEquals("/auth/login?redirect=%2Ffile-chat", redirectUrl);
        }

        @Test
        @DisplayName("Should properly encode special characters")
        void shouldEncodeSpecialCharacters() {
            String redirectUrl = filter.buildLoginRedirectUrl("/path?query=value&other=data");
            assertTrue(redirectUrl.contains("%3F"), "Should encode '?'");
            assertTrue(redirectUrl.contains("%3D"), "Should encode '='");
            assertTrue(redirectUrl.contains("%26"), "Should encode '&'");
        }
    }

    @Nested
    @DisplayName("URL Encoding Tests")
    class UrlEncodingTests {

        @ParameterizedTest
        @DisplayName("Should encode paths correctly")
        @CsvSource({
            "/file-chat, %2Ffile-chat",
            "/session/123, %2Fsession%2F123",
            "/path with spaces, %2Fpath+with+spaces"
        })
        void shouldEncodePaths(String input, String expected) {
            assertEquals(expected, filter.encodeRedirect(input));
        }

        @Test
        @DisplayName("Should handle null input")
        void shouldHandleNullInput() {
            assertEquals("", filter.encodeRedirect(null));
        }

        @Test
        @DisplayName("Should encode special characters")
        void shouldEncodeSpecialCharacters() {
            String encoded = filter.encodeRedirect("/path?query=value&key=data");
            assertFalse(encoded.contains("?"));
            assertFalse(encoded.contains("&"));
            assertFalse(encoded.contains("="));
        }
    }

    @Nested
    @DisplayName("Authentication Skip Tests")
    class AuthenticationSkipTests {

        @ParameterizedTest
        @DisplayName("Should skip authentication for auth endpoints")
        @ValueSource(strings = {
            "/auth/login", "auth/login", "/auth/logout", "auth/logout",
            "/auth/status", "auth/status", "/auth", "auth"
        })
        void shouldSkipAuthEndpoints(String path) {
            assertTrue(filter.shouldSkipAuthentication(path),
                "Should skip authentication for: " + path);
        }

        @ParameterizedTest
        @DisplayName("Should skip authentication for static assets")
        @ValueSource(strings = {
            "/assets/logo.png", "assets/logo.png",
            "/js/script.js", "js/script.js",
            "/css/style.css", "css/style.css",
            "/favicon.ico", "/image.png", "/style.css", "/app.js"
        })
        void shouldSkipStaticAssets(String path) {
            assertTrue(filter.shouldSkipAuthentication(path),
                "Should skip authentication for: " + path);
        }

        @ParameterizedTest
        @DisplayName("Should skip authentication for health and metrics")
        @ValueSource(strings = {
            "/health", "health", "/health/live", "health/live",
            "/health/ready", "health/ready", "/metrics", "metrics",
            "/q/dev", "q/dev"
        })
        void shouldSkipHealthAndMetrics(String path) {
            assertTrue(filter.shouldSkipAuthentication(path),
                "Should skip authentication for: " + path);
        }

        @ParameterizedTest
        @DisplayName("Should NOT skip authentication for protected endpoints")
        @ValueSource(strings = {
            "/file-chat", "file-chat", "/session/123", "session/123",
            "/file-chat/upload", "file-chat/upload",
            "/some/random/path", "/", ""
        })
        void shouldNotSkipProtectedEndpoints(String path) {
            assertFalse(filter.shouldSkipAuthentication(path),
                "Should NOT skip authentication for: " + path);
        }

        @Test
        @DisplayName("Should handle null path")
        void shouldHandleNullPath() {
            assertFalse(filter.shouldSkipAuthentication(null));
        }
    }

    @Nested
    @DisplayName("API Request Detection Tests")
    class ApiRequestDetectionTests {

        @Test
        @DisplayName("Should detect API request by Accept header")
        void shouldDetectApiByAcceptHeader() {
            when(mockRequestContext.getHeaderString("Accept")).thenReturn("application/json");
            assertTrue(filter.isApiRequest(mockRequestContext, "/any-path"));
        }

        @Test
        @DisplayName("Should detect API request by Content-Type header")
        void shouldDetectApiByContentType() {
            when(mockRequestContext.getHeaderString("Content-Type")).thenReturn("application/json");
            assertTrue(filter.isApiRequest(mockRequestContext, "/any-path"));
        }

        @ParameterizedTest
        @DisplayName("Should detect API request by path pattern")
        @ValueSource(strings = {
            "/file-chat/http/some-endpoint",
            "file-chat/http/some-endpoint",
            "/session/generate",
            "session/generate",
            "/file-chat/upload/list",
            "file-chat/upload/list",
            "/file-chat/explorer/list",
            "file-chat/explorer/list"
        })
        void shouldDetectApiByPathPattern(String path) {
            assertTrue(filter.isApiRequest(mockRequestContext, path),
                "Should detect as API request: " + path);
        }

        @ParameterizedTest
        @DisplayName("Should NOT detect browser request by path")
        @ValueSource(strings = {
            "/file-chat", "file-chat", "/", "/some-page", "some-page"
        })
        void shouldNotDetectBrowserRequest(String path) {
            assertFalse(filter.isApiRequest(mockRequestContext, path),
                "Should NOT detect as API request: " + path);
        }

        @Test
        @DisplayName("Should handle null path")
        void shouldHandleNullPath() {
            assertFalse(filter.isApiRequest(mockRequestContext, null));
        }
    }

    @Nested
    @DisplayName("Integration Tests")
    class IntegrationTests {

        @Test
        @DisplayName("Should redirect unauthenticated browser request to login with correct URL")
        void shouldRedirectUnauthenticatedBrowserRequest() throws Exception {
            // Setup
            when(mockUriInfo.getPath()).thenReturn("/");
            when(mockRequestContext.getCookies()).thenReturn(new HashMap<>());

            // Execute
            filter.filter(mockRequestContext);

            // Verify redirect response
            ArgumentCaptor<Response> responseCaptor = ArgumentCaptor.forClass(Response.class);
            verify(mockRequestContext).abortWith(responseCaptor.capture());

            Response response = responseCaptor.getValue();
            assertEquals(Response.Status.SEE_OTHER.getStatusCode(), response.getStatus());

            URI location = (URI) response.getMetadata().getFirst("Location");
            assertNotNull(location);
            String locationStr = location.toString();
            assertEquals("/auth/login?redirect=%2Ffile-chat", locationStr,
                "Root path should redirect to login with /file-chat as target");
        }

        @Test
        @DisplayName("Should return 401 for unauthenticated API request")
        void shouldReturn401ForUnauthenticatedApiRequest() throws Exception {
            // Setup
            when(mockUriInfo.getPath()).thenReturn("/session/generate");
            when(mockRequestContext.getCookies()).thenReturn(new HashMap<>());
            when(mockRequestContext.getHeaderString("Accept")).thenReturn("application/json");

            // Execute
            filter.filter(mockRequestContext);

            // Verify 401 response
            ArgumentCaptor<Response> responseCaptor = ArgumentCaptor.forClass(Response.class);
            verify(mockRequestContext).abortWith(responseCaptor.capture());

            Response response = responseCaptor.getValue();
            assertEquals(Response.Status.UNAUTHORIZED.getStatusCode(), response.getStatus());
        }

        @Test
        @DisplayName("Should allow authenticated user with valid cookie")
        void shouldAllowAuthenticatedUser() throws Exception {
            // Setup
            String userId = "user-123";
            String email = "user@example.com";

            Map<String, Cookie> cookies = new HashMap<>();
            cookies.put("filesurf_userId", new Cookie("filesurf_userId", userId));

            UserRecord mockUser = new UserRecord();
            mockUser.setUserId(userId);
            mockUser.setEmail(email);

            when(mockUriInfo.getPath()).thenReturn("/file-chat");
            when(mockRequestContext.getCookies()).thenReturn(cookies);
            when(userService.getUserByUserId(userId)).thenReturn(mockUser);

            // Execute
            filter.filter(mockRequestContext);

            // Verify no abort
            verify(mockRequestContext, never()).abortWith(any());
        }

        @Test
        @DisplayName("Should reject user with invalid cookie")
        void shouldRejectInvalidCookie() throws Exception {
            // Setup
            String userId = "invalid-user";

            Map<String, Cookie> cookies = new HashMap<>();
            cookies.put("filesurf_userId", new Cookie("filesurf_userId", userId));

            when(mockUriInfo.getPath()).thenReturn("/file-chat");
            when(mockRequestContext.getCookies()).thenReturn(cookies);
            when(userService.getUserByUserId(userId)).thenReturn(null);

            // Execute
            filter.filter(mockRequestContext);

            // Verify redirect
            ArgumentCaptor<Response> responseCaptor = ArgumentCaptor.forClass(Response.class);
            verify(mockRequestContext).abortWith(responseCaptor.capture());

            Response response = responseCaptor.getValue();
            assertEquals(Response.Status.SEE_OTHER.getStatusCode(), response.getStatus());
        }

        @Test
        @DisplayName("Should allow access to public endpoints without authentication")
        void shouldAllowPublicEndpoints() throws Exception {
            // Setup - no cookies
            when(mockUriInfo.getPath()).thenReturn("/auth/login");
            when(mockRequestContext.getCookies()).thenReturn(new HashMap<>());

            // Execute
            filter.filter(mockRequestContext);

            // Verify no abort
            verify(mockRequestContext, never()).abortWith(any());
        }
    }

    @Nested
    @DisplayName("Edge Case Tests")
    class EdgeCaseTests {

        @Test
        @DisplayName("Should handle empty cookie value")
        void shouldHandleEmptyCookie() throws Exception {
            Map<String, Cookie> cookies = new HashMap<>();
            cookies.put("filesurf_userId", new Cookie("filesurf_userId", ""));

            when(mockUriInfo.getPath()).thenReturn("/file-chat");
            when(mockRequestContext.getCookies()).thenReturn(cookies);

            filter.filter(mockRequestContext);

            verify(mockRequestContext).abortWith(any());
        }

        @Test
        @DisplayName("Should handle blank cookie value with spaces")
        void shouldHandleBlankCookie() throws Exception {
            Map<String, Cookie> cookies = new HashMap<>();
            cookies.put("filesurf_userId", new Cookie("filesurf_userId", "   "));

            when(mockUriInfo.getPath()).thenReturn("/file-chat");
            when(mockRequestContext.getCookies()).thenReturn(cookies);

            filter.filter(mockRequestContext);

            verify(mockRequestContext).abortWith(any());
        }

        @Test
        @DisplayName("Should handle path with double slashes correctly")
        void shouldHandleDoubleSlashesInPath() {
            String normalized = filter.normalizeRedirectPath("//file-chat");
            assertEquals("//file-chat", normalized);

            String redirectUrl = filter.buildLoginRedirectUrl(normalized);
            assertTrue(redirectUrl.contains("%2F%2Ffile-chat"));
        }
    }
}
