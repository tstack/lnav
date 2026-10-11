package com.example;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Job {
    private static final Logger logger = LoggerFactory.getLogger(Job.class);

    public void run(String status, String details, String trace) {
        logger.info("status {}", status);
        logger.info("status ok\ndetails: {}", details);
        logger.info("status ok\ndetails: {}\ntrace: {}", details, trace);
    }
}
