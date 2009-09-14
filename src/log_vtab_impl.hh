
#ifndef __vtab_impl_hh
#define __vtab_impl_hh

#include <soci.h>

#include <sqlite3/soci-sqlite3.h>
#include <sqlite3.h>

#include <map>
#include <string>

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
			 sqlite_api::sqlite3_context *ctx) {
    };
    
private:
    const std::string vi_name;
};

class log_vtab_manager {
public:
    log_vtab_manager(soci::session &sql, logfile_sub_source &lss);

    logfile_sub_source *get_source() { return &this->vm_source; };
    
    void register_vtab(log_vtab_impl *vi);
    log_vtab_impl *lookup_impl(std::string name) {
	return this->vm_impls[name];
    };
    
private:
    soci::session &vm_sql;
    logfile_sub_source &vm_source;
    std::map<std::string, log_vtab_impl *> vm_impls;
};

#endif
