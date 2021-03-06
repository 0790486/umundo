/**
 *  @file
 *  @brief      Generates 36 byte UUID strings.
 *  @author     2012 Stefan Radomski (stefan.radomski@cs.tu-darmstadt.de)
 *  @copyright  Simplified BSD
 *
 *  @cond
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
 *  @endcond
 */

#ifndef UUID_H_ASB7D2U4
#define UUID_H_ASB7D2U4

#include "umundo/common/Common.h"
#include <boost/uuid/uuid_generators.hpp>

namespace umundo {

/**
 * UUID Generator for 36 byte UUIDs
 */
class DLLEXPORT UUID {
public:
	static const std::string getUUID();
	static bool isUUID(const std::string& uuid);

private:
	UUID() {}
	static boost::uuids::random_generator randomGen;
};

}

#endif /* end of include guard: UUID_H_ASB7D2U4 */
