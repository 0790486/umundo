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

#include "umundo/connection/zeromq/ZeroMQNode.h"

#include "umundo/config.h"

#if defined UNIX || defined IOS || defined IOSSIM
#include <arpa/inet.h> // htons
#include <string.h> // strlen, memcpy
#include <stdio.h> // snprintf
#endif

#include <boost/lexical_cast.hpp>

#include "umundo/common/Message.h"
#include "umundo/common/Regex.h"
#include "umundo/discovery/Discovery.h"
#include "umundo/discovery/NodeQuery.h"
#include "umundo/connection/zeromq/ZeroMQPublisher.h"
#include "umundo/connection/zeromq/ZeroMQSubscriber.h"

namespace umundo {

ZeroMQNode::~ZeroMQNode() {
	Discovery::unbrowse(_nodeQuery);
  Discovery::remove(this);
	stop();

	// close connection to all remote publishers
	map<string, void*>::iterator sockIter;
	for (sockIter = _sockets.begin(); sockIter != _sockets.end(); sockIter++) {
		while(zmq_close(sockIter->second) != 0) {
			LOG_WARN("zmq_close: %s - retrying", zmq_strerror(errno));
			Thread::sleepMs(50);
		}
	}

	join();

	// close node socket
	while(zmq_close(_responder) != 0) {
		LOG_WARN("zmq_close: %s - retrying", zmq_strerror(errno));
		Thread::sleepMs(50);
	}
}

/**
 * Return the global ZeroMQ context.
 */
void* ZeroMQNode::getZeroMQContext() {
	if (_zmqContext == NULL) {
		(_zmqContext = zmq_init(1)) || LOG_ERR("zmq_init: %s",zmq_strerror(errno));
	}
	return _zmqContext;
}
void* ZeroMQNode::_zmqContext = NULL;

ZeroMQNode::ZeroMQNode() {
}

shared_ptr<Implementation> ZeroMQNode::create(void*) {
	return shared_ptr<ZeroMQNode>(new ZeroMQNode());
}

void ZeroMQNode::destroy() {
	delete(this);
}

uint16_t ZeroMQNode::bindToFreePort(void* socket, const string& transport, const string& address) {
	std::stringstream ss;
	int port = 4242;

	ss << transport << "://" << address << ":" << port;

	while(zmq_bind(socket, ss.str().c_str()) == -1) {
		switch(errno) {
		case EADDRINUSE:
			port++;
			ss.clear();        // clear error bits
			ss.str(string());  // reset string
			ss << transport << "://" << address << ":" << port;
			break;
		default:
			LOG_WARN("zmq_bind at %s: %s", ss.str().c_str(), zmq_strerror(errno));
			Thread::sleepMs(100);
		}
	}

	return port;
}


#ifdef PUBPORT_SHARING
void* ZeroMQNode::_sharedPubSocket = NULL;
void* ZeroMQNode::_sharedSubSocket = NULL;
ZeroMQNode::ZeroMQForwarder* ZeroMQNode::_forwarder = NULL;
uint16_t ZeroMQNode::_sharedPubPort = 0;

#endif

/**
 * Initialize this instance.
 *
 * Be aware that this method is also called when resuming from suspension.
 */
void ZeroMQNode::init(shared_ptr<Configuration> config) {
	_config = boost::static_pointer_cast<NodeConfig>(config);
	_transport = "tcp";
#ifdef PUBPORT_SHARING
	if(_forwarder == NULL) {
		(_sharedPubSocket = zmq_socket(getZeroMQContext(), ZMQ_PUB))  || LOG_ERR("zmq_socket: %s", zmq_strerror(errno));
		(_sharedSubSocket = zmq_socket(getZeroMQContext(), ZMQ_SUB))  || LOG_ERR("zmq_socket: %s", zmq_strerror(errno));

		_forwarder = new ZeroMQForwarder(_sharedSubSocket, _sharedPubSocket);
		_sharedPubPort = bindToFreePort(_sharedPubSocket, _transport, "*");
		_forwarder->start();
	}
#endif

	(_responder = zmq_socket(getZeroMQContext(), ZMQ_ROUTER))  || LOG_ERR("zmq_socket: %s", zmq_strerror(errno));
	int rcvTimeOut = 20;
	zmq_setsockopt(_responder, ZMQ_RCVTIMEO, &rcvTimeOut, sizeof(rcvTimeOut));
	_port = bindToFreePort(_responder, _transport, "*");
	LOG_INFO("Node %s listening on port %d", SHORT_UUID(_uuid).c_str(), _port);

	start();

	// register nodestub query at discovery
	_nodeQuery = shared_ptr<NodeQuery>(new NodeQuery(_domain, this));
  Discovery::add(this);
	Discovery::browse(_nodeQuery);
}

/**
 * Disconnect all subscribers, publishers and remove node query at discovery.
 *
 * This will suspend the node by removing our publishers and remembering them as
 * _suspendedLocalPubs. Furthermore, we simulate the removal of all remote nodes
 * and rely on our reinitialized query later-on to add them again.
 */
void ZeroMQNode::suspend() {
	UMUNDO_LOCK(_mutex);
	if (_isSuspended) {
		UMUNDO_UNLOCK(_mutex);
		return;
	}
	_isSuspended = true;

	// stop browsing for remote nodes
	Discovery::unbrowse(_nodeQuery);
  Discovery::remove(this);
  
	// remove all local publishers
	_suspendedLocalPubs = _pubs;
	map<string, Publisher>::iterator localPubIter = _pubs.begin();
	while(localPubIter != _pubs.end()) {
		localPubIter->second.suspend();
		removePublisher(localPubIter->second);
		localPubIter++;
	}
	assert(_pubs.size() == 0);

	// close connections to all nodes
	map<string, NodeStub>::iterator nodeIter = _nodes.begin();
	while(nodeIter != _nodes.end()) {
		removed(nodeIter->second);
		nodeIter++;
	}

	stop();
	join();

	// close the node socket
	zmq_close(_responder) || LOG_WARN("zmq_close: %s", zmq_strerror(errno));

	UMUNDO_UNLOCK(_mutex);
}

/**
 * Reconnect subscribers, publishers and re-establish node query at discovery.
 */
void ZeroMQNode::resume() {
	UMUNDO_LOCK(_mutex);
	if (!_isSuspended) {
		UMUNDO_UNLOCK(_mutex);
		return;
	}
	_isSuspended = false;

	// add all local publishers again
	map<string, Publisher>::iterator localPubIter = _suspendedLocalPubs.begin();
	while(localPubIter != _suspendedLocalPubs.end()) {
		localPubIter->second.resume();
		addPublisher(localPubIter->second);
		localPubIter++;
	}
	_suspendedLocalPubs.clear();

	// make sure all subscribers are initialized
	map<string, Subscriber>::iterator localSubIter = _subs.begin();
	while(localSubIter != _subs.end()) {
		localSubIter->second.resume();
		localSubIter++;
	}

	// reinitialize this node
	init(_config);

	UMUNDO_UNLOCK(_mutex);

	// now we rely on zeroconf to tell us about remote nodes
}

/**
 * Read from node socket and dispatch message type.
 */
void ZeroMQNode::run() {
	char* remoteId = NULL;
	int32_t more;
	size_t more_size = sizeof(more);

	while(isStarted()) {
		// read a whole envelope
		while (1) {
			zmq_msg_t message;
			zmq_msg_init(&message) && LOG_WARN("zmq_msg_init: %s",zmq_strerror(errno));

			// receive timeout is set via setsockopt - keep in mind that we do not hold a mutex
			while (zmq_recvmsg(_responder, &message, 0) < 0) {
				if (!isStarted()) {
					zmq_msg_close(&message) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));
					return;
				}
				if (errno != EINTR && errno != EAGAIN)
					LOG_WARN("zmq_recvmsg: %s",zmq_strerror(errno));

        // process publisher loops
        std::map<std::string, Publisher>::iterator pubIter = _pubs.begin();
        while(pubIter != _pubs.end()) {
          boost::static_pointer_cast<ZeroMQPublisher>(pubIter->second.getImpl())->runOnce();
          pubIter++;
        }
			}

			int msgSize = zmq_msg_size(&message);

			if (msgSize == 36 && remoteId == NULL) {
				// remote id from envelope
				remoteId = (char*)malloc(37);
				memcpy(remoteId, zmq_msg_data(&message), 36);
				remoteId[36] = 0;

			} else if (msgSize >= 2 && remoteId != NULL) {
				// every envelope starts with the remote uuid we read above
				assert(strlen(remoteId) == 36);

				// first two bytes are type of message
				uint16_t type = ntohs(*(short*)(zmq_msg_data(&message)));
				LOG_DEBUG("%s received message type %s with %d bytes from %s", SHORT_UUID(_uuid).c_str(), Message::typeToString(type), msgSize, SHORT_UUID(string(remoteId)).c_str());

				UMUNDO_LOCK(_mutex);

				/**
				 * We may receive data and other messages from nodes that are about to vanish
				 * only accept PUB_ADDED messages for unknown nodes.
				 */
				if (type != Message::PUB_ADDED && _nodes.find(remoteId) == _nodes.end()) {
					zmq_msg_close(&message) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));
					string remoteIdStr(remoteId);
					LOG_WARN("Ignoring %s message from unconnected node %s", Message::typeToString(type), SHORT_UUID(remoteIdStr).c_str());
					UMUNDO_UNLOCK(_mutex);
					continue;
				}

