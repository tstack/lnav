#ifndef CAPABILITIES_H
#define CAPABILITIES_H

// These are aligned with the terminfo binary order
extern const char *terminfo_bool_names[];
extern const char *terminfo_num_names[];
extern const char *terminfo_str_names[];

extern const int terminfo_bool_count;
extern const int terminfo_num_count;
extern const int terminfo_str_count;

int terminfo_find_bool_index(const char *name);
int terminfo_find_num_index(const char *name);
int terminfo_find_str_index(const char *name);

#endif
