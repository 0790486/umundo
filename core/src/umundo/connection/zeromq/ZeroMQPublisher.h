/**
 *  @file
 *  @brief      Publisher implementation with 0MQ.
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

#ifndef ZEROMQPUBLISHER_H_AX5HLY5Q
#define ZEROMQPUBLISHER_H_AX5HLY5Q

#include <boost/enable_shared_from_this.hpp>

#include "umundo/common/Common.h"
#include "umundo/connection/Publisher.h"
#include "umundo/thread/Thread.h"

#include <list>

namespace umundo {

class ZeroMQNode;

/**
 * Concrete publisher implementor for 0MQ (bridge pattern).
 */
class DLLEXPORT ZeroMQPublisher : public PublisherImpl, public boost::enable_shared_from_this<ZeroMQPublisher>  {
public:
	virtual ~ZeroMQPublisher();

	boost::shared_ptr<Implementation> create();
	void init(Options*);
	void suspend();
	void resume();

	void send(Message* msg);
	int waitForSubscribers(int count, int timeoutMs);

protected:
	/**
	 * Constructor used for prototype in Factory only.
	 */
	ZeroMQPublisher();

	void added(const SubscriberStub& sub, const NodeStub& node);
	void removed(const SubscriberStub& sub, const NodeStub& node);

private:
	void run();

	void* _pubSocket;
	boost::shared_ptr<PublisherConfig> _config;
	std::multimap<std::string, std::pair<NodeStub, SubscriberStub> > _domainSubs;
	typedef std::multimap<std::string, std::pair<NodeStub, SubscriberStub> > _domainSubs_t;

	/// messages for subscribers we do not know yet
	std::map<std::string, std::list<std::pair<uint64_t, umundo::Message*> > > _queuedMessages;

	Monitor _pubLock;
	Mutex _mutex;

	friend class Factory;
};

}

#endif /* end of include guard: ZEROMQPUBLISHER_H_AX5HLY5Q */