				// dispatch message type
				switch (type) {
				case Message::DATA:
					break;
				case Message::PUB_ADDED:
					processPubAdded(remoteId, message);
					break;
				case Message::PUB_REMOVED:
					processPubRemoved(remoteId, message);
					break;
				case Message::SUBSCRIBE:
					processSubscription(remoteId, message, true);
					break;
				case Message::UNSUBSCRIBE:
					processSubscription(remoteId, message, false);
					break;
				default:
					LOG_WARN("Received unknown message type");
				}
			} else {
				LOG_WARN("Received message without remote id in header");
			}

			UMUNDO_UNLOCK(_mutex);

			// are there more messages in the envelope?
			zmq_getsockopt(_responder, ZMQ_RCVMORE, &more, &more_size) && LOG_WARN("zmq_getsockopt: %s",zmq_strerror(errno));
			zmq_msg_close(&message) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));

			if (!more) {
				free(remoteId);
				remoteId = NULL;
				break; // last message part
			}
		}
	}
}

/**
 * A remote subscriber either subscribed or unsubscribed from one of our publishers.
 */
void ZeroMQNode::processSubscription(const char* remoteId, zmq_msg_t message, bool subscribe) {
	ScopeLock lock(_mutex);

	int msgSize = zmq_msg_size(&message);
	char* buffer = (char*)zmq_msg_data(&message);
	uint16_t port = 0;
	char* channel;
	char* pubId;
	char* subId;
	buffer = readPubInfo(buffer + 2, port, channel, pubId);
	buffer = readSubInfo(buffer, subId);

	if (buffer - (char*)zmq_msg_data(&message) != msgSize) {
		if (subscribe) {
			LOG_ERR("Malformed SUBSCRIBE message received - ignoring");
		} else {
			LOG_ERR("Malformed UNSUBSCRIBE message received - ignoring");
		}
		assert(validateState());
		return;
	}

	if (_pubs.find(pubId) == _pubs.end()) {
		if (subscribe) {
			LOG_WARN("Subscribing to unkown publisher");
		} else {
			LOG_WARN("Unsubscribing from unkown publisher");
		}
		assert(validateState());
		return;
	}

	if (subscribe) {
		// remember remote node subscription to local pub
		if (_subscriptions[remoteId][pubId].find(subId) == _subscriptions[remoteId][pubId].end()) {
			_subscriptions[remoteId][pubId].insert(subId);
			_pubs[pubId].addedSubscriber(remoteId, subId);
		}
	} else {
		// remove subscription on remote node for publisher
		if (
		    _subscriptions.find(remoteId) != _subscriptions.end() &&
		    _subscriptions[remoteId].find(pubId) != _subscriptions[remoteId].end() &&
		    _subscriptions[remoteId][pubId].find(subId) != _subscriptions[remoteId][pubId].end()
		) {
			// we knew about this subscription
			_pubs[pubId].removedSubscriber(remoteId, subId);
			_subscriptions[remoteId][pubId].erase(subId);
			if (_subscriptions[remoteId][pubId].size() == 0) {
				// this was the last subscription from the remote node to the publisher
				_subscriptions[remoteId].erase(pubId);
			}
			if (_subscriptions[remoteId].size() == 0) {
				// this was the last subscription of the remote node
				_subscriptions.erase(remoteId);
			}
		} else {
			LOG_DEBUG("Received UNSUBSCRIBE for unkown subscription");
		}
	}
	assert(validateState());
}

