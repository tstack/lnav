// A driver whose messages are in log2src/c/dmesg.0, a log without levels.

static int drv_probe(struct device *dev, int err)
{
    if (err < 0) {
        dev_err(dev, "failed to enable device: %d\n", err);
        return err;
    }
    dev_warn(dev, "link is slow to respond, please be patient\n");
    dev_info(dev, "registered as drv%d\n", err);
    return 0;
}

static void drv_report(struct device *dev, const char *what, const char *why)
{
    // Almost all placeholders, so it explains too little of a message for
    // its level to say anything about it.
    dev_err(dev, "%s: %s\n", what, why);
}
