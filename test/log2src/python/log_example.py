import logging
import sys

def setup_logging():
    handler = logging.StreamHandler(sys.stdout)
    formatter = logging.Formatter("%(asctime)s %(levelname)s %(message)s")
    handler.setFormatter(formatter)
    root = logging.getLogger()
    root.setLevel(logging.DEBUG)
    # Remove default handlers if any
    if root.handlers:
        root.handlers.clear()
    root.addHandler(handler)


def main():
    setup_logging()
    logger = logging.getLogger("python_logging_example")
    logger.debug("debug message: initializing demo \N{greek small letter pi}")
    logger.info(rf"""info message:
processing started -- {sys.argv[0]}""")
    logger.warning(f"warning message:\nlow disk space")
    logger.error("error message: failed to open resource")
    try:
        1 / 0
    except Exception:
        logger.exception("exception message: caught unexpected error")


if __name__ == "__main__":
    main()
