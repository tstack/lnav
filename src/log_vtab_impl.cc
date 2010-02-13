
#include "config.h"

#include "log_vtab_impl.hh"

#include "logfile_sub_source.hh"

using namespace std;

static string declare_table_statement(log_vtab_impl *vi)
{
    std::vector<log_vtab_impl::vtab_column> cols;
    std::vector<log_vtab_impl::vtab_column>::const_iterator iter;
    std::ostringstream oss;
    
    oss << "CREATE TABLE unused (\n"
	<< "  line_number int,\n"
	<< "  path text,\n"
	<< "  log_time datetime,\n"
	<< "  idle_msecs int,\n"
	<< "  level text,\n"
	<< "  raw_line text";
    vi->get_columns(cols);
    for (iter = cols.begin(); iter != cols.end(); iter++) {
	oss << ",\n";
	oss << "  " << iter->vc_name << " " << iter->vc_type;
    }
    oss << "\n);";

    return oss.str();
}

struct vtab {
    sqlite3_vtab base;
    sqlite3 *db;
    logfile_sub_source *lss;
    log_vtab_impl *vi;
};

struct vtab_cursor {
    sqlite3_vtab_cursor base;
    vis_line_t curr_line;
};

static int vt_destructor(sqlite3_vtab *p_svt);

static int vt_create( sqlite3 *db,
                      void *pAux,
                      int argc, const char *const*argv,
                      sqlite3_vtab **pp_vt,
                      char **pzErr )
{
    log_vtab_manager *vm = (log_vtab_manager *)pAux;
    int rc = SQLITE_OK;
    vtab* p_vt;

    /* Allocate the sqlite3_vtab/vtab structure itself */
    p_vt = (vtab*)sqlite3_malloc(sizeof(*p_vt));

    if(p_vt == NULL)
    {
        return SQLITE_NOMEM;
    }

    memset(&p_vt->base, 0, sizeof(sqlite3_vtab));
    p_vt->db = db;
    
    /* Declare the vtable's structure */
    p_vt->vi = vm->lookup_impl(argv[3]);
    p_vt->lss = vm->get_source();
    rc = sqlite3_declare_vtab(db, declare_table_statement(p_vt->vi).c_str());

    /* Success. Set *pp_vt and return */
    *pp_vt = &p_vt->base;

    return SQLITE_OK;
}

static int vt_destructor(sqlite3_vtab *p_svt)
{
    vtab *p_vt = (vtab*)p_svt;

    /* Free the SQLite structure */
    sqlite3_free(p_vt);

    return SQLITE_OK;
}

static int vt_connect( sqlite3 *db, void *p_aux,
                       int argc, const char *const*argv,
                       sqlite3_vtab **pp_vt, char **pzErr )
{
    return vt_create(db, p_aux, argc, argv, pp_vt, pzErr);
}

static int vt_disconnect(sqlite3_vtab *pVtab)
{
    return vt_destructor(pVtab);
}

static int vt_destroy(sqlite3_vtab *p_vt)
{
    return vt_destructor(p_vt);
}

static int vt_next(sqlite3_vtab_cursor *cur);

static int vt_open(sqlite3_vtab *p_svt, sqlite3_vtab_cursor **pp_cursor)
{
    vtab* p_vt         = (vtab*)p_svt;
    p_vt->base.zErrMsg = NULL;
    
    vtab_cursor *p_cur = 
        (vtab_cursor*)sqlite3_malloc(sizeof(vtab_cursor));

    *pp_cursor = (sqlite3_vtab_cursor*)p_cur;

    p_cur->base.pVtab = p_svt;
    p_cur->curr_line = vis_line_t(-1);
    vt_next((sqlite3_vtab_cursor *)p_cur);
    
    return (p_cur ? SQLITE_OK : SQLITE_NOMEM);
}

static int vt_close(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *p_cur = (vtab_cursor*)cur;

    /* Free cursor struct. */
    sqlite3_free(p_cur);

    return SQLITE_OK;
}

static int vt_eof(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *vc = (vtab_cursor *)cur;
    vtab *vt = (vtab *)cur->pVtab;

    return vc->curr_line == (int)vt->lss->text_line_count();
}

static int vt_next(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *vc = (vtab_cursor *)cur;
    vtab *vt = (vtab *)cur->pVtab;
    const string &format_name = vt->vi->get_name();
    bool done = false;

    do {
	vc->curr_line = vc->curr_line + vis_line_t(1);

	if (vc->curr_line == (int)vt->lss->text_line_count())
	    break;
	
	content_line_t cl(vt->lss->at(vc->curr_line));
	logfile *lf = vt->lss->find(cl);

	log_format *format = lf->get_format();
	if (format != NULL && format->get_name() == format_name) {
	    done = true;
	}
    }
    while (!done);

    return SQLITE_OK;
}

