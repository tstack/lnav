package com.example.net;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Meter {
    private static final Logger logger = LoggerFactory.getLogger(Meter.class);

    void record(int count, Double elapsed, String name) {
        logger.info("batch of {} took {} ms for {}", count, elapsed, name);
    }

    void transfer(String size, String elapsed) {
        logger.info("sent {} in {}", size, elapsed);
    }
}
