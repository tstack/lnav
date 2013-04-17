/**
 * @file log_format.hh
 */

#ifndef __log_format_hh
#define __log_format_hh

#include <assert.h>
#include <time.h>
#include <stdint.h>
#include <sys/types.h>

#include <string>
#include <vector>
#include <memory>

/**
 * Metadata for a single line in a log file.
 */
class logline {
public:

    /**
     * The logging level identifiers for a line(s).
     */
    typedef enum {
	LEVEL_UNKNOWN,
	LEVEL_TRACE,
	LEVEL_DEBUG,
	LEVEL_INFO,
	LEVEL_WARNING,
	LEVEL_ERROR,
	LEVEL_CRITICAL,

	LEVEL__MAX,

	LEVEL_MULTILINE = 0x40,  /*< Start of a multiline entry. */
	LEVEL_CONTINUED = 0x80,  /*< Continuation of multiline entry. */

	/** Mask of flags for the level field. */
	LEVEL__FLAGS    = (LEVEL_MULTILINE | LEVEL_CONTINUED)
    } level_t;

    static const char *level_names[LEVEL__MAX];

    static level_t string2level(const char *levelstr);

    /**
     * Construct a logline object with the given values.
     *
     * @param off The offset of the line in the file.
     * @param t The timestamp for the line.
     * @param millis The millisecond timestamp for the line.
     * @param l The logging level.
     */
    logline(off_t off,
	    time_t t,
	    uint16_t millis,
	    level_t l,
	    uint8_t m = 0)
	: ll_offset(off),
	  ll_time(t),
	  ll_millis(millis),
	  ll_level(l),
	  ll_module(m) { };

    /** @return The offset of the line in the file. */
    off_t get_offset() const { return this->ll_offset; };

    /** @return The timestamp for the line. */
    time_t get_time() const { return this->ll_time; };

    void set_time(time_t t) { this->ll_time = t; };

    /** @return The millisecond timestamp for the line. */
    uint16_t get_millis() const { return this->ll_millis; };

    void set_multiline(void) { this->ll_level |= LEVEL_MULTILINE; };

    /** @param l The logging level. */
    void set_level(level_t l) { this->ll_level = l; };

    /** @return The logging level. */
    level_t get_level() const { return (level_t)(this->ll_level & 0xff); };

    const char *get_level_name() const {
	return level_names[this->ll_level & 0x0f];
    };
    
    uint8_t get_module() const { return this->ll_module; };

    /**
     * Compare loglines based on their timestamp.
     */
    bool operator<(const logline &rhs) const
    {
	return this->ll_time < rhs.ll_time ||
	       (this->ll_time == rhs.ll_time &&
		this->ll_millis < rhs.ll_millis);
    };

    bool operator<(const time_t &rhs) const { return this->ll_time < rhs; };

private:
    off_t   ll_offset;
    time_t  ll_time;
    uint16_t   ll_millis;
    uint8_t ll_level;
    uint8_t ll_module;
};

/**
 * Base class for implementations of log format parsers.
 */
class log_format {
public:
    /**
     * @return The collection of builtin log formats.
     */
    static std::vector<log_format *> &get_root_formats(void);

    /**
     * Template used to register log formats during initialization.
     */
    template<class T> class register_root_format {
    public:
	register_root_format() {
	    static T format;

	    log_format::lf_root_formats.push_back(&format);
	};
    };

    log_format() : lf_fmt_lock(-1), lf_time_fmt_lock(-1) { };
    virtual ~log_format() { };

    virtual void clear(void) {
	this->lf_fmt_lock = -1;
	this->lf_time_fmt_lock = -1;
    };

    /**
     * Get the name of this log format.
     * 
     * @return The log format name.
     */
    virtual std::string get_name(void) = 0;

    /**
     * Scan a log line to see if it matches this log format.
     *
     * @param dst The vector of loglines that the formatter should append to
     *   if it detected a match.
     * @param offset The offset in the file where this line is located.
     * @param prefix The contents of the line.
     * @param len The length of the prefix string.
     */
    virtual bool scan(std::vector< logline > &dst,
		      off_t offset,
		      char *prefix,
		      int len) = 0;

    virtual void scrub(std::string &line) { };
    
    virtual std::auto_ptr<log_format> specialized(void) = 0;

protected:
    static std::vector<log_format *> lf_root_formats;

    char *log_scanf(const char *line,
		    const char *fmt[],
		    int expected_matches,
		    const char *time_fmt[],
		    char *time_dest,
		    struct tm *tm_out,
		    time_t &time_out,
		    ...);

    int lf_fmt_lock;
    int lf_time_fmt_lock;
};

/**
 * Convert the time stored in a 'tm' struct into epoch time.
 *
 * @param t The 'tm' structure to convert to epoch time.
 * @return The given time in seconds since the epoch.
 */
time_t tm2sec(const struct tm *t);

#endif