static int vt_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col)
{
    vtab_cursor *vc = (vtab_cursor *)cur;
    vtab *vt = (vtab *)cur->pVtab;

    content_line_t cl(vt->lss->at(vc->curr_line));
    logfile *lf = vt->lss->find(cl);
    logfile::iterator ll = lf->begin() + cl;

    /* Just return the ordinal of the column requested. */
    switch(col)
    {
    case 0:
	{
	    sqlite3_result_int64( ctx, vc->curr_line );
	}
	break;
    case 1:
        {
	    string &fn = lf->get_filename();
	    
	    sqlite3_result_text( ctx,
				 fn.c_str(),
				 fn.length(),
				 SQLITE_STATIC );
	}
	break;
    case 2:
	{
	    time_t line_time;
	    char buffer[64];

	    line_time = ll->get_time();
	    strftime(buffer, sizeof(buffer),
		     "%F %T",
		     gmtime(&line_time));
	    sqlite3_result_text(ctx, buffer, strlen(buffer), SQLITE_TRANSIENT);
	}
	break;
    case 3:
	if (vc->curr_line == 0) {
	    sqlite3_result_int64( ctx, 0 );
	}
	else {
	    content_line_t prev_cl(vt->lss->at(vis_line_t(vc->curr_line - 1)));
	    logfile *prev_lf = vt->lss->find(prev_cl);
	    logfile::iterator prev_ll = prev_lf->begin() + prev_cl;
	    uint64_t prev_time, curr_line_time;

	    prev_time = prev_ll->get_time() * 1000ULL;
	    prev_time += prev_ll->get_millis();
	    curr_line_time = ll->get_time() * 1000ULL;
	    curr_line_time += ll->get_millis();
	    assert(curr_line_time >= prev_time);
	    sqlite3_result_int64( ctx, curr_line_time - prev_time );
	}
	break;
    case 4:
	{
	    const char *level_name = ll->get_level_name();
	    
	    sqlite3_result_text(ctx,
				level_name,
				strlen(level_name),
				SQLITE_STATIC);
	}
	break;
    case 5:
	{
	    string line;

	    lf->read_line(ll, line);
	    sqlite3_result_text(ctx,
				line.c_str(),
				line.length(),
				SQLITE_TRANSIENT);
	}
	break;
    default:
        {
	    logfile::iterator line_iter;
	    string line, value;
	    
	    line_iter = lf->begin() + cl;
	    lf->read_line(line_iter, line);
	    vt->vi->extract(line, col - 6, ctx);
	}
	break;
    }

    return SQLITE_OK;
}

static int vt_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *p_rowid)
{
    vtab_cursor *p_cur = (vtab_cursor*)cur;

    *p_rowid = p_cur->curr_line;

    return SQLITE_OK;
}

static int vt_filter( sqlite3_vtab_cursor *p_vtc, 
                      int idxNum, const char *idxStr,
                      int argc, sqlite3_value **argv )
{
    return SQLITE_OK;
}

static int vt_best_index(sqlite3_vtab *tab, sqlite3_index_info *p_info)
{
    return SQLITE_OK;
}

static sqlite3_module vtab_module = {
    0,              /* iVersion */
    vt_create,      /* xCreate       - create a vtable */
    vt_connect,     /* xConnect      - associate a vtable with a connection */
    vt_best_index,  /* xBestIndex    - best index */
    vt_disconnect,  /* xDisconnect   - disassociate a vtable with a connection */
    vt_destroy,     /* xDestroy      - destroy a vtable */
    vt_open,        /* xOpen         - open a cursor */
    vt_close,       /* xClose        - close a cursor */
    vt_filter,      /* xFilter       - configure scan constraints */
    vt_next,        /* xNext         - advance a cursor */
    vt_eof,         /* xEof          - inidicate end of result set*/
    vt_column,      /* xColumn       - read data */
    vt_rowid,       /* xRowid        - read data */
    NULL,           /* xUpdate       - write data */
    NULL,           /* xBegin        - begin transaction */
    NULL,           /* xSync         - sync transaction */
    NULL,           /* xCommit       - commit transaction */
    NULL,           /* xRollback     - rollback transaction */
    NULL,           /* xFindFunction - function overloading */
};

log_vtab_manager::log_vtab_manager(sqlite3 *memdb, logfile_sub_source &lss)
    : vm_db(memdb), vm_source(lss)
{
    sqlite3_create_module(this->vm_db, "log_vtab_impl", &vtab_module, this);
}

void log_vtab_manager::register_vtab(log_vtab_impl *vi) {
    if (this->vm_impls.find(vi->get_name()) == this->vm_impls.end()) {
	char *sql;
	int rc;
	
	this->vm_impls[vi->get_name()] = vi;

	sql = sqlite3_mprintf("CREATE VIRTUAL TABLE %s "
			      "USING log_vtab_impl(%s)",
			      vi->get_name().c_str(),
			      vi->get_name().c_str());
	rc = sqlite3_exec(this->vm_db,
			  sql,
			  NULL,
			  NULL,
			  NULL);
	assert(rc == SQLITE_OK);

	sqlite3_free(sql);
    }
}
