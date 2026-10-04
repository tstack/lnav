package com.example.net;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Server {
    private static final Logger logger = LoggerFactory.getLogger(Server.class);

    public Server(int port) {
        logger.info("constructing server on port {}", port);
    }

    public void handle(String client) {
        logger.debug("handling request from {}", client);
        Runnable r = () -> logger.warn("client {} is slow", client);
        r.run();
    }

    static class Inner {
        void run() {
            logger.error("inner worker failed");
        }
    }

    public static void main(String[] args) {
        Server server = new Server(8080);
        server.handle("10.0.0.1");
        new Inner().run();
    }
}
