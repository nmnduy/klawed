package com.filesurf.service;

import com.filesurf.model.WaitlistEntry;
import com.filesurf.repository.WaitlistRepository;
import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.util.logging.Logger;

@ApplicationScoped
public class WaitlistService {
    private static final Logger LOGGER = Logger.getLogger(WaitlistService.class.getName());

    @Inject
    WaitlistRepository waitlistRepository;

    @PostConstruct
    void init() {
        LOGGER.info("Initializing WaitlistService and waitlist table schema...");
        waitlistRepository.initializeSchema();
    }

    public WaitlistEntry addToWaitlist(String email, String name, String useCase) {
        if (email == null || email.trim().isEmpty()) {
            throw new IllegalArgumentException("Email cannot be null or empty");
        }
        return waitlistRepository.insert(email, name, useCase);
    }
}
