/**
 * Copyright (c) 2007-2012, Timothy Stack
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
