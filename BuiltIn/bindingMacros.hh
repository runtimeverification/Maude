/*

    This file is part of the Maude 2 interpreter.

    Copyright 1997-2003 SRI International, Menlo Park, CA 94025, USA.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.

*/

//
//      Macros for binding stuff in builtins.
//
#ifndef _bindingMacros_hh_
#define _bindingMacros_hh_

//
//	This needs to be a macro in order to produce constant expressions.
//
#define CODE(c1, c2)	((c1) + ((c2) << 8))

#define BIND_OP(purpose, className, op, data) \
  if (strcmp(purpose, #className) == 0) \
    { \
      if (data.length() == 1) \
	{ \
	  const char* opName = (data)[0]; \
	  if (opName[0] != '\0') \
	    { \
	      int t = CODE(opName[0], opName[1]); \
	      if (op == NONE) \
		{ \
		  op = t; \
		  return true; \
		} \
	      if (op == t) \
		return true; \
	    } \
	} \
      return false; \
    }

#define NULL_DATA(purpose, className, data) \
  if (strcmp(purpose, #className) == 0) \
    { \
      return data.length() == 0; \
    } 

#define BIND_SYMBOL(purpose, symbol, name, type) \
  if (strcmp(purpose, #name) == 0) \
    { \
      if (name != 0) \
	return name == symbol; \
      name = dynamic_cast<type>(symbol); \
      return name != 0; \
    }

#define BIND_TERM(purpose, term, name) \
  if (strcmp(purpose, #name) == 0) \
    { \
      bool r = true; \
      if (Term* t = name.getTerm()) \
	{ \
	  r = term->equal(t); \
	  term->deepSelfDestruct(); \
	} \
      else \
	name.setTerm(term); \
      return r; \
    }

#define PREPARE_TERM(name) \
  if (name.getTerm() != 0) \
    { \
      (void) name.normalize(); \
      name.prepare(); \
    }

#define COPY_SYMBOL(original, name, mapping, type) \
  if (name == 0) \
    { \
      if (type s = original->name) \
	name = (mapping == 0) ? s : safeCast(type, mapping->translate(s)); \
    }

#define COPY_TERM(original, name, mapping) \
  if (name.getTerm() == 0) \
    { \
      if (Term* t = original->name.getTerm()) \
	name.setTerm(t->deepCopy(mapping)); \
    }

#endif