/**
 * We received a publisher from a remote node
 */
void ZeroMQNode::processPubAdded(const char* remoteId, zmq_msg_t message) {
	UMUNDO_LOCK(_mutex);

	int msgSize = zmq_msg_size(&message);
	char* buffer = (char*)zmq_msg_data(&message);
	uint16_t port = 0;
	char* channel;
	char* pubId;
	buffer = readPubInfo(buffer + 2, port, channel, pubId);
	if (buffer - (char*)zmq_msg_data(&message) != msgSize) {
		LOG_ERR("Malformed PUB_ADDED received - ignoring");
		assert(validateState());
		UMUNDO_UNLOCK(_mutex);
		return;
	}
	LOG_INFO("received publisher %s from %s", channel, SHORT_UUID(string(remoteId)).c_str());

  boost::shared_ptr<PublisherStubImpl> pubStub = boost::shared_ptr<PublisherStubImpl>(new PublisherStubImpl());
	pubStub->setChannelName(channel);
	pubStub->setPort(port);
	pubStub->setUUID(pubId);
	addRemotePubToLocalSubs(remoteId, pubStub);
	assert(validateState());
	UMUNDO_UNLOCK(_mutex);

}

void ZeroMQNode::processPubRemoved(const char* remoteId, zmq_msg_t message) {
	UMUNDO_LOCK(_mutex);

	int msgSize = zmq_msg_size(&message);
	char* buffer = (char*)zmq_msg_data(&message);
	uint16_t port = 0;
	char* channel;
	char* pubId;
	buffer = readPubInfo(buffer + 2, port, channel, pubId);

	if (buffer - (char*)zmq_msg_data(&message) != msgSize) {
		LOG_ERR("Malformed PUB_REMOVED received - ignoring");
		assert(validateState());
		UMUNDO_UNLOCK(_mutex);
		return;
	}

	if (_nodes.find(remoteId) != _nodes.end() && _nodes[remoteId].getPublisher(pubId)) {
		PublisherStub pubStub = _nodes[remoteId].getPublisher(pubId);
		removeRemotePubFromLocalSubs(remoteId, pubStub);
		_nodes[remoteId].removePublisher(pubStub);

	}
	assert(validateState());
	UMUNDO_UNLOCK(_mutex);
}

