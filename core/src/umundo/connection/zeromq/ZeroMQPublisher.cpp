/**
 *  @file
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

#ifdef WIN32
#include <time.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#endif

#include "umundo/connection/zeromq/ZeroMQPublisher.h"

#include "umundo/connection/zeromq/ZeroMQNode.h"
#include "umundo/common/Message.h"
#include "umundo/common/UUID.h"

#include "umundo/config.h"
#if defined UNIX || defined IOS || defined IOSSIM
#include <string.h> // strlen, memcpy
#include <stdio.h> // snprintf
#endif

namespace umundo {

ZeroMQPublisher::ZeroMQPublisher() {}

void ZeroMQPublisher::init(Options* config) {
	ScopeLock lock(_mutex);

	_transport = "tcp";

	(_pubSocket = zmq_socket(ZeroMQNode::getZeroMQContext(), ZMQ_PUB)) || UM_LOG_WARN("zmq_socket: %s",zmq_strerror(errno));

	int hwm = NET_ZEROMQ_SND_HWM;
	std::string pubId("um.pub.intern." + _uuid);

//	zmq_setsockopt(_pubSocket, ZMQ_IDENTITY, pubId.c_str(), pubId.length()) && UM_LOG_WARN("zmq_setsockopt: %s",zmq_strerror(errno));
	zmq_setsockopt(_pubSocket, ZMQ_SNDHWM, &hwm, sizeof(hwm)) && UM_LOG_WARN("zmq_setsockopt: %s",zmq_strerror(errno));
	zmq_bind(_pubSocket, std::string("inproc://" + pubId).c_str());

	UM_LOG_INFO("creating internal publisher for %s on %s", _channelName.c_str(), std::string("inproc://" + pubId).c_str());

}

ZeroMQPublisher::~ZeroMQPublisher() {
	UM_LOG_INFO("deleting publisher for %s", _channelName.c_str());

	zmq_close(_pubSocket);

	// clean up pending messages
	std::map<std::string, std::list<std::pair<uint64_t, Message*> > >::iterator queuedMsgSubIter = _queuedMessages.begin();
	while(queuedMsgSubIter != _queuedMessages.end()) {
		std::list<std::pair<uint64_t, Message*> >::iterator queuedMsgIter = queuedMsgSubIter->second.begin();
		while(queuedMsgIter != queuedMsgSubIter->second.end()) {
			delete (queuedMsgIter->second);
			queuedMsgIter++;
		}
		queuedMsgSubIter++;
	}

}

boost::shared_ptr<Implementation> ZeroMQPublisher::create() {
	return boost::shared_ptr<ZeroMQPublisher>(new ZeroMQPublisher());
}

void ZeroMQPublisher::suspend() {
	ScopeLock lock(_mutex);
	if (_isSuspended)
		return;
	_isSuspended = true;
};

void ZeroMQPublisher::resume() {
	ScopeLock lock(_mutex);
	if (!_isSuspended)
		return;
	_isSuspended = false;
};

int ZeroMQPublisher::waitForSubscribers(int count, int timeoutMs) {
	ScopeLock lock(_mutex);
	uint64_t now = Thread::getTimeStampMs();
	while (unique_keys(_domainSubs) < (unsigned int)count) {
		_pubLock.wait(_mutex, timeoutMs);
		if (timeoutMs > 0 && Thread::getTimeStampMs() - timeoutMs > now)
			break;
	}
	/**
	 * TODO: we get notified when the subscribers uuid occurs, that
	 * might be before the actual channel is subscribed. I posted
	 * to the ZeroMQ ML regarding socket IDs with every subscription
	 * message. Until then, sleep a bit.
	 * Update: I use a late alphabetical order to get the subscriber id last
	 */
//  Thread::sleepMs(20);
	return unique_keys(_domainSubs);
}

