package com.filesurf.service;

import com.filesurf.model.UserRecord;
import com.filesurf.repository.UserRepository;
import io.quarkus.runtime.Startup;
import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.util.UUID;
import java.util.logging.Logger;

/**
 * Service for user management operations.
 * Provides authentication and user lookup functionality.
 */
@Startup
@ApplicationScoped
public class UserService {
    private static final Logger LOGGER = Logger.getLogger(UserService.class.getName());

    @Inject
    UserRepository userRepository;

    @PostConstruct
    void init() {
        LOGGER.info("Initializing UserService and users table schema...");
        userRepository.initializeSchema();
    }

    /**
     * Find or create a user by email.
     * If user exists, returns existing user and updates last login.
     * If user doesn't exist, creates a new user with generated userId.
     *
     * @param email The user's email address
     * @return The UserRecord (existing or newly created)
     */
    public UserRecord findOrCreateUserByEmail(String email) {
        if (email == null || email.trim().isEmpty()) {
            throw new IllegalArgumentException("Email cannot be null or empty");
        }

        String normalizedEmail = email.toLowerCase().trim();

        // Check if user exists
        UserRecord existingUser = userRepository.findByEmail(normalizedEmail);
        if (existingUser != null) {
            LOGGER.info("Found existing user for email: " + normalizedEmail + " with userId: " + existingUser.getUserId());
            userRepository.updateLastLogin(existingUser.getUserId());
            return existingUser;
        }

        // Create new user with generated userId
        String newUserId = "user-" + UUID.randomUUID().toString();
        UserRecord newUser = userRepository.createUser(normalizedEmail, newUserId);
        LOGGER.info("Created new user for email: " + normalizedEmail + " with userId: " + newUserId);
        return newUser;
    }

    /**
     * Get user by userId (cookie value)
     *
     * @param userId The userId from the cookie
     * @return The UserRecord or null if not found
     */
    public UserRecord getUserByUserId(String userId) {
        if (userId == null || userId.trim().isEmpty()) {
            return null;
        }
        return userRepository.findByUserId(userId);
    }

    /**
     * Get user by email
     *
     * @param email The email address
     * @return The UserRecord or null if not found
     */
    public UserRecord getUserByEmail(String email) {
        if (email == null || email.trim().isEmpty()) {
            return null;
        }
        return userRepository.findByEmail(email.toLowerCase().trim());
    }

    /**
     * Check if a userId has a registered email
     *
     * @param userId The userId to check
     * @return true if userId exists and has an email
     */
    public boolean isUserRegistered(String userId) {
        if (userId == null || userId.trim().isEmpty()) {
            return false;
        }
        UserRecord user = userRepository.findByUserId(userId);
        return user != null && user.getEmail() != null && !user.getEmail().isEmpty();
    }

    /**
     * Update the last login timestamp for a user
     *
     * @param userId The userId to update
     */
    public void updateLastLogin(String userId) {
        userRepository.updateLastLogin(userId);
    }

    /**
     * Invite a new user by email (admin function).
     * Creates a new user account that can then log in.
     *
     * @param email The email to invite
     * @return The created UserRecord
     * @throws IllegalArgumentException if email is invalid or already exists
     */
    public UserRecord inviteUser(String email) {
        if (email == null || email.trim().isEmpty()) {
            throw new IllegalArgumentException("Email cannot be null or empty");
        }

        if (!isValidEmail(email)) {
            throw new IllegalArgumentException("Invalid email format: " + email);
        }

        String normalizedEmail = email.toLowerCase().trim();

        // Check if user already exists
        UserRecord existingUser = userRepository.findByEmail(normalizedEmail);
        if (existingUser != null) {
            throw new IllegalArgumentException("User already exists with email: " + normalizedEmail);
        }

        // Create new user with generated userId
        String newUserId = "user-" + java.util.UUID.randomUUID().toString();
        UserRecord newUser = userRepository.createUser(normalizedEmail, newUserId);
        LOGGER.info("Invited new user with email: " + normalizedEmail + " userId: " + newUserId);
        return newUser;
    }

    /**
     * Validate email format
     *
     * @param email The email to validate
     * @return true if email format is valid
     */
    public boolean isValidEmail(String email) {
        if (email == null || email.trim().isEmpty()) {
            return false;
        }
        // Simple email validation - contains @ and at least one dot after @
        String trimmed = email.trim();
        int atIndex = trimmed.indexOf('@');
        if (atIndex < 1) {
            return false;
        }
        String domain = trimmed.substring(atIndex + 1);
        return domain.contains(".") && domain.indexOf('.') < domain.length() - 1;
    }
}