/**
 * Notify a remote node that we will unsubscribe from a publisher.
 */
void ZeroMQNode::notifyOfUnsubscription(void* socket, const Subscriber& zSub, const PublisherStub& zPub) {
	zmq_msg_t msg;
	ZMQ_PREPARE(msg,
	            2 +                                      // uint16_t message type
	            PUB_INFO_SIZE(zPub) +                    // publisher info
	            zSub.getUUID().length() +               // uuid of subscriber
	            1                                        // \0 string terminator
	           );

	char* buffer = (char*)zmq_msg_data(&msg);
	*(uint16_t*)(buffer) = htons(Message::UNSUBSCRIBE);
	buffer = writePubInfo(buffer + 2, zPub);
	buffer = writeSubInfo(buffer, zSub);
	assert((size_t)(buffer - (char*)zmq_msg_data(&msg)) == zmq_msg_size(&msg));

	zmq_sendmsg(socket, &msg, 0) >= 0 || LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno));
	zmq_msg_close(&msg) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));
}

/**
 * Notify a remote node that we will subscribe to a publisher.
 */
void ZeroMQNode::notifyOfSubscription(void* socket, const Subscriber& zSub, const PublisherStub& zPub) {
	zmq_msg_t msg;
	ZMQ_PREPARE(msg,
	            2 +                                      // uint16_t message type
	            PUB_INFO_SIZE(zPub) +                    // publisher info
	            zSub.getUUID().length() +               // uuid of subscriber
	            1                                        // \0 string terminator
	           );

	char* buffer = (char*)zmq_msg_data(&msg);
	*(uint16_t*)(buffer) = htons(Message::SUBSCRIBE);
	buffer = writePubInfo(buffer + 2, zPub);
	buffer = writeSubInfo(buffer, zSub);
	assert((size_t)(buffer - (char*)zmq_msg_data(&msg)) == zmq_msg_size(&msg));

	zmq_sendmsg(socket, &msg, 0) >= 0 || LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno));
	zmq_msg_close(&msg) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));
}

/**
\msc
  "Remote ZeroMQNode",Discovery,ZeroMQNode;
  Discovery->"Remote ZeroMQNode" [label="discovered"];
	Discovery->ZeroMQNode [label="added(NodeStub)", URL="\ref NodeStub"];
	ZeroMQNode->"Remote ZeroMQNode" [label="establish connection on node socket"];
	ZeroMQNode->"Remote ZeroMQNode" [label="List of all local publishers", URL="\ref Publisher" ];
\endmsc
 *
 * This will cause processPubAdded() to be called for every Publisher we list.<br />
 * Keep in mind that the remote node will do the same!
 */