void ZeroMQPublisher::added(const SubscriberStub& sub, const NodeStub& node) {
	ScopeLock lock(_mutex);

	_subs[sub.getUUID()] = sub;

	// do we already now about this sub via this node?
	std::pair<_domainSubs_t::iterator, _domainSubs_t::iterator> subIter = _domainSubs.equal_range(sub.getUUID());
	while(subIter.first != subIter.second) {
		if (subIter.first->second.first.getUUID() == node.getUUID())
			return; // we already know about this sub from this node
		subIter.first++;
	}

	_domainSubs.insert(std::make_pair(sub.getUUID(), std::make_pair(node, sub)));

	UM_LOG_INFO("Publisher %s received subscriber %s on node %s for channel %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(sub.getUUID()).c_str(), SHORT_UUID(node.getUUID()).c_str(), _channelName.c_str());

	if (_greeter != NULL && _domainSubs.count(sub.getUUID()) == 1) // only perform greeting for first occurence of subscriber
		_greeter->welcome(Publisher(boost::static_pointer_cast<PublisherImpl>(shared_from_this())), sub);

	if (_queuedMessages.find(sub.getUUID()) != _queuedMessages.end()) {
		UM_LOG_INFO("Subscriber with queued messages joined, sending %d old messages", _queuedMessages[sub.getUUID()].size());
		std::list<std::pair<uint64_t, Message*> >::iterator msgIter = _queuedMessages[sub.getUUID()].begin();
		while(msgIter != _queuedMessages[sub.getUUID()].end()) {
			send(msgIter->second);
			msgIter++;
		}
		_queuedMessages.erase(sub.getUUID());
	}
	UMUNDO_SIGNAL(_pubLock);
}

void ZeroMQPublisher::removed(const SubscriberStub& sub, const NodeStub& node) {
	ScopeLock lock(_mutex);

	// do we now about this sub via this node?
	bool subscriptionFound = false;
	std::pair<_domainSubs_t::iterator, _domainSubs_t::iterator> subIter = _domainSubs.equal_range(sub.getUUID());
	while(subIter.first != subIter.second) {
		if (subIter.first->second.first.getUUID() == node.getUUID()) {
			subscriptionFound = true;
			break;
		}
		subIter.first++;
	}
	if (!subscriptionFound)
		return;

	UM_LOG_INFO("Publisher %s lost subscriber %s on node %s for channel %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(sub.getUUID()).c_str(), SHORT_UUID(node.getUUID()).c_str(), _channelName.c_str());

	if (_domainSubs.count(sub.getUUID()) == 1) { // about to vanish
		if (_greeter != NULL)
			_greeter->farewell(Publisher(boost::static_pointer_cast<PublisherImpl>(shared_from_this())), sub);
		_subs.erase(sub.getUUID());
	}

	_domainSubs.erase(subIter.first);
	UMUNDO_SIGNAL(_pubLock);
}

void ZeroMQPublisher::send(Message* msg) {
	if (_isSuspended) {
		UM_LOG_WARN("Not sending message on suspended publisher");
		return;
	}

	// topic name or explicit subscriber id is first message in envelope
	zmq_msg_t channelEnvlp;
	if (msg->getMeta().find("um.sub") != msg->getMeta().end()) {
		// explicit destination
		if (_domainSubs.count(msg->getMeta("um.sub")) == 0 && !msg->isQueued()) {
			UM_LOG_INFO("Subscriber %s is not (yet) connected on %s - queuing message", msg->getMeta("um.sub").c_str(), _channelName.c_str());
			Message* queuedMsg = new Message(*msg); // copy message
			queuedMsg->setQueued(true);
			_queuedMessages[msg->getMeta("um.sub")].push_back(std::make_pair(Thread::getTimeStampMs(), queuedMsg));
			return;
		}
		ZMQ_PREPARE_STRING(channelEnvlp, std::string("~" + msg->getMeta("um.sub")).c_str(), msg->getMeta("um.sub").size() + 1);
	} else {
		// everyone on channel
		ZMQ_PREPARE_STRING(channelEnvlp, _channelName.c_str(), _channelName.size());
	}

	// mandatory meta fields
	msg->putMeta("um.pub", _uuid);
	msg->putMeta("um.proc", procUUID);
	msg->putMeta("um.host", hostUUID);

	std::map<std::string, std::string>::const_iterator metaIter = _mandatoryMeta.begin();
	while(metaIter != _mandatoryMeta.end()) {
		if (metaIter->second.length() > 0)
			msg->putMeta(metaIter->first, metaIter->second);
		metaIter++;
	}

	zmq_sendmsg(_pubSocket, &channelEnvlp, ZMQ_SNDMORE) >= 0 || UM_LOG_WARN("zmq_sendmsg: %s", zmq_strerror(errno));
	zmq_msg_close(&channelEnvlp) && UM_LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));

	// all our meta information
	for (metaIter = msg->getMeta().begin(); metaIter != msg->getMeta().end(); metaIter++) {
		// string key(metaIter->first);
		// string value(metaIter->second);
		// std::cout << key << ": " << value << std::endl;

		// string length of key + value + two null bytes as string delimiters
		size_t metaSize = (metaIter->first).length() + (metaIter->second).length() + 2;
		zmq_msg_t metaMsg;
		ZMQ_PREPARE(metaMsg, metaSize);

		char* writePtr = (char*)zmq_msg_data(&metaMsg);

		memcpy(writePtr, (metaIter->first).data(), (metaIter->first).length());
		// indexes start at zero, so length is the byte after the string
		((char*)zmq_msg_data(&metaMsg))[(metaIter->first).length()] = '\0';
		assert(strlen((char*)zmq_msg_data(&metaMsg)) == (metaIter->first).length());
		assert(strlen(writePtr) == (metaIter->first).length()); // just to be sure

		// increment write pointer
		writePtr += (metaIter->first).length() + 1;

		memcpy(writePtr,
		       (metaIter->second).data(),
		       (metaIter->second).length());
		// first string + null byte + second string
		((char*)zmq_msg_data(&metaMsg))[(metaIter->first).length() + 1 + (metaIter->second).length()] = '\0';
		assert(strlen(writePtr) == (metaIter->second).length());

		zmq_sendmsg(_pubSocket, &metaMsg, ZMQ_SNDMORE) >= 0 || UM_LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno));
		zmq_msg_close(&metaMsg) && UM_LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));
	}

	// data as the second part of a multipart message
	zmq_msg_t publication;
	ZMQ_PREPARE_DATA(publication, msg->data(), msg->size());
	zmq_sendmsg(_pubSocket, &publication, 0) >= 0 || UM_LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno));
	zmq_msg_close(&publication) && UM_LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));
}


}