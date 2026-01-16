package com.filesurf.service;

import com.filesurf.model.FeedbackRecord;
import com.filesurf.repository.FeedbackRepository;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.inject.Inject;

import java.util.List;
import java.util.logging.Logger;

/**
 * Service for feedback management operations.
 * Handles feedback storage and retrieval.
 */
@ApplicationScoped
public class FeedbackService {
    private static final Logger LOGGER = Logger.getLogger(FeedbackService.class.getName());

    @Inject
    FeedbackRepository feedbackRepository;

    /**
     * Get all feedback with pagination
     */
    public List<FeedbackRecord> getAllFeedback(int limit, int offset) {
        return feedbackRepository.findAll(limit, offset);
    }

    /**
     * Get feedback by user
     */
    public List<FeedbackRecord> getFeedbackByUser(String userId) {
        return feedbackRepository.findByUserId(userId);
    }

    /**
     * Get feedback by type
     */
    public List<FeedbackRecord> getFeedbackByType(String type) {
        return feedbackRepository.findByType(type);
    }

    /**
     * Get total feedback count
     */
    public long getTotalCount() {
        return feedbackRepository.count();
    }

    /**
     * Get feedback count by type
     */
    public long getCountByType(String type) {
        return feedbackRepository.countByType(type);
    }
}