void ZeroMQNode::added(NodeStub node) {
	assert(node);
	assert(node.getUUID().length() == 36);
	if (node.getUUID().compare(_uuid) != 0) { // this is not us!
		LOG_INFO("%s added %s at %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(node.getUUID()).c_str(), node.getIP().c_str());

		// we found ourselves a remote node, lets get some privacy
		ScopeLock lock(_mutex);

		std::stringstream ss;
		ss << node.getTransport() << "://" << node.getIP() << ":" << node.getPort();

		// connect a socket to the remote node port
		void* client;
		(client = zmq_socket(getZeroMQContext(), ZMQ_DEALER)) || LOG_ERR("zmq_socket: %s",zmq_strerror(errno));

		// give the socket an id for zeroMQ routing
		zmq_setsockopt(client, ZMQ_IDENTITY, _uuid.c_str(), _uuid.length()) && LOG_WARN("zmq_setsockopt: %s",zmq_strerror(errno));
		zmq_connect(client, ss.str().c_str()) && LOG_WARN("zmq_connect %s: %s", ss.str().c_str(), zmq_strerror(errno));

		// remember node stub and socket
		_nodes[node.getUUID()] = node;
		_sockets[node.getUUID()] = client;

		// send all our local publisher channelnames as "short PUB_ADDED:string CHANNELNAME:short PORT:\0"
		map<string, Publisher>::iterator pubIter;
		int hasMore = _pubs.size() - 1;
		for (pubIter = _pubs.begin(); pubIter != _pubs.end(); pubIter++, hasMore--) {

			// create a publisher added message from current publisher
			zmq_msg_t msg;
			ZMQ_PREPARE(msg, PUB_INFO_SIZE(pubIter->second) + 2);

			char* buffer = (char*)zmq_msg_data(&msg);
			*(uint16_t*)(buffer) = htons(Message::PUB_ADDED);
			char* end = writePubInfo(buffer + 2, pubIter->second);
			(void)end;
			assert(end - buffer == (int)(PUB_INFO_SIZE(pubIter->second) + 2));

			if (hasMore) {
				zmq_sendmsg(client, &msg, ZMQ_SNDMORE) >= 0 || LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno));
			} else {
				zmq_sendmsg(client, &msg, 0) >= 0 || LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno));
				LOG_INFO("sending %d publishers to newcomer", _pubs.size());
			}
			zmq_msg_close(&msg) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));
		}
		addRemotePubToLocalSubs(node.getUUID().c_str(), PublisherStub());
		assert(validateState());
	}
}

