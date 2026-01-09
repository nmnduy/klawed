package com.filesurf.filter;

import io.vertx.core.http.HttpServerRequest;
import io.vertx.core.net.SocketAddress;
import jakarta.ws.rs.container.ContainerRequestContext;
import jakarta.ws.rs.core.Response;
import jakarta.ws.rs.core.UriInfo;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Nested;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.mockito.ArgumentCaptor;

import java.io.IOException;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class MetricsAccessFilterTest {

    private MetricsAccessFilter filter;
    private ContainerRequestContext requestContext;
    private HttpServerRequest httpServerRequest;
    private UriInfo uriInfo;

    @BeforeEach
    void setUp() {
        filter = new MetricsAccessFilter();
        requestContext = mock(ContainerRequestContext.class);
        httpServerRequest = mock(HttpServerRequest.class);
        uriInfo = mock(UriInfo.class);
        
        when(requestContext.getUriInfo()).thenReturn(uriInfo);
        filter.httpServerRequest = httpServerRequest;
    }

    @Nested
    @DisplayName("isMetricsEndpoint Tests")
    class IsMetricsEndpointTests {

        @Test
        @DisplayName("Should identify /metrics as metrics endpoint")
        void shouldIdentifyMetricsEndpoint() {
            assertTrue(filter.isMetricsEndpoint("/metrics"));
            assertTrue(filter.isMetricsEndpoint("metrics"));
        }

        @Test
        @DisplayName("Should identify /q/metrics as metrics endpoint")
        void shouldIdentifyQuarkusMetricsEndpoint() {
            assertTrue(filter.isMetricsEndpoint("/q/metrics"));
            assertTrue(filter.isMetricsEndpoint("q/metrics"));
        }

        @Test
        @DisplayName("Should not identify non-metrics paths")
        void shouldNotIdentifyNonMetricsPaths() {
            assertFalse(filter.isMetricsEndpoint("/health"));
            assertFalse(filter.isMetricsEndpoint("/file-chat"));
            assertFalse(filter.isMetricsEndpoint("/auth/login"));
            assertFalse(filter.isMetricsEndpoint("/metrics/something"));
            assertFalse(filter.isMetricsEndpoint(""));
            assertFalse(filter.isMetricsEndpoint(null));
        }
    }

    @Nested
    @DisplayName("isTailscaleIp Tests")
    class IsTailscaleIpTests {

        @ParameterizedTest
        @ValueSource(strings = {
            "100.0.0.1",
            "100.64.0.1",
            "100.127.255.254",
            "100.100.100.100",
            "100.1.2.3"
        })
        @DisplayName("Should identify Tailscale IPs (100.x.x.x)")
        void shouldIdentifyTailscaleIps(String ip) {
            assertTrue(filter.isTailscaleIp(ip));
        }

        @ParameterizedTest
        @ValueSource(strings = {
            "100.0.0.1:8080",
            "100.64.0.1:12345",
            "100.127.255.254:443"
        })
        @DisplayName("Should identify Tailscale IPs with port numbers")
        void shouldIdentifyTailscaleIpsWithPort(String ip) {
            assertTrue(filter.isTailscaleIp(ip));
        }

        @ParameterizedTest
        @ValueSource(strings = {
            "192.168.1.1",
            "10.0.0.1",
            "172.16.0.1",
            "8.8.8.8",
            "1.2.3.4",
            "99.100.200.300",
            "101.0.0.1"
        })
        @DisplayName("Should reject non-Tailscale IPs")
        void shouldRejectNonTailscaleIps(String ip) {
            assertFalse(filter.isTailscaleIp(ip));
        }

        @ParameterizedTest
        @ValueSource(strings = {"", "Unknown", "invalid"})
        @DisplayName("Should reject invalid or unknown IPs")
        void shouldRejectInvalidIps(String ip) {
            assertFalse(filter.isTailscaleIp(ip));
        }

        @Test
        @DisplayName("Should reject null IP")
        void shouldRejectNullIp() {
            assertFalse(filter.isTailscaleIp(null));
        }
    }

    @Nested
    @DisplayName("getClientIp Tests")
    class GetClientIpTests {

        @Test
        @DisplayName("Should prioritize CF-Connecting-IP header")
        void shouldPrioritizeCfConnectingIp() {
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn("100.0.0.1");
            when(httpServerRequest.getHeader("X-Forwarded-For")).thenReturn("192.168.1.1");
            
            SocketAddress socketAddress = mock(SocketAddress.class);
            when(socketAddress.toString()).thenReturn("10.0.0.1");
            when(httpServerRequest.remoteAddress()).thenReturn(socketAddress);

            String clientIp = filter.getClientIp();
            
            assertEquals("100.0.0.1", clientIp);
        }

        @Test
        @DisplayName("Should use X-Forwarded-For if CF-Connecting-IP not present")
        void shouldUseXForwardedForIfCfHeaderMissing() {
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn(null);
            when(httpServerRequest.getHeader("X-Forwarded-For")).thenReturn("100.0.0.1");
            
            SocketAddress socketAddress = mock(SocketAddress.class);
            when(socketAddress.toString()).thenReturn("10.0.0.1");
            when(httpServerRequest.remoteAddress()).thenReturn(socketAddress);

            String clientIp = filter.getClientIp();
            
            assertEquals("100.0.0.1", clientIp);
        }

        @Test
        @DisplayName("Should extract first IP from X-Forwarded-For chain")
        void shouldExtractFirstIpFromXForwardedFor() {
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn(null);
            when(httpServerRequest.getHeader("X-Forwarded-For")).thenReturn("100.0.0.1, 192.168.1.1, 10.0.0.1");

            String clientIp = filter.getClientIp();
            
            assertEquals("100.0.0.1", clientIp);
        }

        @Test
        @DisplayName("Should use remote address if no proxy headers present")
        void shouldUseRemoteAddressIfNoProxyHeaders() {
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn(null);
            when(httpServerRequest.getHeader("X-Forwarded-For")).thenReturn(null);
            
            SocketAddress socketAddress = mock(SocketAddress.class);
            when(socketAddress.toString()).thenReturn("100.0.0.1:8080");
            when(httpServerRequest.remoteAddress()).thenReturn(socketAddress);

            String clientIp = filter.getClientIp();
            
            assertEquals("100.0.0.1:8080", clientIp);
        }

        @Test
        @DisplayName("Should return Unknown if httpServerRequest is null")
        void shouldReturnUnknownIfRequestIsNull() {
            filter.httpServerRequest = null;

            String clientIp = filter.getClientIp();
            
            assertEquals("Unknown", clientIp);
        }

        @Test
        @DisplayName("Should return Unknown if remote address is null")
        void shouldReturnUnknownIfRemoteAddressIsNull() {
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn(null);
            when(httpServerRequest.getHeader("X-Forwarded-For")).thenReturn(null);
            when(httpServerRequest.remoteAddress()).thenReturn(null);

            String clientIp = filter.getClientIp();
            
            assertEquals("Unknown", clientIp);
        }
    }

    @Nested
    @DisplayName("filter Tests - Integration")
    class FilterIntegrationTests {

        @Test
        @DisplayName("Should allow access to /metrics from Tailscale IP")
        void shouldAllowAccessFromTailscaleIp() throws IOException {
            when(uriInfo.getPath()).thenReturn("/metrics");
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn("100.0.0.1");

            filter.filter(requestContext);

            verify(requestContext, never()).abortWith(any());
        }

        @Test
        @DisplayName("Should deny access to /metrics from non-Tailscale IP")
        void shouldDenyAccessFromNonTailscaleIp() throws IOException {
            when(uriInfo.getPath()).thenReturn("/metrics");
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn("192.168.1.1");

            filter.filter(requestContext);

            ArgumentCaptor<Response> responseCaptor = ArgumentCaptor.forClass(Response.class);
            verify(requestContext).abortWith(responseCaptor.capture());
            
            Response response = responseCaptor.getValue();
            assertEquals(Response.Status.FORBIDDEN.getStatusCode(), response.getStatus());
            assertTrue(response.getEntity().toString().contains("Tailscale network"));
        }

        @Test
        @DisplayName("Should deny access to /metrics from unknown IP")
        void shouldDenyAccessFromUnknownIp() throws IOException {
            when(uriInfo.getPath()).thenReturn("/metrics");
            filter.httpServerRequest = null;

            filter.filter(requestContext);

            ArgumentCaptor<Response> responseCaptor = ArgumentCaptor.forClass(Response.class);
            verify(requestContext).abortWith(responseCaptor.capture());
            
            Response response = responseCaptor.getValue();
            assertEquals(Response.Status.FORBIDDEN.getStatusCode(), response.getStatus());
        }

        @Test
        @DisplayName("Should allow access to /q/metrics from Tailscale IP")
        void shouldAllowAccessToQuarkusMetricsFromTailscaleIp() throws IOException {
            when(uriInfo.getPath()).thenReturn("/q/metrics");
            when(httpServerRequest.getHeader("X-Forwarded-For")).thenReturn("100.64.0.1");

            filter.filter(requestContext);

            verify(requestContext, never()).abortWith(any());
        }

        @Test
        @DisplayName("Should not filter non-metrics endpoints")
        void shouldNotFilterNonMetricsEndpoints() throws IOException {
            when(uriInfo.getPath()).thenReturn("/file-chat");
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn("192.168.1.1");

            filter.filter(requestContext);

            verify(requestContext, never()).abortWith(any());
        }

        @Test
        @DisplayName("Should allow access with Tailscale IP in X-Forwarded-For chain")
        void shouldAllowAccessWithTailscaleIpInXForwardedForChain() throws IOException {
            when(uriInfo.getPath()).thenReturn("/metrics");
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn(null);
            when(httpServerRequest.getHeader("X-Forwarded-For")).thenReturn("100.0.0.1, 192.168.1.1");

            filter.filter(requestContext);

            verify(requestContext, never()).abortWith(any());
        }

        @Test
        @DisplayName("Should handle Tailscale IP with port number")
        void shouldHandleTailscaleIpWithPort() throws IOException {
            when(uriInfo.getPath()).thenReturn("/metrics");
            when(httpServerRequest.getHeader("CF-Connecting-IP")).thenReturn(null);
            when(httpServerRequest.getHeader("X-Forwarded-For")).thenReturn(null);
            
            SocketAddress socketAddress = mock(SocketAddress.class);
            when(socketAddress.toString()).thenReturn("100.0.0.1:8080");
            when(httpServerRequest.remoteAddress()).thenReturn(socketAddress);

            filter.filter(requestContext);

            verify(requestContext, never()).abortWith(any());
        }
    }
}
