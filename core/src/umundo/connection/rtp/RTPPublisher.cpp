/**
 *  @file
 *  @author     2013 Thilo Molitor (thilo@eightysoft.de)
 *  @author     2013 Stefan Radomski (stefan.radomski@cs.tu-darmstadt.de)
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

#include "umundo/connection/rtp/RTPPublisher.h"

namespace umundo {

RTPPublisher::RTPPublisher() {}

void RTPPublisher::init(Options* config) {
}

RTPPublisher::~RTPPublisher() {
}

boost::shared_ptr<Implementation> RTPPublisher::create() {
	return boost::shared_ptr<RTPPublisher>(new RTPPublisher());
}

void RTPPublisher::suspend() {
};

void RTPPublisher::resume() {
};

int RTPPublisher::waitForSubscribers(int count, int timeoutMs) {
	return 0;
}

void RTPPublisher::added(const SubscriberStub& sub, const NodeStub& node) {
	UM_LOG_DEBUG("%s: received a new subscriber", SHORT_UUID(_uuid).c_str());
}

void RTPPublisher::removed(const SubscriberStub& sub, const NodeStub& node) {
	UM_LOG_DEBUG("%s: lost a subscriber", SHORT_UUID(_uuid).c_str());
}

void RTPPublisher::send(Message* msg) {
}

}