void ZeroMQNode::removed(NodeStub node) {
	assert(node);
	if (node.getUUID().compare(_uuid) != 0) { // this is us?
		std::stringstream nodeDesc;
		nodeDesc << node;
		LOG_INFO("%s removed %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(node.getUUID()).c_str());

		UMUNDO_LOCK(_mutex);

		// unregister local subscribers from remote nodes publishers
		if (_nodes.find(node.getUUID()) != _nodes.end()) {
      map<std::string, PublisherStub> pubs = _nodes[node.getUUID()].getPublishers();
			map<std::string, PublisherStub>::iterator pubIter = pubs.begin();
			// iterate all remote publishers
			while (pubIter != pubs.end()) {
				removeRemotePubFromLocalSubs(node.getUUID().c_str(), pubIter->second);
				pubIter++;
			}
		}

		if (_subscriptions.find(node.getUUID()) != _subscriptions.end()) {
			// A remote node was removed, disconnect all its subscriptions
			map<string, set<string> >::iterator localPubIter = _subscriptions[node.getUUID()].begin();
			while (localPubIter != _subscriptions[node.getUUID()].end()) {
				if (_pubs.find(localPubIter->first) != _pubs.end()) {
					set<string>::iterator subIter = localPubIter->second.begin();
					while(subIter != localPubIter->second.end()) {
						_pubs[localPubIter->first].removedSubscriber(node.getUUID(), *subIter);
						subIter++;
					}
				} else {
					LOG_DEBUG("removed: node was subscribed to non-existent publisher");
				}
				localPubIter++;
			}
		}

		if (_sockets.find(node.getUUID()) == _sockets.end()) {
			LOG_WARN("removed: client was never added: %s", SHORT_UUID(node.getUUID()).c_str());
			assert(validateState());
			UMUNDO_UNLOCK(_mutex);
			return;
		}

		zmq_close(_sockets[node.getUUID()]) && LOG_WARN("zmq_close: %s",zmq_strerror(errno));

		_sockets.erase(node.getUUID());               // delete socket
		_subscriptions.erase(node.getUUID());            // remove all references to remote nodes subs
		_nodes.erase(node.getUUID());                 // remove node itself
		_pendingRemotePubs.erase(node.getUUID());   // I don't know whether this is needed, but it cant be wrong

//		assert(_sockets.size() == _remotePubs.size());
		assert(_sockets.size() == _nodes.size());
		assert(validateState());
		UMUNDO_UNLOCK(_mutex);

	}
}

void ZeroMQNode::changed(NodeStub node) {
	if (node.getUUID().compare(_uuid) != 0) {
//		LOG_INFO("%s changed %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(node->getUUID()).c_str());
	}
}

void ZeroMQNode::addRemotePubToLocalSubs(const char* remoteId, const PublisherStub& pub) {
	UMUNDO_LOCK(_mutex);

	// ZeroMQNode::added will call us without a publisher
	if (pub)
		_pendingRemotePubs[remoteId][pub.getUUID()] = pub;

	if (_nodes.find(remoteId) != _nodes.end() && _pendingRemotePubs.find(remoteId) != _pendingRemotePubs.end()) {
		void* nodeSocket = _sockets[remoteId];
		assert(nodeSocket != NULL);
		// we have discovered the node already and are ready to go
		map<string, PublisherStub>::iterator pubIter;
		for (pubIter = _pendingRemotePubs[remoteId].begin(); pubIter != _pendingRemotePubs[remoteId].end(); pubIter++) {
			// copy pubStub from pendingPubs to remotePubs
			_nodes[remoteId].addPublisher(pubIter->second);

			// set publisherstub endpoint data from node
			// publishers are not bound to a node as they can be added to multiple nodes
			pubIter->second.getImpl()->setHost(_nodes[remoteId].getHost());
			pubIter->second.getImpl()->setDomain(_nodes[remoteId].getDomain());
			pubIter->second.getImpl()->setTransport(_nodes[remoteId].getTransport());
			pubIter->second.getImpl()->setIP(_nodes[remoteId].getIP());

			// is this a publisher from this process?
			if (_nodes[remoteId].isInProcess()) {
				pubIter->second.getImpl()->setInProcess(true);
			}

			// is this a publisher from a local node?
			if (!_nodes[remoteId].isRemote()) {
				pubIter->second.getImpl()->setRemote(false);
			}

			// see if we have a local subscriber interested in the publisher's channel
			map<string, Subscriber>::iterator subIter;
			for (subIter = _subs.begin(); subIter != _subs.end(); subIter++) {

				Regex channelRe(subIter->second.getChannelName());
				if (channelRe.matches(pubIter->second.getChannelName())) {
					subIter->second.added(pubIter->second);
					notifyOfSubscription(nodeSocket, subIter->second, pubIter->second);
				}
			}
		}
		_pendingRemotePubs[remoteId].clear();
	}
	UMUNDO_UNLOCK(_mutex);
}

void ZeroMQNode::removeRemotePubFromLocalSubs(const char* remoteId, const PublisherStub& pub) {
	UMUNDO_LOCK(_mutex);

	map<string, Subscriber>::iterator subIter;
	for (subIter = _subs.begin(); subIter != _subs.end(); subIter++) {

		Regex channelRe(subIter->second.getChannelName());
		if (channelRe.matches(pub.getChannelName())) {
			subIter->second.removed(pub);
		}
	}
	UMUNDO_UNLOCK(_mutex);
}

/**
 * Add a local subscriber.
 *
 * This will call added(pub) for every known publisher with a matching channelname.
 */
void ZeroMQNode::addSubscriber(Subscriber& sub) {
	ScopeLock lock(_mutex);
  if (_subs.find(sub.getUUID()) != _subs.end()) {
    LOG_WARN("Subscriber %s for %s already added", SHORT_UUID(sub.getUUID()).c_str(), sub.getChannelName().c_str());
    return;
  }
  
  _subs[sub.getUUID()] = sub;
	map<string, NodeStub>::iterator nodeIter = _nodes.begin();
	while (nodeIter != _nodes.end()) {
		void* nodeSocket = _sockets[nodeIter->first];
		assert(nodeSocket != NULL);
    
    map<std::string, PublisherStub> pubs = nodeIter->second.getPublishers();
    map<std::string, PublisherStub>::iterator pubIter = pubs.begin();
    // iterate all remote publishers
    while (pubIter != pubs.end()) {
			Regex channelRe(sub.getChannelName());
			if (channelRe.matches(pubIter->second.getChannelName())) {
				sub.added(pubIter->second);
				notifyOfSubscription(nodeSocket, sub, pubIter->second);
			}
			pubIter++;
		}
		nodeIter++;
	}
	assert(validateState());
	/**
	 * ZeroMQ reconnection timeout is 200ms, we need to wait here
	 * for ZeroMQ to reestablish connections that already existed.
	 */
//	Thread::sleepMs(300);
}

/**
 * Remove a local subscriber.
 *
 * This will call removed(pub) for every publisher with a matching channelname.
 */
void ZeroMQNode::removeSubscriber(Subscriber& sub) {

	// do we know this subscriber?
	ScopeLock lock(_mutex);
  if (_subs.find(sub.getUUID()) == _subs.end()) {
    LOG_WARN("Subscriber %s for %s was never added - not removing", SHORT_UUID(sub.getUUID()).c_str(), sub.getChannelName().c_str());
    return;
  }

	// disconnect all publishers
	map<string, NodeStub>::iterator nodeIter = _nodes.begin();
	while (nodeIter != _nodes.end()) {
		void* nodeSocket = _sockets[nodeIter->first];
		assert(nodeSocket != NULL);
    
    map<std::string, PublisherStub> pubs = nodeIter->second.getPublishers();
    map<std::string, PublisherStub>::iterator pubIter = pubs.begin();
    // iterate all remote publishers
    while (pubIter != pubs.end()) {
			Regex channelRe(sub.getChannelName());
			if (channelRe.matches(pubIter->second.getChannelName())) {
				notifyOfUnsubscription(nodeSocket, sub, pubIter->second);
				sub.removed(pubIter->second);
			}
			pubIter++;
		}
    nodeIter++;
	}

  _subs.erase(sub.getUUID());
	assert(validateState());

}

void ZeroMQNode::addPublisher(Publisher& pub) {

  ScopeLock lock(_mutex);
  if (_pubs.find(pub.getUUID()) != _pubs.end()) {
    LOG_WARN("Publisher %s for %s already added", SHORT_UUID(pub.getUUID()).c_str(), pub.getChannelName().c_str());
    return;
  }
  _pubs[pub.getUUID()] = pub;

	// do we already now this publisher?
	LOG_DEBUG("Publisher added %s at %d", pub.getChannelName().c_str(), pub.getPort());

#ifdef PUBPORT_SHARING

  std::stringstream ssInProc;
  ssInProc << "inproc://" << pub->getUUID();
  zmq_connect(_sharedSubSocket, ssInProc.str().c_str()) && LOG_WARN("zmq_connect: %s", zmq_strerror(errno));

#endif

  map<string, void*>::iterator sockIter;
  for (sockIter = _sockets.begin(); sockIter != _sockets.end(); sockIter++) {
    zmq_msg_t msg;
    ZMQ_PREPARE(msg, PUB_INFO_SIZE(pub) + 2);

    char* buffer = (char*)zmq_msg_data(&msg);
    *(uint16_t*)(buffer) = htons(Message::PUB_ADDED);
    writePubInfo(buffer + 2, pub);

    LOG_DEBUG("Informing %s of new publisher", SHORT_UUID(sockIter->first).c_str());
    zmq_sendmsg(sockIter->second, &msg, 0) >= 0 || LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno));
    zmq_msg_close(&msg) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));
  }

	assert(validateState());
}

