package com.filesurf.db;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.AfterEach;
import java.sql.Connection;
import java.sql.Statement;
import java.sql.ResultSet;
import static org.junit.jupiter.api.Assertions.*;

public class SQLiteManagerTest {
    
    private SQLiteManager sqliteManager;
    
    @BeforeEach
    void setUp() throws Exception {
        sqliteManager = new SQLiteManager();
        // Use reflection to call init method since it's @PostConstruct
        var initMethod = SQLiteManager.class.getDeclaredMethod("init");
        initMethod.setAccessible(true);
        initMethod.invoke(sqliteManager);
    }
    
    @AfterEach
    void tearDown() throws Exception {
        // Use reflection to call cleanup method since it's @PreDestroy
        var cleanupMethod = SQLiteManager.class.getDeclaredMethod("cleanup");
        cleanupMethod.setAccessible(true);
        cleanupMethod.invoke(sqliteManager);
    }
    
    @Test
    void testConnection() throws Exception {
        sqliteManager.execute((SQLiteManager.ConnectionConsumer<Void>) conn -> {
            assertNotNull(conn);
            assertFalse(conn.isClosed());
            return null;
        });
    }
    
    @Test
    void testPragmas() throws Exception {
        sqliteManager.execute((SQLiteManager.ConnectionConsumer<Void>) conn -> {
            try (Statement stmt = conn.createStatement();
                 ResultSet rs = stmt.executeQuery("PRAGMA journal_mode")) {
                assertTrue(rs.next());
                assertEquals("wal", rs.getString(1).toLowerCase());
            }
            
            try (Statement stmt = conn.createStatement();
                 ResultSet rs = stmt.executeQuery("PRAGMA foreign_keys")) {
                assertTrue(rs.next());
                assertEquals(1, rs.getInt(1));
            }
            
            return null;
        });
    }
    
    @Test
    void testTransaction() throws Exception {
        String result = sqliteManager.executeInTransaction((SQLiteManager.ConnectionConsumer<String>) conn -> {
            try (Statement stmt = conn.createStatement()) {
                stmt.execute("CREATE TABLE IF NOT EXISTS test_table (id INTEGER PRIMARY KEY, name TEXT)");
                stmt.execute("INSERT INTO test_table (name) VALUES ('test')");
                
                try (ResultSet rs = stmt.executeQuery("SELECT name FROM test_table WHERE id = 1")) {
                    if (rs.next()) {
                        return rs.getString("name");
                    }
                }
            }
            return null;
        });
        
        assertEquals("test", result);
    }
}