
#ifndef __tables_h
#define __tables_h

#define __table_str( x ) #x
#define __table_section( table, idx ) \
	__section__ ( ".tbl." __table_str ( table ) "." __table_str ( idx ) )

#define __table_section_start( table ) __table_section ( table, 00 )
#define __table_section_end( table ) __table_section ( table, 99 )

#define __natural_alignment( type ) __aligned__ ( __alignof__ ( type ) )

/**
 * Linker table entry.
 *
 * Declares a data structure to be part of a linker table.  Use as
 * e.g.
 *
 * @code
 *
 *   struct my_foo __table ( foo, 01 ) = {
 *      ...
 *   };
 *
 * @endcode
 *
 */
#define __table( type, table, idx )				\
	__attribute__ (( __table_section ( table, idx ),	\
			 __natural_alignment ( type ) ))

/**
 * Linker table start marker.
 *
 * Declares a data structure (usually an empty data structure) to be
 * the start of a linker table.  Use as e.g.
 *
 * @code
 *
 *   static struct foo_start[0] __table_start ( foo );
 *
 * @endcode
 *
 */
#define __table_start( type, table ) __table ( type, table, 00 )

/**
 * Linker table end marker.
 *
 * Declares a data structure (usually an empty data structure) to be
 * the end of a linker table.  Use as e.g.
 *
 * @code
 *
 *   static struct foo_end[0] __table_end ( foo );
 *
 * @endcode
 *
 */
#define __table_end( type, table ) __table ( type, table, 99 )

#endif