void ZeroMQNode::removePublisher(Publisher& pub) {

	ScopeLock lock(_mutex);
  if (_pubs.find(pub.getUUID()) == _pubs.end()) {
    LOG_WARN("Publisher %s for %s was never added - not removing", SHORT_UUID(pub.getUUID()).c_str(), pub.getChannelName().c_str());
    return;
  }

  LOG_DEBUG("Publisher removed %s at %d", pub.getChannelName().c_str(), pub.getPort());
  map<string, void*>::iterator sockIter;
  for (sockIter = _sockets.begin(); sockIter != _sockets.end(); sockIter++) {
    zmq_msg_t msg;
    ZMQ_PREPARE(msg, PUB_INFO_SIZE(pub) + 2);

    char* buffer = (char*)zmq_msg_data(&msg);
    *(uint16_t*)(buffer) = htons(Message::PUB_REMOVED);
    writePubInfo(buffer + 2, pub);

    LOG_DEBUG("Informing %s", SHORT_UUID(sockIter->first).c_str());
    zmq_sendmsg(sockIter->second, &msg, 0) >= 0 || LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno));
    zmq_msg_close(&msg) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));
  }

  _pubs.erase(pub.getUUID());
  assert(_pubs.find(pub.getUUID()) == _pubs.end());
	assert(validateState());
}

/**
 * Write channel\0uuid\0port into the given byte array
 */
