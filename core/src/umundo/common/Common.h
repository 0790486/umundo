/**
 *  Copyright (C) 2012  Stefan Radomski (stefan.radomski@cs.tu-darmstadt.de)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the FreeBSD license as published by the FreeBSD
 *  project.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 *  You should have received a copy of the FreeBSD license along with this
 *  program. If not, see <http://www.opensource.org/licenses/bsd-license>.
 */

#ifndef COMMON_H_ANPQOWX0
#define COMMON_H_ANPQOWX0

// disable: "<type> needs to have dll-interface to be used by clients'
// Happens on STL member variables which are not public therefore is ok?
//#pragma warning (disable : 4251)

#if defined(_MSC_VER)
// disable signed / unsigned comparison warnings
#pragma warning (disable : 4018)
// possible loss of data
#pragma warning (disable : 4244)
#pragma warning (disable : 4267)
// 'this' : used in base member initializer list (TypedSubscriber)
#pragma warning (disable : 4355)

#endif

#include <map>
#include <set>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <stdlib.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(UMUNDO_STATIC)
#	ifdef COMPILING_DLL
#		define DLLEXPORT __declspec(dllexport)
#	else
#		define DLLEXPORT __declspec(dllimport)
#	endif
#else
#	define DLLEXPORT
#endif

// #if defined UNIX || defined IOS || defined IOSSIM
// #include <string.h> // memcpy
// #include <stdio.h> // snprintf
// #endif

#include "portability.h"
#include "umundo/common/Debug.h"

namespace umundo {

using std::string;
using std::map;
using std::set;
using std::vector;
using boost::shared_ptr;
using boost::weak_ptr;

extern string procUUID;

// see http://stackoverflow.com/questions/228005/alternative-to-itoa-for-converting-integer-to-string-c
template <typename T> std::string toStr(T tmp)
{
    std::ostringstream out;
    out << tmp;
    return out.str();
}

template <typename T> T strTo(std::string tmp)
{
    T output;
    std::istringstream in(tmp);
    in >> output;
    return output;
}

}

#endif /* end of include guard: COMMON_H_ANPQOWX0 */

/**
 * \mainpage umundo-core
 *
 * This is the documentation of umundo-core, a leight-weight implementation of a pub/sub system. Its only responsibility
 * is to deliver byte-arrays from publishers to subscribers on channels. Where a channel is nothing more than an agreed
 * upon ASCII string.
 */
