package com.filesurf.service;

import jakarta.enterprise.context.ApplicationScoped;
import java.util.logging.Logger;

/**
 * Service to manage klawed agents lifecycle
 */
@ApplicationScoped
public class KlawedService {

    private static final Logger LOGGER = Logger.getLogger(KlawedService.class.getName());

    public KlawedService() {
        LOGGER.info("KlawedService initialized");
    }

    public void onStop() {
        LOGGER.info("KlawedService shutdown");
    }

}