char* ZeroMQNode::writePubInfo(char* buffer, const PublisherStub& pub) {
	const char* channel = pub.getChannelName().c_str();
	const char* uuid = pub.getUUID().c_str();
	uint16_t port = pub.getPort();

	char* start = buffer;
	(void)start; // surpress unused warning wiithout assert
	memcpy(buffer, channel, strlen(channel));
	buffer += strlen(channel);
	*buffer = 0;
	buffer++;
	memcpy(buffer, uuid, strlen(uuid));
	buffer += strlen(uuid);
	*buffer = 0;
	buffer++;
	*(uint16_t*)buffer = htons(port);
	buffer += 2;
	assert(buffer - start == (int)strlen(channel) + 1 + (int)strlen(uuid) + 1 + 2);
	return buffer;
}

/**
 * Read channel\0port from the given byte array into the variables
 */
char* ZeroMQNode::readPubInfo(char* buffer, uint16_t& port, char*& channelName, char*& uuid) {
	char* start = buffer;
	(void)start; // surpress unused warning without assert
	channelName = buffer;
	buffer += strlen(buffer);
	buffer++;
	uuid = buffer;
	buffer += strlen(buffer);
	buffer++;
	port = ntohs(*(short*)(buffer));
	buffer += 2;
	assert(buffer - start == (int)strlen(channelName) + 1 + (int)strlen(uuid) + 1 + 2);
	return buffer;
}

char* ZeroMQNode::writeSubInfo(char* buffer, const Subscriber& sub) {
	const char* uuid = sub.getUUID().c_str();

	char* start = buffer;
	(void)start; // surpress unused warning without assert
	memcpy(buffer, uuid, strlen(uuid));
	buffer += strlen(uuid);
	*buffer = 0;
	buffer++;
	assert(buffer - start == (int)strlen(uuid) + 1);
	return buffer;
}

char* ZeroMQNode::readSubInfo(char* buffer, char*& uuid) {
	char* start = buffer;
	(void)start; // surpress unused warning without assert
	uuid = buffer;
	buffer += strlen(buffer);
	buffer++;
	assert(buffer - start == (int)strlen(uuid) + 1);
	return buffer;
}


bool ZeroMQNode::validateState() {
#if 0
	UMUNDO_LOCK(_mutex);

	// make sure there is a connection to every node
	map<string, shared_ptr<NodeStub> >::iterator nodeStubIter;
	for (nodeStubIter = _nodes.begin(); nodeStubIter != _nodes.end(); nodeStubIter++) {
		assert(_sockets.find(nodeStubIter->first) != _sockets.end());
//		LOG_DEBUG("Known node at %s", nodeStubIter->second->getIP().c_str());
	}
	assert(_nodes.size() == _sockets.size());

	// make sure we know the node to every remote publisher
	map<string, shared_ptr<NodeStub> >::iterator nodeIter;
	for (nodeIter = _nodes.begin(); nodeIter != _nodes.end(); nodeIter++) {
		if (_pendingRemotePubs.find(nodeIter->first) != _pendingRemotePubs.end())
			continue;
		assert(_sockets.find(nodeIter->first) != _sockets.end());
		shared_ptr<NodeStub> node = nodeIter->second;

		// make sure node info for remote publishers matches
		map<string, shared_ptr<PublisherStub> >::iterator remotePubIter;
		for (remotePubIter = node->getPublishers().begin(); remotePubIter != node->getPublishers().end(); remotePubIter++) {
			shared_ptr<PublisherStub> pub = remotePubIter->second;
			assert(remotePubIter->first == pub->getUUID());
			assert(node->getHost().compare(pub->getHost()) == 0);
			assert(node->getDomain().compare(pub->getDomain()) == 0);
			assert(node->getTransport().compare(pub->getTransport()) == 0);
			assert(node->getIP().compare(pub->getIP()) == 0);
		}
	}

	// make sure we know the node for every remote sub
	map<string, map<string, set<string> > >::iterator remoteSubsIter;
	for (remoteSubsIter = _subscriptions.begin(); remoteSubsIter != _subscriptions.end(); remoteSubsIter++) {
		string uuid = remoteSubsIter->first;
		assert(_nodes.find(uuid) != _nodes.end());
		// we should have deleted such a thing
		assert(remoteSubsIter->second.size() > 0);
		map<string, set<string> >::iterator remoteSubIter;
		for (remoteSubIter = remoteSubsIter->second.begin(); remoteSubIter != remoteSubsIter->second.end(); remoteSubIter++) {

			// we may have deleted the publisher just now
//			assert(_localPubs.find(remoteSubIter->first) != _localPubs.end());

			// we should have deleted such a thing
			assert(remoteSubIter->second.size() > 0);

		}
	}
	UMUNDO_UNLOCK(_mutex);
#endif
	return true;
}
}
