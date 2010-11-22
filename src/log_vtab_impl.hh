
#ifndef __vtab_impl_hh
#define __vtab_impl_hh

#include <sqlite3.h>

#include <map>
#include <string>
#include <vector>

enum {
    VT_COL_LINE_NUMBER,
    VT_COL_PATH,
    VT_COL_LOG_TIME,
    VT_COL_IDLE_MSECS,
    VT_COL_LEVEL,
    VT_COL_RAW_LINE,
    VT_COL_MAX
};

class logfile_sub_source;

class log_vtab_impl {
public:
    struct vtab_column {
	vtab_column(const char *name, const char *type)
	    : vc_name(name), vc_type(type) { };
	
	const char *vc_name;
	const char *vc_type;
    };
    
    log_vtab_impl(const std::string name) : vi_name(name) { };
    virtual ~log_vtab_impl() { };
    
    const std::string &get_name(void) const {
	return this->vi_name;
    };
    
    virtual void get_columns(std::vector<vtab_column> &cols) { };

    virtual void extract(const std::string &line,
			 int column,
			 sqlite3_context *ctx) {
    };
    
private:
    const std::string vi_name;
};

class log_vtab_manager {
public:
    log_vtab_manager(sqlite3 *db, logfile_sub_source &lss);

    logfile_sub_source *get_source() { return &this->vm_source; };
    
    void register_vtab(log_vtab_impl *vi);
    log_vtab_impl *lookup_impl(std::string name) {
	return this->vm_impls[name];
    };
    
private:
    sqlite3 *vm_db;
    logfile_sub_source &vm_source;
    std::map<std::string, log_vtab_impl *> vm_impls;
};

#endif
