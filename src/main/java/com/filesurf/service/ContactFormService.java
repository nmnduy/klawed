package com.filesurf.service;

import com.filesurf.model.ContactFormEntry;
import com.filesurf.repository.ContactFormRepository;
import jakarta.annotation.PostConstruct;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.util.List;
import java.util.logging.Logger;

@ApplicationScoped
public class ContactFormService {
    private static final Logger LOGGER = Logger.getLogger(ContactFormService.class.getName());

    @Inject
    ContactFormRepository contactFormRepository;

    @Inject
    MetricsService metricsService;

    @PostConstruct
    void init() {
        LOGGER.info("Initializing ContactFormService and contact forms table schema...");
        contactFormRepository.initializeSchema();
    }

    public ContactFormEntry submitForm(String email, String company, String message) {
        if (email == null || email.trim().isEmpty()) {
            throw new IllegalArgumentException("Email is required");
        }
        ContactFormEntry entry = contactFormRepository.insert(email, company, message);
        metricsService.incrementContactFormSubmissions();
        LOGGER.info("Contact form submission recorded: " + email);
        return entry;
    }

    public List<ContactFormEntry> getAllSubmissions() {
        return contactFormRepository.findAll();
    }

    public long getSubmissionCount() {
        return contactFormRepository.count();
    }
}
