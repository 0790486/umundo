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

#define UMUNDO_PERF_WINDOW_LENGTH_MS 5000
#define UMUNDO_PERF_BUCKET_LENGTH_MS 200.0

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
#include "umundo/common/UUID.h"
#include "umundo/connection/zeromq/ZeroMQPublisher.h"
#include "umundo/connection/zeromq/ZeroMQSubscriber.h"

#define DRAIN_SOCKET(socket) \
for(;;) { \
zmq_getsockopt(socket, ZMQ_RCVMORE, &more, &more_size) && UM_LOG_ERR("zmq_getsockopt: %s", zmq_strerror(errno)); \
if (!more) \
break; \
UM_LOG_INFO("Superfluous message on " #socket ""); \
zmq_recv(socket, NULL, 0, 0) && UM_LOG_ERR("zmq_recv: %s", zmq_strerror(errno)); \
}

#define MORE_OR_RETURN(socket, msg) \
zmq_getsockopt(socket, ZMQ_RCVMORE, &more, &more_size) && UM_LOG_ERR("zmq_getsockopt: %s", zmq_strerror(errno));\
if (!more) { \
if (strlen(msg) > 0)\
UM_LOG_WARN(#msg " on " #socket ""); \
return; \
}

#define COMMON_VARS \
int more; (void)more;\
size_t more_size = sizeof(more); (void)more_size;\
int msgSize; (void)msgSize;\
char* recvBuffer; (void)recvBuffer;\
char* readPtr; (void)readPtr;\
char* writeBuffer; (void)writeBuffer;\
char* writePtr; (void)writePtr;

#define RECV_MSG(socket, msg) \
zmq_msg_t msg; \
zmq_msg_init(&msg) && UM_LOG_ERR("zmq_msg_init: %s", zmq_strerror(errno)); \
zmq_recvmsg(socket, &msg, 0) == -1 && UM_LOG_ERR("zmq_recvmsg: %s", zmq_strerror(errno)); \
msgSize = zmq_msg_size(&msg); \
recvBuffer = (char*)zmq_msg_data(&msg); \
readPtr = recvBuffer;

#define REMAINING_BYTES_TOREAD \
(msgSize - (readPtr - recvBuffer))

#define PUB_INFO_SIZE(pub) \
pub.getChannelName().length() + 1 + pub.getUUID().length() + 1 + 2 + 2

#define SUB_INFO_SIZE(sub) \
sub.getChannelName().length() + 1 + sub.getUUID().length() + 1 + 2

#define PREPARE_MSG(msg, size) \
zmq_msg_t msg; \
zmq_msg_init(&msg) && UM_LOG_ERR("zmq_msg_init: %s", zmq_strerror(errno)); \
zmq_msg_init_size (&msg, size) && UM_LOG_WARN("zmq_msg_init_size: %s",zmq_strerror(errno));\
writeBuffer = (char*)zmq_msg_data(&msg);\
writePtr = writeBuffer;\
 
#define NODE_BROADCAST_MSG(msg) \
std::map<std::string, boost::shared_ptr<NodeConnection> >::iterator nodeIter_ = _connFrom.begin();\
while (nodeIter_ != _connFrom.end()) {\
	if (UUID::isUUID(nodeIter_->first)) {\
		zmq_msg_t broadCastMsgCopy_;\
		zmq_msg_init(&broadCastMsgCopy_) && UM_LOG_ERR("zmq_msg_init: %s", zmq_strerror(errno));\
		zmq_msg_copy(&broadCastMsgCopy_, &msg) && UM_LOG_ERR("zmq_msg_copy: %s", zmq_strerror(errno));\
		UM_LOG_DEBUG("%s: Broadcasting to %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(nodeIter_->first).c_str()); \
		zmq_send(_nodeSocket, nodeIter_->first.c_str(), nodeIter_->first.length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));\
		_buckets.back().nrMetaMsgSent++;\
		_buckets.back().sizeMetaMsgSent += nodeIter_->first.length();\
		zmq_msg_send(&broadCastMsgCopy_, _nodeSocket, ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_msg_send: %s", zmq_strerror(errno));\
		_buckets.back().nrMetaMsgSent++;\
		_buckets.back().sizeMetaMsgSent += zmq_msg_size(&broadCastMsgCopy_);\
		zmq_msg_close(&broadCastMsgCopy_) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));\
	}\
	nodeIter_++;\
}

#define RESETSS(ss) ss.clear(); ss.str(std::string(""));

namespace umundo {

void* ZeroMQNode::getZeroMQContext() {
	if (_zmqContext == NULL) {
		(_zmqContext = zmq_ctx_new()) || UM_LOG_ERR("zmq_init: %s",zmq_strerror(errno));
	}
	return _zmqContext;
}
void* ZeroMQNode::_zmqContext = NULL;

ZeroMQNode::ZeroMQNode() {
}

ZeroMQNode::~ZeroMQNode() {
	stop();

	UM_LOG_INFO("%s: node shutting down", SHORT_UUID(_uuid).c_str());

	char tmp[4];
	writeVersionAndType(tmp, Message::SHUTDOWN);
	zmq_send(_writeOpSocket, tmp, 4, 0) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno)); // unblock poll
	join(); // wait for thread to finish

	COMMON_VARS;
	ScopeLock lock(_mutex);

	PREPARE_MSG(shutdownMsg, 4 + 37);
	writePtr = writeVersionAndType(writePtr, Message::SHUTDOWN);
	assert(writePtr - writeBuffer == 4);
	writePtr = writeString(writePtr, _uuid.c_str(), _uuid.length());
	assert(writePtr - writeBuffer == zmq_msg_size(&shutdownMsg));

	NODE_BROADCAST_MSG(shutdownMsg);
	zmq_msg_close(&shutdownMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));

	// delete all connection information
	while(_connTo.size() > 0) {
		NodeStub node = _connTo.begin()->second->node;
		if(node) {
			removed(node);
			processOpComm();
		} else {
			_connTo.erase(_connTo.begin());
		}
	}

	while(_connFrom.size() > 0) {
		_connFrom.erase(_connFrom.begin());
	}

	// close sockets
	zmq_close(_nodeSocket)    && UM_LOG_ERR("zmq_close: %s", zmq_strerror(errno));
	zmq_close(_pubSocket)     && UM_LOG_ERR("zmq_close: %s", zmq_strerror(errno));
	zmq_close(_subSocket)     && UM_LOG_ERR("zmq_close: %s",zmq_strerror(errno));
	zmq_close(_readOpSocket)  && UM_LOG_ERR("zmq_close: %s", zmq_strerror(errno));
	zmq_close(_writeOpSocket) && UM_LOG_ERR("zmq_close: %s", zmq_strerror(errno));
	UM_LOG_INFO("%s: node gone", SHORT_UUID(_uuid).c_str());

}

void ZeroMQNode::init(Options* options) {
	_options = options->getKVPs();
	_port = strTo<uint16_t>(_options["node.port.node"]);
	_pubPort = strTo<uint16_t>(_options["node.port.pub"]);
	_allowLocalConns = strTo<bool>(_options["node.allowLocal"]);

	_transport = "tcp";
	_ip = "127.0.0.1";
	_lastNodeInfoBroadCast = Thread::getTimeStampMs();

	int routMand = 1;
	int routProbe = 0;
	int vbsSub = 1;
	int sndhwm = NET_ZEROMQ_SND_HWM;
	int rcvhwm = NET_ZEROMQ_RCV_HWM;

	(_nodeSocket = zmq_socket(ZeroMQNode::getZeroMQContext(), ZMQ_ROUTER))  || UM_LOG_ERR("zmq_socket: %s", zmq_strerror(errno));
	(_pubSocket = zmq_socket(ZeroMQNode::getZeroMQContext(), ZMQ_XPUB))     || UM_LOG_ERR("zmq_socket: %s", zmq_strerror(errno));
	(_subSocket = zmq_socket(ZeroMQNode::getZeroMQContext(), ZMQ_SUB))      || UM_LOG_ERR("zmq_socket: %s", zmq_strerror(errno));
	(_readOpSocket  = zmq_socket(ZeroMQNode::getZeroMQContext(), ZMQ_PAIR)) || UM_LOG_ERR("zmq_socket: %s", zmq_strerror(errno));
	(_writeOpSocket = zmq_socket(ZeroMQNode::getZeroMQContext(), ZMQ_PAIR)) || UM_LOG_ERR("zmq_socket: %s", zmq_strerror(errno));

	// connect read and write op sockets
	std::string readOpId("inproc://um.node.readop." + _uuid);
	zmq_bind(_readOpSocket, readOpId.c_str())  && UM_LOG_ERR("zmq_bind: %s", zmq_strerror(errno))
	zmq_connect(_writeOpSocket, readOpId.c_str()) && UM_LOG_ERR("zmq_connect %s: %s", readOpId.c_str(), zmq_strerror(errno));

	// connect node socket
	if (_port > 0) {
		std::stringstream ssNodeAddress;
		ssNodeAddress << "tcp://*:" << _port;
		zmq_bind(_nodeSocket, ssNodeAddress.str().c_str()) && UM_LOG_ERR("zmq_bind: %s", zmq_strerror(errno));
	} else {
		_port = bindToFreePort(_nodeSocket, "tcp", "*");
	}
	std::string nodeId("um.node." + _uuid);
	zmq_bind(_nodeSocket, std::string("inproc://" + nodeId).c_str()) && UM_LOG_ERR("zmq_bind: %s", zmq_strerror(errno));
	//  zmq_bind(_nodeSocket, std::string("ipc://" + nodeId).c_str())    && UM_LOG_WARN("zmq_bind: %s %s", std::string("ipc://" + nodeId).c_str(),  zmq_strerror(errno));

	// connect publisher socket
	if (_pubPort > 0) {
		std::stringstream ssPubAddress;
		ssPubAddress << "tcp://*:" << _pubPort;
		zmq_bind(_pubSocket, ssPubAddress.str().c_str()) && UM_LOG_ERR("zmq_bind: %s", zmq_strerror(errno));
	} else {
		_pubPort = bindToFreePort(_pubSocket, "tcp", "*");
	}
	std::string pubId("um.pub." + _uuid);
	zmq_bind(_pubSocket,  std::string("inproc://" + pubId).c_str())  && UM_LOG_ERR("zmq_bind: %s", zmq_strerror(errno))
	//  zmq_bind(_pubSocket,  std::string("ipc://" + pubId).c_str())     && UM_LOG_WARN("zmq_bind: %s %s", std::string("ipc://" + pubId).c_str(), zmq_strerror(errno))

	zmq_setsockopt(_pubSocket, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm))         && UM_LOG_ERR("zmq_setsockopt: %s", zmq_strerror(errno));
	zmq_setsockopt(_pubSocket, ZMQ_XPUB_VERBOSE, &vbsSub, sizeof(vbsSub))   && UM_LOG_ERR("zmq_setsockopt: %s", zmq_strerror(errno)); // receive all subscriptions

	zmq_setsockopt(_subSocket, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm)) && UM_LOG_ERR("zmq_setsockopt: %s", zmq_strerror(errno));
	zmq_setsockopt(_subSocket, ZMQ_SUBSCRIBE, "", 0)                && UM_LOG_ERR("zmq_setsockopt: %s", zmq_strerror(errno)); // subscribe to every internal publisher

	zmq_setsockopt(_nodeSocket, ZMQ_IDENTITY, _uuid.c_str(), _uuid.length())        && UM_LOG_ERR("zmq_setsockopt: %s", zmq_strerror(errno));
	zmq_setsockopt(_nodeSocket, ZMQ_ROUTER_MANDATORY, &routMand, sizeof(routMand))  && UM_LOG_ERR("zmq_setsockopt: %s", zmq_strerror(errno));
	zmq_setsockopt(_nodeSocket, ZMQ_PROBE_ROUTER, &routProbe, sizeof(routProbe))    && UM_LOG_ERR("zmq_setsockopt: %s", zmq_strerror(errno));

	sockets[0].socket = _nodeSocket;
	sockets[1].socket = _pubSocket;
	sockets[2].socket = _readOpSocket;
	sockets[3].socket = _subSocket;
	sockets[0].fd = sockets[1].fd = sockets[2].fd = sockets[3].fd = 0;
	sockets[0].events = sockets[1].events = sockets[2].events = sockets[3].events = ZMQ_POLLIN;

	start();
}

boost::shared_ptr<Implementation> ZeroMQNode::create() {
	return boost::shared_ptr<ZeroMQNode>(new ZeroMQNode());
}

void ZeroMQNode::suspend() {}

void ZeroMQNode::resume() {}

uint16_t ZeroMQNode::bindToFreePort(void* socket, const std::string& transport, const std::string& address) {
	std::stringstream ss;
	int port = 4242;

	ss << transport << "://" << address << ":" << port;

	while(zmq_bind(socket, ss.str().c_str()) == -1) {
		switch(errno) {
		case EADDRINUSE:
			port++;
			ss.clear();        // clear error bits
			ss.str(std::string());  // reset string
			ss << transport << "://" << address << ":" << port;
			break;
		default:
			UM_LOG_WARN("zmq_bind at %s: %s", ss.str().c_str(), zmq_strerror(errno));
			Thread::sleepMs(100);
		}
	}

	return port;
}

std::map<std::string, NodeStub> ZeroMQNode::connectedFrom() {
	ScopeLock lock(_mutex);

	std::map<std::string, NodeStub> from;
	ScopeLock(_mutex);
	std::map<std::string, boost::shared_ptr<NodeConnection> >::iterator nodeIter = _connFrom.begin();
	while (nodeIter != _connFrom.end()) {
		// this will report ourself as well
		from[nodeIter->first] = nodeIter->second->node;
		nodeIter++;
	}
	return from;
}

std::map<std::string, NodeStub> ZeroMQNode::connectedTo() {
	ScopeLock lock(_mutex);

	std::map<std::string, NodeStub> to;
	ScopeLock(_mutex);
	std::map<std::string, boost::shared_ptr<NodeConnection> >::iterator nodeIter = _connTo.begin();
	while (nodeIter != _connTo.end()) {
		// only report UUIDs as keys
		if (UUID::isUUID(nodeIter->first)) {
			to[nodeIter->first] = nodeIter->second->node;
		}
		nodeIter++;
	}
	return to;
}

void ZeroMQNode::addSubscriber(Subscriber& sub) {
	ScopeLock lock(_mutex);
	if (_subs.find(sub.getUUID()) != _subs.end())
		return;

	UM_LOG_INFO("%s added subscriber %s on %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(sub.getUUID()).c_str(), sub.getChannelName().c_str());

	_subs[sub.getUUID()] = sub;

	std::map<std::string, boost::shared_ptr<NodeConnection> >::const_iterator nodeIter = _connTo.begin();
	while (nodeIter != _connTo.end()) {
		if (nodeIter->second && nodeIter->second->node) {
			std::map<std::string, PublisherStub> pubs = nodeIter->second->node.getPublishers();
			std::map<std::string, PublisherStub>::iterator pubIter = pubs.begin();

			// iterate all remote publishers and remove from sub
			while (pubIter != pubs.end()) {
				if(sub.matches(pubIter->second.getChannelName())) {
					sub.added(pubIter->second, nodeIter->second->node);
					sendSubAdded(nodeIter->first.c_str(), sub, pubIter->second);
				}
				pubIter++;
			}
		}
		nodeIter++;
	}
}

void ZeroMQNode::removeSubscriber(Subscriber& sub) {
	ScopeLock lock(_mutex);

	if (_subs.find(sub.getUUID()) == _subs.end())
		return;

	UM_LOG_INFO("%s removed subscriber %d on %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(sub.getUUID()).c_str(), sub.getChannelName().c_str());

	std::map<std::string, boost::shared_ptr<NodeConnection> >::const_iterator nodeIter = _connTo.begin();
	while (nodeIter != _connTo.end()) {
		std::map<std::string, PublisherStub> pubs = nodeIter->second->node.getPublishers();
		std::map<std::string, PublisherStub>::iterator pubIter = pubs.begin();

		// iterate all remote publishers and remove from sub
		while (pubIter != pubs.end()) {
			if(sub.matches(pubIter->second.getChannelName())) {
				sub.removed(pubIter->second, nodeIter->second->node);
				sendSubRemoved(nodeIter->first.c_str(), sub, pubIter->second);
			}
			pubIter++;
		}
		nodeIter++;
	}
	_subs.erase(sub.getUUID());
}

void ZeroMQNode::addPublisher(Publisher& pub) {
	ScopeLock lock(_mutex);
	COMMON_VARS;

	if (_pubs.find(pub.getUUID()) != _pubs.end())
		return;

	size_t bufferSize = 4 + _uuid.length() + 1 + PUB_INFO_SIZE(pub);
	PREPARE_MSG(pubAddedMsg, bufferSize);

	UM_LOG_INFO("%s added publisher %s on %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(pub.getUUID()).c_str(), pub.getChannelName().c_str());

	writePtr = writeVersionAndType(writePtr, Message::PUB_ADDED);
	writePtr = writeString(writePtr, _uuid.c_str(), _uuid.length());
	writePtr = writePubInfo(writePtr, pub);
	assert(writePtr - writeBuffer == bufferSize);

	zmq_msg_send(&pubAddedMsg, _writeOpSocket, 0) == -1 && UM_LOG_ERR("zmq_msg_send: %s", zmq_strerror(errno));
	_buckets.back().nrMetaMsgSent++;
	_buckets.back().sizeMetaMsgSent += bufferSize;

	_pubs[pub.getUUID()] = pub;
	zmq_msg_close(&pubAddedMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));

}

void ZeroMQNode::removePublisher(Publisher& pub) {
	ScopeLock lock(_mutex);
	COMMON_VARS;

	if (_pubs.find(pub.getUUID()) == _pubs.end())
		return;

	size_t bufferSize = 4 + _uuid.length() + 1 + PUB_INFO_SIZE(pub);
	PREPARE_MSG(pubRemovedMsg, bufferSize);

	UM_LOG_INFO("%s removed publisher %s on %s", SHORT_UUID(_uuid).c_str(), SHORT_UUID(pub.getUUID()).c_str(), pub.getChannelName().c_str());

	writePtr = writeVersionAndType(writePtr, Message::PUB_REMOVED);
	writePtr = writeString(writePtr, _uuid.c_str(), _uuid.length());
	writePtr = writePubInfo(writePtr, pub);
	assert(writePtr - writeBuffer == bufferSize);

	zmq_msg_send(&pubRemovedMsg, _writeOpSocket, 0) == -1 && UM_LOG_ERR("zmq_msg_send: %s", zmq_strerror(errno));
	_buckets.back().nrMetaMsgSent++;
	_buckets.back().sizeMetaMsgSent += bufferSize;

	zmq_msg_close(&pubRemovedMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
	_pubs.erase(pub.getUUID());

}

/**
 * Remote endpoint was added.
 *
 * added(EndPoint) -> NODE_CONNECT_REQ   -> local opSocket:
 * This step is needed to avoid threading issues when operatin on socekts for 0mq
 *
 * processOpComm   -> NODE_CONNECT_REQ   -> new client socket to remote router socket
 * We send a connection request with our information to the remote router socket
 *
 * remote router   -> NODE_CONNECT_REPLY -> local client socket
 * Remote socket replies with its information and a list of its publishers
 *
 * Connection from this to remote is established.
 */
void ZeroMQNode::added(EndPoint endPoint) {
	ScopeLock lock(_mutex);

	UM_LOG_INFO("%s: Adding endpoint at %s://%s:%d",
	            SHORT_UUID(_uuid).c_str(),
	            endPoint.getTransport().c_str(),
	            endPoint.getIP().c_str(),
	            endPoint.getPort());

	COMMON_VARS;

	std::stringstream otherAddress;
	otherAddress << endPoint.getTransport() << "://" << endPoint.getIP() << ":" << endPoint.getPort();

	// write connection request to operation socket
	PREPARE_MSG(addEndPointMsg, 4 + otherAddress.str().length() + 1);

	writePtr = writeVersionAndType(writePtr, Message::CONNECT_REQ);
	assert(writePtr - writeBuffer == 4);
	writePtr = writeString(writePtr, otherAddress.str().c_str(), otherAddress.str().length());
	assert(writePtr - writeBuffer == 4 + otherAddress.str().length() + 1);

	zmq_sendmsg(_writeOpSocket, &addEndPointMsg, 0) == -1 && UM_LOG_ERR("zmq_sendmsg: %s", zmq_strerror(errno));
	zmq_msg_close(&addEndPointMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
}

void ZeroMQNode::removed(EndPoint endPoint) {
	ScopeLock lock(_mutex);

	UM_LOG_INFO("%s: Removing endpoint at %s://%s:%d",
	            SHORT_UUID(_uuid).c_str(),
	            endPoint.getTransport().c_str(),
	            endPoint.getIP().c_str(),
	            endPoint.getPort());

	COMMON_VARS;
	std::stringstream otherAddress;
	otherAddress << endPoint.getTransport() << "://" << endPoint.getIP() << ":" << endPoint.getPort();

	PREPARE_MSG(removeEndPointMsg, 4 + otherAddress.str().length() + 1);
	writePtr = writeVersionAndType(writePtr, Message::DISCONNECT);
	assert(writePtr - writeBuffer == 4);
	writePtr = writeString(writePtr, otherAddress.str().c_str(), otherAddress.str().length());
	assert(writePtr - writeBuffer == 4 + otherAddress.str().length() + 1);

	zmq_sendmsg(_writeOpSocket, &removeEndPointMsg, 0) == -1 && UM_LOG_ERR("zmq_sendmsg: %s", zmq_strerror(errno));
	zmq_msg_close(&removeEndPointMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));

}

void ZeroMQNode::changed(EndPoint endPoint) {
}

/**
 * Process messages sent to our node socket.
 */
void ZeroMQNode::processNodeComm() {
	COMMON_VARS;
	
#if 0
	for(;;) {
		RECV_MSG(_nodeSocket, msg);
		printf("%s: node socket received %d bytes from %s\n", _uuid.c_str(), msgSize, readPtr);
		zmq_msg_close(&msg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
		MORE_OR_RETURN(_nodeSocket, "");
	}
#endif

	// read first message
	RECV_MSG(_nodeSocket, header);
	_buckets.back().nrMetaMsgRcvd++;
	_buckets.back().sizeMetaMsgRcvd += msgSize;

	std::string from(recvBuffer, msgSize);
	zmq_msg_close(&header) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));

	if (from.length() == 36) {
		RECV_MSG(_nodeSocket, content);

		// remember node and update last seen
		//touchNeighbor(from);

		// dealer socket sends no delimiter, but req does
		if (REMAINING_BYTES_TOREAD == 0) {
			zmq_getsockopt(_nodeSocket, ZMQ_RCVMORE, &more, &more_size) && UM_LOG_ERR("zmq_getsockopt: %s", zmq_strerror(errno));
			if (!more) {
				zmq_msg_close(&content) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
				return;
			}
			RECV_MSG(_nodeSocket, content);
		}

		_buckets.back().nrMetaMsgRcvd++;
		_buckets.back().sizeMetaMsgRcvd += msgSize;

		// assume the mesage has at least version and type
		if (REMAINING_BYTES_TOREAD < 4) {
			zmq_msg_close(&content) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
			return;
		}

		Message::Type type;
		uint16_t version;
		readPtr = readVersionAndType(recvBuffer, version, type);

		if (version != Message::VERSION) {
			UM_LOG_INFO("%s: node socket received unversioned or different message format version - discarding", SHORT_UUID(_uuid).c_str());
			zmq_msg_close(&content) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
			return;
		}

		UM_LOG_INFO("%s: node socket received %s", SHORT_UUID(_uuid).c_str(), Message::typeToString(type));
		switch (type) {
		case Message::DEBUG: {
			// someone wants debug info from us
			replyWithDebugInfo(from);
			break;
		}
		case Message::CONNECT_REQ: {

			// someone is about to connect to us
			if (from != _uuid || _allowLocalConns)
				processConnectedFrom(from);

			// reply with our uuid and publishers
			UM_LOG_INFO("%s: Replying with CONNECT_REP and %d pubs on _nodeSocket to %s", SHORT_UUID(_uuid).c_str(), _pubs.size(), SHORT_UUID(from).c_str());
			zmq_send(_nodeSocket, from.c_str(), from.length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno)); // return to sender
			_buckets.back().nrMetaMsgSent++;
			_buckets.back().sizeMetaMsgSent += from.length();

			zmq_msg_t replyNodeInfoMsg;
			writeNodeInfo(&replyNodeInfoMsg, Message::CONNECT_REP);

			zmq_sendmsg(_nodeSocket, &replyNodeInfoMsg, ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_sendmsg: %s", zmq_strerror(errno));
			_buckets.back().nrMetaMsgSent++;
			_buckets.back().sizeMetaMsgSent += zmq_msg_size(&replyNodeInfoMsg);

			zmq_msg_close(&replyNodeInfoMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
			break;
		}
		case Message::SUBSCRIBE:
		case Message::UNSUBSCRIBE: {
			// a remote node subscribed or unsubscribed to one of our publishers

			char* subChannelName;
			char* subUUID;
			char* pubChannelName;
			char* pubUUID;
			uint16_t pubPort;
			uint16_t pubType;
			uint16_t subType;

			readPtr = readSubInfo(readPtr, subType, subChannelName, subUUID);
			readPtr = readPubInfo(readPtr, pubType, pubPort, pubChannelName, pubUUID);

			assert(REMAINING_BYTES_TOREAD == 0);
			ScopeLock lock(_mutex);

			if (_pubs.find(pubUUID) == _pubs.end())
				break;

			if (type == Message::SUBSCRIBE) {
				// confirm subscription
				if (!_subscriptions[subUUID].subStub) {
					_subscriptions[subUUID].subStub = SubscriberStub(boost::shared_ptr<SubscriberStubImpl>(new SubscriberStubImpl()));
					_subscriptions[subUUID].subStub.getImpl()->setChannelName(subChannelName);
					_subscriptions[subUUID].subStub.getImpl()->setUUID(subUUID);
					_subscriptions[subUUID].subStub.getImpl()->implType = subType;
				}
				_subscriptions[subUUID].nodeUUID = from;
				_subscriptions[subUUID].pending[pubUUID] = _pubs[pubUUID];

				if (_subscriptions[subUUID].isZMQConfirmed || subType != Subscriber::ZEROMQ)
					confirmSub(subUUID);
			} else {
				// remove a subscription
				Subscription& confSub = _subscriptions[subUUID];
				if (_connFrom.find(confSub.nodeUUID) != _connFrom.end() && _connFrom[confSub.nodeUUID]->node)
					_connFrom[confSub.nodeUUID]->node.removeSubscriber(confSub.subStub);

				// move all pending subscriptions to confirmed
				std::map<std::string, Publisher>::iterator confPubIter = confSub.confirmed.begin();
				while(confPubIter != confSub.confirmed.end()) {
					confPubIter->second.removed(confSub.subStub, _connFrom[confSub.nodeUUID]->node);
					confPubIter++;
				}
				confSub.confirmed.clear();

			}
			break;
		}

		default:
			break;
		}
		zmq_msg_close(&content) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
		return;

	} else {
		UM_LOG_WARN("%s: Got unprefixed message on _nodeSocket - discarding", SHORT_UUID(_uuid).c_str());
	}
}

void ZeroMQNode::processPubComm() {
	COMMON_VARS;
	/**
	 * someone subscribed, process here to avoid
	 * XPUB socket and thread at publisher
	 */
	ScopeLock lock(_mutex);
	zmq_msg_t message;
	while (1) {
		//  Process all parts of the message
		zmq_msg_init(&message)  && UM_LOG_ERR("zmq_msg_init: %s", zmq_strerror(errno));
		zmq_msg_recv(&message, _pubSocket, 0);

		char* data = (char*)zmq_msg_data(&message);
		bool subscription = (data[0] == 0x1);
		std::string subChannel(data+1, zmq_msg_size(&message) - 1);
		std::string subUUID;
		if (UUID::isUUID(subChannel.substr(1, zmq_msg_size(&message) - 2))) {
			subUUID = subChannel.substr(1, zmq_msg_size(&message) - 2);
		}

		if (subscription) {
			UM_LOG_INFO("%s: Got 0MQ subscription on %s", _uuid.c_str(), subChannel.c_str());
			if (subUUID.length() > 0) {
				// every subscriber subscribes to its uuid prefixed with a "~" for late alphabetical order
				ScopeLock lock(_mutex);
				_subscriptions[subUUID].isZMQConfirmed = true;
				if (_subscriptions[subUUID].subStub)
					confirmSub(subUUID);
			}
		} else {
			UM_LOG_INFO("%s: Got 0MQ unsubscription on %s", _uuid.c_str(), subChannel.c_str());
		}

		zmq_getsockopt (_pubSocket, ZMQ_RCVMORE, &more, &more_size) && UM_LOG_ERR("zmq_getsockopt: %s", zmq_strerror(errno));
		zmq_msg_close (&message) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
		assert(!more); // subscriptions are not multipart
		if (!more)
			break;      //  Last message part
	}

}

/**
 * Process messages sent to one of the client sockets from a remote node
 */
void ZeroMQNode::processClientComm(boost::shared_ptr<NodeConnection> client) {
	COMMON_VARS;

	// we have a reply from the server
	RECV_MSG(client->socket, opMsg);

	if (REMAINING_BYTES_TOREAD < 4) {
		zmq_msg_close(&opMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
		return;
	}

	Message::Type type;
	uint16_t version;
	readPtr = readVersionAndType(recvBuffer, version, type);

	if (version != Message::VERSION) {
		UM_LOG_INFO("%s: client socket received unversioned or different message format version - discarding", SHORT_UUID(_uuid).c_str());
		zmq_msg_close(&opMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
		return;
	}

	UM_LOG_INFO("%s: client socket received %s from %s", SHORT_UUID(_uuid).c_str(), Message::typeToString(type), client->address.c_str());

	switch (type) {
	case Message::PUB_REMOVED:
	case Message::PUB_ADDED: {

		if (REMAINING_BYTES_TOREAD < 37) {
			break;
		}

		char* from;
		readPtr = readString(readPtr, from, 37);
		uint16_t port;
		uint16_t pubType;
		char* channelName;
		char* pubUUID;

		readPtr = readPubInfo(readPtr, pubType, port, channelName, pubUUID);
		assert(REMAINING_BYTES_TOREAD == 0);

		ScopeLock lock(_mutex);

		if (type == Message::PUB_ADDED) {
			processRemotePubAdded(from, port, pubType, channelName, pubUUID);
		} else {
			processRemotePubRemoved(from, port, pubType, channelName, pubUUID);
		}

	}
	case Message::SHUTDOWN: {
		// a remote neighbor shut down
		if (REMAINING_BYTES_TOREAD < 37) {
			break;
		}

		char* from;
		readPtr = readString(readPtr, from, 37);
		assert(REMAINING_BYTES_TOREAD == 0);

		ScopeLock lock(_mutex);
		if (_connFrom.find(from) == _connFrom.end()) {
			// node terminated, it's no longer connected to us
			_connFrom.erase(from);
		}

		// if we were connected, remove it, object will be destructed there
		if (_connTo.find(from) != _connTo.end()) {
			removed(_connTo[from]->node);
		}

		break;
	}
	case Message::CONNECT_REP: {

		// remote server answered our connect_req
		if (REMAINING_BYTES_TOREAD < 37) {
			break;
		}

		char* fromUUID;
		readPtr = readString(readPtr, fromUUID, 37);

		ScopeLock(_mutex);
		assert(client->address.length() > 0);

		if (fromUUID == _uuid && !_allowLocalConns) // do not connect to ourself
			break;

		processConnectedTo(fromUUID, client);
		processNodeInfo(recvBuffer + 4, msgSize - 4);

		break;
	}
	default:
		UM_LOG_WARN("%s: Unhandled message type on client socket", SHORT_UUID(_uuid).c_str());
		break;
	}
	zmq_msg_close(&opMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
}

/**
 * Process messages sent to the internal operation socket.
 */
void ZeroMQNode::processOpComm() {
	COMMON_VARS;

	// read first message
	RECV_MSG(_readOpSocket, opMsg)

	Message::Type type;
	uint16_t version;

	readPtr = readVersionAndType(recvBuffer, version, type);

	UM_LOG_INFO("%s: internal op socket received %s", SHORT_UUID(_uuid).c_str(), Message::typeToString(type));
	switch (type) {
	case Message::PUB_REMOVED:
	case Message::PUB_ADDED: {
		// removePublisher / addPublisher called us
		char* uuid;
		uint16_t port;
		uint16_t pubType;
		char* channelName;
		char* pubUUID;

		readPtr = readString(readPtr, uuid, 37);
		readPtr = readPubInfo(readPtr, pubType, port, channelName, pubUUID);
		assert(REMAINING_BYTES_TOREAD == 0);

		std::string internalPubId("inproc://um.pub.intern.");
		internalPubId += pubUUID;

		if (type == Message::PUB_ADDED) {
			zmq_connect(_subSocket, internalPubId.c_str()) && UM_LOG_ERR("zmq_connect %s: %s", internalPubId.c_str(), zmq_strerror(errno));
		} else {
			zmq_disconnect(_subSocket, internalPubId.c_str()) && UM_LOG_ERR("zmq_connect %s: %s", internalPubId.c_str(), zmq_strerror(errno));
		}

		// tell every other node that we changed publishers
		NODE_BROADCAST_MSG(opMsg);

		break;
	}
	case Message::DISCONNECT: {
		// endpoint was removed
		char* address;
		readPtr = readString(readPtr, address, msgSize - (readPtr - recvBuffer));

		ScopeLock lock(_mutex);
		if (_connTo.find(address) == _connTo.end())
			return;

		_connTo[address]->refCount--;

		if (_connTo[address]->refCount <= 0) {
			NodeStub& nodeStub = _connTo[address]->node;
			std::string nodeUUID = nodeStub.getUUID();
			disconnectRemoteNode(nodeStub);

			// disconnect socket and remove as a neighbors
			zmq_close(_connTo[address]->socket);
			_connTo[address]->socket = NULL;

			// delete NodeConnection object if not contained in _connFrom as well
			if (!_connTo[address]->connectedFrom) {
				//delete _connTo[address];
			} else {
				assert(nodeUUID.size() > 0);
				assert(_connFrom.find(nodeUUID) != _connFrom.end());
				
				//delete _connFrom[nodeUUID];
				_connFrom.erase(nodeUUID);

			}
			_connTo.erase(address);

			if (nodeUUID.length() > 0)
				_connTo.erase(nodeUUID);
		}

		break;
	}
	case Message::CONNECT_REQ: {
		// added(EndPoint) called us - rest of message is endpoint address
		char* address;
		readPtr = readString(readPtr, address, msgSize - (readPtr - recvBuffer));

		ScopeLock lock(_mutex);

		boost::shared_ptr<NodeConnection> clientConn;
		// we don't know this endpoint
		if (_connTo.find(address) == _connTo.end()) {
			// open a new client connection
			clientConn = boost::shared_ptr<NodeConnection>(new NodeConnection(address, _uuid));
			if (!clientConn->socket) {
//				delete clientConn;
				break;
			}
			_connTo[address] = clientConn;
		} else {
			clientConn = _connTo[address];
		}

		UM_LOG_INFO("%s: Sending CONNECT_REQ to %s", SHORT_UUID(_uuid).c_str(), address);

		// send a CONNECT_REQ message
		PREPARE_MSG(connReqMsg, 4);
		writePtr = writeVersionAndType(writePtr, Message::CONNECT_REQ);
		assert(writePtr - writeBuffer == zmq_msg_size(&connReqMsg));

		zmq_sendmsg(clientConn->socket, &connReqMsg, ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_sendmsg: %s", zmq_strerror(errno));
		_buckets.back().nrMetaMsgSent++;
		_buckets.back().sizeMetaMsgSent += 4;

		zmq_msg_close(&connReqMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));

		break;
	}
	case Message::SHUTDOWN: {
		// do we need to do something here - destructor does most of the work
		break;
	}
	default:
		UM_LOG_WARN("%s: Unhandled message type on internal op socket", SHORT_UUID(_uuid).c_str());
		break;
	}
	zmq_msg_close(&opMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
}

void ZeroMQNode::run() {
	int more;
	size_t more_size = sizeof(more);
	size_t stdSockets = 4;
	size_t index;
	std::list<std::pair<uint32_t, std::string> > nodeSockets;

	while(isStarted()) {
		_mutex.lock();
		size_t maxSockets = _connTo.size() + stdSockets;

		// reset std socket event flag
		for (int i = 0; i < stdSockets; i++) {
			sockets[i].revents = 0;
		}

		zmq_pollitem_t* items = (zmq_pollitem_t*)malloc(sizeof(zmq_pollitem_t) * maxSockets);
		// prepopulate with standard sockets
		memcpy(items, sockets, stdSockets * sizeof(zmq_pollitem_t));

		index = stdSockets;
		std::map<std::string, boost::shared_ptr<NodeConnection> >::const_iterator sockIter = _connTo.begin();
		while (sockIter != _connTo.end()) {
			if (!UUID::isUUID(sockIter->first)) { // only add if key is an address
				items[index].socket = sockIter->second->socket;
				items[index].fd = 0;
				items[index].events = ZMQ_POLLIN;
				items[index].revents = 0;
				nodeSockets.push_back(std::make_pair(index, sockIter->first));
				index++;
			}
			sockIter++;
		}
		
		// make sure we have a bucket for performance measuring
		if (_buckets.size() == 0)
			_buckets.push_back(StatBucket<size_t>());

		//UM_LOG_DEBUG("%s: polling on %ld sockets", _uuid.c_str(), nrSockets);
		_mutex.unlock();
		zmq_poll(items, index, -1);
		_mutex.lock();
		// We do have a message to read!
		
		// manage performane status buckets
		uint64_t now = Thread::getTimeStampMs();
		while (_buckets.size() > 0 && _buckets.front().timeStamp < now - UMUNDO_PERF_WINDOW_LENGTH_MS) {
			// drop oldest bucket
			_buckets.pop_front();
		}
		if (_buckets.back().timeStamp < now - UMUNDO_PERF_BUCKET_LENGTH_MS) {
			// we need a new bucket
			_buckets.push_back(StatBucket<size_t>());
		}
		
		// look through node sockets
		while(nodeSockets.size() > 0) {
			if (items[nodeSockets.front().first].revents & ZMQ_POLLIN) {
				ScopeLock lock(_mutex);
				if (_connTo.find(nodeSockets.front().second) != _connTo.end()) {
					processClientComm(_connTo[nodeSockets.front().second]);
				} else {
					UM_LOG_WARN("%s: message from vanished node %s", _uuid.c_str(), nodeSockets.front().second.c_str());
				}
				break;
			}
			nodeSockets.pop_front();
		}
		nodeSockets.clear();
		

		if (items[0].revents & ZMQ_POLLIN) {
			processNodeComm();
			DRAIN_SOCKET(_nodeSocket);
		}

		if (items[1].revents & ZMQ_POLLIN) {
			processPubComm();
			DRAIN_SOCKET(_pubSocket);
		}

		if (items[2].revents & ZMQ_POLLIN) {
			processOpComm();
			DRAIN_SOCKET(_readOpSocket);
		}

		// someone is publishing - this is last to
		if (items[3].revents & ZMQ_POLLIN) {
			size_t msgSize = 0;
			zmq_msg_t message;
			std::string channelName;
			while (1) {
				//  Process all parts of the message
				zmq_msg_init (&message) && UM_LOG_ERR("zmq_msg_init: %s", zmq_strerror(errno));
				zmq_msg_recv (&message, _subSocket, 0);
				msgSize = zmq_msg_size(&message);
				
				if (channelName.size() == 0)
					channelName = std::string((char*)zmq_msg_data(&message));
				
				_buckets.back().nrChannelMsg[channelName]++;
				_buckets.back().sizeChannelMsg[channelName] += msgSize;

				zmq_getsockopt (_subSocket, ZMQ_RCVMORE, &more, &more_size) && UM_LOG_ERR("zmq_getsockopt: %s", zmq_strerror(errno));
				zmq_msg_send(&message, _pubSocket, more ? ZMQ_SNDMORE: 0) == -1 && UM_LOG_ERR("zmq_msg_send: %s", zmq_strerror(errno));
				zmq_msg_close (&message) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
				if (!more)
					break;      //  Last message part
			}
		}
		
//			if (now - _lastNodeInfoBroadCast > 5000) {
//				broadCastNodeInfo(now);
//				_lastNodeInfoBroadCast = now;
//			}
//			if (now - _lastDeadNodeRemoval > 15000) {
//				removeStaleNodes(now);
//				_lastDeadNodeRemoval = now;
//			}
		_mutex.unlock();
		free(items);
	}
}

void ZeroMQNode::broadCastNodeInfo(uint64_t now) {
	zmq_msg_t infoMsg;
	writeNodeInfo(&infoMsg, Message::NODE_INFO);
	NODE_BROADCAST_MSG(infoMsg);
	zmq_msg_close(&infoMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
}

void ZeroMQNode::removeStaleNodes(uint64_t now) {
	std::map<std::string, boost::shared_ptr<NodeConnection> >::iterator pendingNodeIter = _connTo.begin();
	while(pendingNodeIter != _connTo.end()) {
		if (now - pendingNodeIter->second->startedAt > 30000) {
			// we have been very patient remove pending node
			UM_LOG_ERR("%s could not connect to node at %s - removing", SHORT_UUID(_uuid).c_str(), pendingNodeIter->first.c_str());
//			delete pendingNodeIter->second;
			_connTo.erase(pendingNodeIter++);
		} else {
			pendingNodeIter++;
		}
	}

	std::map<std::string, boost::shared_ptr<NodeConnection> >::iterator connIter = _connFrom.begin();
	while(connIter != _connFrom.end()) {
		if (!UUID::isUUID(connIter->first)) {
			connIter++;
			continue;
		}
		if (now - connIter->second->node.getLastSeen() > 30000) {
			// we have been very patient remove pending node
			UM_LOG_ERR("%s timeout for %s - removing", SHORT_UUID(_uuid).c_str(), SHORT_UUID(connIter->second->node.getUUID()).c_str());
			NodeStub nodeStub = connIter->second->node;
			// Disconnect subscribers
			std::map<std::string, PublisherStub> pubStubs = nodeStub.getPublishers();
			std::map<std::string, PublisherStub>::iterator pubStubIter = pubStubs.begin();
			while(pubStubIter != pubStubs.end()) {
				std::map<std::string, Subscriber>::iterator subIter = _subs.begin();
				while(subIter != _subs.end()) {
					if (subIter->second.matches(pubStubIter->second.getChannelName()))
						subIter->second.removed(pubStubIter->second, nodeStub);
					subIter++;
				}
				pubStubIter++;
			}

			if (connIter->second->address.length())
				_connFrom.erase(connIter->second->address);
//			delete connIter->second;
			_connFrom.erase(connIter++);
		} else {
			connIter++;
		}
	}
}

void ZeroMQNode::disconnectRemoteNode(NodeStub& nodeStub) {
	if (!nodeStub)
		return;

	std::string nodeUUID = nodeStub.getUUID();
	std::map<std::string, PublisherStub> remotePubs = nodeStub.getPublishers();
	std::map<std::string, PublisherStub>::iterator remotePubIter = remotePubs.begin();
	std::map<std::string, Subscriber>::iterator localSubIter = _subs.begin();

	// iterate all remote publishers and remove from local subs
	while (remotePubIter != remotePubs.end()) {
		while (localSubIter != _subs.end()) {
			if(localSubIter->second.matches(remotePubIter->second.getChannelName())) {
				localSubIter->second.removed(remotePubIter->second, nodeStub);
				sendSubRemoved(nodeStub.getUUID().c_str(), localSubIter->second, remotePubIter->second);
			}
			localSubIter++;
		}
		remotePubIter++;
	}

	std::map<std::string, SubscriberStub> remoteSubs = nodeStub.getSubscribers();
	std::map<std::string, SubscriberStub>::iterator remoteSubIter = remoteSubs.begin();
	while(remoteSubIter != remoteSubs.end()) {
		if (_subscriptions.find(remoteSubIter->first) != _subscriptions.end()) {
			Subscription& subscription = _subscriptions[remoteSubIter->first];
			std::map<std::string, Publisher>::iterator confirmedIter = subscription.confirmed.begin();
			while(confirmedIter != subscription.confirmed.end()) {
				confirmedIter->second.removed(subscription.subStub, nodeStub);
				confirmedIter++;
			}
			_subscriptions.erase(remoteSubIter->first);
		} else {
			// could not find any subscriptions
		}
		remoteSubIter++;
	}
}

void ZeroMQNode::processConnectedFrom(const std::string& uuid) {
	// we received a connection from the given uuid
	ScopeLock lock(_mutex);
	
	// let's see whether we already are connected *to* this one
	if (_connTo.find(uuid) != _connTo.end()) {
		_connFrom[uuid] = _connTo[uuid];
	} else {
		boost::shared_ptr<NodeConnection> conn = boost::shared_ptr<NodeConnection>(new NodeConnection());
		conn->node = NodeStub(boost::shared_ptr<NodeStubImpl>(new NodeStubImpl()));
		conn->node.getImpl()->setUUID(uuid);
		_connFrom[uuid] = conn;
	}

	// in any case, mark as connected from and update last seen
	_connFrom[uuid]->connectedFrom = true;
	_connFrom[uuid]->node.updateLastSeen();
}

void ZeroMQNode::processConnectedTo(const std::string& uuid, boost::shared_ptr<NodeConnection> client) {
	assert(client->address.length() > 0);

	// parse client's address back into its constituting parts
	size_t colonPos = client->address.find_last_of(":");
	if(colonPos == std::string::npos || client->address.length() < 6 + 8 + 1 + 1) {
		return;
	}

	std::string transport = client->address.substr(0,3);
	std::string ip = client->address.substr(6, colonPos - 6);
	std::string port = client->address.substr(colonPos + 1, client->address.length() - colonPos + 1);

	// processOpComm must have added this one
	assert(_connTo.find(client->address) != _connTo.end());

	NodeStub nodeStub;

	if (client->node && uuid != client->node.getUUID()) {
		// previous and this uuid of remote node differ - assume that it was replaced
		disconnectRemoteNode(client->node);
		_connTo.erase(client->node.getUUID());
		_connFrom.erase(client->node.getUUID());
		nodeStub = NodeStub(boost::shared_ptr<NodeStubImpl>(new NodeStubImpl()));
		client->refCount = 0;
	}

	// are we already connected *from* this one?
	if (_connFrom.find(uuid) != _connFrom.end()) {
		nodeStub = _connFrom[uuid]->node; // maybe we remembered something about the stub, keep entry
		_connFrom[uuid] = client;   // overwrite with this one
		client->connectedFrom = true; // and remember that we are already connected from
	} else {
		// we know nothing about the remote node yet
		nodeStub = NodeStub(boost::shared_ptr<NodeStubImpl>(new NodeStubImpl()));
	}

	assert(_connTo.find(client->address) != _connTo.end());

	// remember remote node
	client->refCount++;
	client->connectedTo = true;
	client->node = nodeStub;
	client->node.getImpl()->setUUID(uuid);
	client->node.getImpl()->setTransport(transport);
	client->node.getImpl()->setIP(ip);
	client->node.getImpl()->setPort(strTo<uint16_t>(port));
	client->node.updateLastSeen();
	_connTo[uuid] = client;
}

void ZeroMQNode::confirmSub(const std::string& subUUID) {
	if (_subscriptions.find(subUUID) == _subscriptions.end())
		return;

	if (!_subscriptions[subUUID].subStub)
		return;

	if (_subscriptions[subUUID].subStub.getImpl()->implType == Subscriber::ZEROMQ &&
	        !_subscriptions[subUUID].isZMQConfirmed)
		return;

	Subscription& pendSub = _subscriptions[subUUID];

	if (_connFrom.find(pendSub.nodeUUID) != _connFrom.end()) {
		_connFrom[pendSub.nodeUUID]->node.addSubscriber(pendSub.subStub);
	}

	// move all pending subscriptions to confirmed
	std::map<std::string, Publisher>::iterator pendPubIter = pendSub.pending.begin();
	while(pendPubIter != pendSub.pending.end()) {
		pendPubIter->second.added(pendSub.subStub, _connFrom[pendSub.nodeUUID]->node);
		pendSub.confirmed[pendPubIter->first] = pendPubIter->second;
		pendPubIter++;
	}
	pendSub.pending.clear();
}

void ZeroMQNode::writeNodeInfo(zmq_msg_t* msg, Message::Type type) {
	ScopeLock lock(_mutex);

	zmq_msg_init(msg) && UM_LOG_WARN("zmq_msg_init: %s", zmq_strerror(errno));

	size_t pubInfoSize = 0;
	std::map<std::string, Publisher>::iterator pubIter =_pubs.begin();
	while(pubIter != _pubs.end()) {
		pubInfoSize += PUB_INFO_SIZE(pubIter->second);
		pubIter++;
	}

	zmq_msg_init_size (msg, 4 + _uuid.length() + 1 + pubInfoSize) && UM_LOG_WARN("zmq_msg_init_size: %s",zmq_strerror(errno));
	char* writeBuffer = (char*)zmq_msg_data(msg);
	char* writePtr = writeBuffer;

	// version and type
	writePtr = writeVersionAndType(writePtr, type);
	assert(writePtr - writeBuffer == 4);

	// local uuid
	writePtr = writeString(writePtr, _uuid.c_str(), _uuid.length());
	assert(writePtr - writeBuffer == 4 + _uuid.length() + 1);

	pubIter =_pubs.begin();
	while(pubIter != _pubs.end()) {
		writePtr = writePubInfo(writePtr, pubIter->second);
		pubIter++;
	}

	assert(writePtr - writeBuffer == zmq_msg_size(msg));
}

void ZeroMQNode::processNodeInfo(char* recvBuffer, size_t msgSize) {
	char* readPtr = recvBuffer;

	if(REMAINING_BYTES_TOREAD < 36)
		return;

	char* from;
	readPtr = readString(readPtr, from, 37);

	if (_connTo.find(from) == _connTo.end()) {
		UM_LOG_WARN("%s not caring for nodeinfo from %s - not connected", SHORT_UUID(_uuid).c_str(), from);
		return;
	}

	std::set<std::string> pubUUIDs;
	while(REMAINING_BYTES_TOREAD > 37) {
		uint16_t port;
		char* channelName;
		char* pubUUID;
		uint16_t type;
		readPtr = readPubInfo(readPtr, type, port, channelName, pubUUID);
		processRemotePubAdded(from, port, type, channelName, pubUUID);
	}
}

void ZeroMQNode::processRemotePubAdded(char* nodeUUID,
                                       uint16_t port,
                                       uint16_t type,
                                       char* channelName,
                                       char* pubUUID) {
	if (_connTo.find(nodeUUID) == _connTo.end())
		return;

	NodeStub nodeStub = _connTo[nodeUUID]->node;
	nodeStub.updateLastSeen();

	PublisherStub pubStub(boost::shared_ptr<PublisherStubImpl>(new PublisherStubImpl()));
	pubStub.getImpl()->setUUID(pubUUID);
	pubStub.getImpl()->setPort(port);
	pubStub.getImpl()->setChannelName(channelName);
	pubStub.getImpl()->setDomain(nodeUUID);

	pubStub.getImpl()->setInProcess(nodeStub.isInProcess());
	pubStub.getImpl()->setRemote(nodeStub.isRemote());
	pubStub.getImpl()->setIP(nodeStub.getIP());
	pubStub.getImpl()->setTransport(nodeStub.getTransport());
	pubStub.getImpl()->implType = type;

	nodeStub.getImpl()->addPublisher(pubStub);

	std::map<std::string, Subscriber>::iterator subIter = _subs.begin();
	while(subIter != _subs.end()) {
		if (subIter->second.getImpl()->implType == type && subIter->second.matches(channelName)) {
			subIter->second.added(pubStub, nodeStub);
			sendSubAdded(nodeUUID, subIter->second, pubStub);
		}
		subIter++;
	}
}

void ZeroMQNode::processRemotePubRemoved(char* nodeUUID,
        uint16_t port,
        uint16_t type,
        char* channelName,
        char* pubUUID) {
	if (_connTo.find(nodeUUID) == _connTo.end())
		return;

	NodeStub nodeStub = _connTo[nodeUUID]->node;
	PublisherStub pubStub = nodeStub.getPublisher(pubUUID);

	if (!pubStub)
		return;

	std::map<std::string, Subscriber>::iterator subIter = _subs.begin();
	while(subIter != _subs.end()) {
		if (subIter->second.getImpl()->implType == type && subIter->second.matches(channelName)) {
			subIter->second.removed(pubStub, nodeStub);
			sendSubRemoved(nodeUUID, subIter->second, pubStub);
		}
		subIter++;
	}
	nodeStub.removePublisher(pubStub);
}

void ZeroMQNode::sendSubAdded(const char* nodeUUID, const Subscriber& sub, const PublisherStub& pub) {
	COMMON_VARS;

	if (_connTo.find(nodeUUID) == _connTo.end())
		return;

	void* clientSocket = _connTo[nodeUUID]->socket;
	if (!clientSocket)
		return;

	UM_LOG_INFO("Sending sub added for %s on %s to publisher %s",
	            sub.getChannelName().c_str(), SHORT_UUID(sub.getUUID()).c_str(), SHORT_UUID(pub.getUUID()).c_str());

	size_t bufferSize = 4 + SUB_INFO_SIZE(sub) + PUB_INFO_SIZE(pub);
	PREPARE_MSG(subAddedMsg, bufferSize);

	writePtr = writeVersionAndType(writePtr, Message::SUBSCRIBE);
	writePtr = writeSubInfo(writePtr, sub);
	writePtr = writePubInfo(writePtr, pub);
	assert(writePtr - writeBuffer == bufferSize);

	zmq_msg_send(&subAddedMsg, clientSocket, ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_msg_send: %s", zmq_strerror(errno));
	_buckets.back().nrMetaMsgSent++;
	_buckets.back().sizeMetaMsgSent += bufferSize;

	zmq_msg_close(&subAddedMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));
}

void ZeroMQNode::sendSubRemoved(const char* nodeUUID, const Subscriber& sub, const PublisherStub& pub) {
	COMMON_VARS;

	if (_connTo.find(nodeUUID) == _connTo.end())
		return;

	void* clientSocket = _connTo[nodeUUID]->socket;
	if (!clientSocket)
		return;

	UM_LOG_INFO("Sending sub removed for %s on %s to publisher %s",
	            sub.getChannelName().c_str(), SHORT_UUID(sub.getUUID()).c_str(), SHORT_UUID(pub.getUUID()).c_str());

	size_t bufferSize = 4 + SUB_INFO_SIZE(sub) + PUB_INFO_SIZE(pub);
	PREPARE_MSG(subRemovedMsg, bufferSize);

	writePtr = writeVersionAndType(writePtr, Message::UNSUBSCRIBE);
	writePtr = writeSubInfo(writePtr, sub);
	writePtr = writePubInfo(writePtr, pub);
	assert(writePtr - writeBuffer == bufferSize);

	zmq_msg_send(&subRemovedMsg, clientSocket, ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_msg_send: %s", zmq_strerror(errno));
	_buckets.back().nrMetaMsgSent++;
	_buckets.back().sizeMetaMsgSent += bufferSize;

	zmq_msg_close(&subRemovedMsg) && UM_LOG_ERR("zmq_msg_close: %s", zmq_strerror(errno));

}

char* ZeroMQNode::writePubInfo(char* buffer, const PublisherStub& pub) {
	std::string channel = pub.getChannelName();
	std::string uuid = pub.getUUID(); // we share a publisher socket in the node, do not publish hidden pub uuid
	uint16_t port = _pubPort; //pub.getPort();
	uint16_t type = pub.getImpl()->implType;

	assert(uuid.length() == 36);

	char* start = buffer;
	(void)start; // surpress unused warning wiithout assert

	buffer = writeString(buffer, channel.c_str(), channel.length());
	buffer = writeString(buffer, uuid.c_str(), uuid.length());
	buffer = writeUInt16(buffer, type);
	buffer = writeUInt16(buffer, port);

	assert(buffer - start == PUB_INFO_SIZE(pub));
	return buffer;
}

char* ZeroMQNode::readPubInfo(char* buffer, uint16_t& type, uint16_t& port, char*& channelName, char*& uuid) {
	char* start = buffer;
	(void)start; // surpress unused warning without assert

	buffer = readString(buffer, channelName, 4096);
	buffer = readString(buffer, uuid, 37);
	buffer = readUInt16(buffer, type);
	buffer = readUInt16(buffer, port);

	return buffer;
}

char* ZeroMQNode::writeSubInfo(char* buffer, const Subscriber& sub) {
	std::string channel = sub.getChannelName();
	std::string uuid = sub.getUUID(); // we share a publisher socket in the node, do not publish hidden pub uuid
	uint16_t type = sub.getImpl()->implType;

	assert(uuid.length() == 36);

	char* start = buffer;
	(void)start; // surpress unused warning without assert

	buffer = writeString(buffer, channel.c_str(), channel.length());
	buffer = writeString(buffer, uuid.c_str(), uuid.length());
	buffer = writeUInt16(buffer, type);

	assert(buffer - start == SUB_INFO_SIZE(sub));
	return buffer;
}

char* ZeroMQNode::readSubInfo(char* buffer, uint16_t& type, char*& channelName, char*& uuid) {
	char* start = buffer;
	(void)start; // surpress unused warning without assert

	buffer = readString(buffer, channelName, 4096);
	buffer = readString(buffer, uuid, 37);
	buffer = readUInt16(buffer, type);

	return buffer;
}

char* ZeroMQNode::writeVersionAndType(char* buffer, Message::Type type) {
	buffer = writeUInt16(buffer, Message::VERSION);
	buffer = writeUInt16(buffer, type);
	return buffer;
}

char* ZeroMQNode::readVersionAndType(char* buffer, uint16_t& version, Message::Type& type) {
	// clear out of type bytes
	memset(&type, 0, sizeof(type));

	buffer = readUInt16(buffer, version);
	buffer = readUInt16(buffer, (uint16_t&)type);
	return buffer;
}

char* ZeroMQNode::writeString(char* buffer, const char* content, size_t length) {
	memcpy(buffer, content, length);
	buffer += length;
	buffer[0] = 0;
	buffer += 1;
	return buffer;
}

char* ZeroMQNode::readString(char* buffer, char*& content, size_t maxLength) {
	if (strnlen(buffer, maxLength) < maxLength) {
		content = buffer;
		buffer += strlen(content) + 1;
	} else {
		content = NULL;
	}
	return buffer;
}

char* ZeroMQNode::writeUInt16(char* buffer, uint16_t value) {
	*(uint16_t*)(buffer) = htons(value);
	buffer += 2;
	return buffer;
}

char* ZeroMQNode::readUInt16(char* buffer, uint16_t& value) {
	value = ntohs(*(uint16_t*)(buffer));
	buffer += 2;
	return buffer;
}

ZeroMQNode::StatBucket<double> ZeroMQNode::accumulateIntoBucket() {
	StatBucket<double> statBucket;
	
	double rollOffFactor = 0.3;

	std::list<StatBucket<size_t> >::iterator buckFrameStart = _buckets.begin();
	std::list<StatBucket<size_t> >::iterator buckFrameEnd = _buckets.begin();
	std::map<std::string, size_t>::iterator chanIter;

	while(buckFrameEnd != _buckets.end()) {
		if (buckFrameEnd->timeStamp - 1000 < buckFrameStart->timeStamp) {
			// we do not yet have a full second
			buckFrameEnd++;
			continue;
		}
		
		// accumulate stats for a second
		StatBucket<size_t> oneSecBucket;
		std::list<StatBucket<size_t> >::iterator curr = buckFrameStart;
		while(curr != buckFrameEnd) {
			oneSecBucket.nrMetaMsgRcvd += curr->nrMetaMsgRcvd;
			oneSecBucket.nrMetaMsgSent += curr->nrMetaMsgSent;
			oneSecBucket.sizeMetaMsgRcvd += curr->sizeMetaMsgRcvd;
			oneSecBucket.sizeMetaMsgSent += curr->sizeMetaMsgSent;
			
			chanIter = curr->nrChannelMsg.begin();
			while (chanIter != curr->nrChannelMsg.end()) {
				oneSecBucket.nrChannelMsg[chanIter->first] += chanIter->second;
				chanIter++;
			}
			chanIter = curr->sizeChannelMsg.begin();
			while (chanIter != curr->sizeChannelMsg.end()) {
				oneSecBucket.sizeChannelMsg[chanIter->first] += chanIter->second;
				chanIter++;
			}
			curr++;
		}

		statBucket.nrMetaMsgSent = (1 - rollOffFactor) * statBucket.nrMetaMsgSent + rollOffFactor * oneSecBucket.nrMetaMsgSent;
		statBucket.nrMetaMsgRcvd = (1 - rollOffFactor) * statBucket.nrMetaMsgRcvd + rollOffFactor * oneSecBucket.nrMetaMsgRcvd;
		statBucket.sizeMetaMsgSent = (1 - rollOffFactor) * statBucket.sizeMetaMsgSent + rollOffFactor * oneSecBucket.sizeMetaMsgSent;
		statBucket.sizeMetaMsgRcvd = (1 - rollOffFactor) * statBucket.sizeMetaMsgRcvd + rollOffFactor * oneSecBucket.sizeMetaMsgRcvd;
		
		chanIter = oneSecBucket.sizeChannelMsg.begin();
		while (chanIter != oneSecBucket.sizeChannelMsg.end()) {
			statBucket.sizeChannelMsg[chanIter->first] = (1 - rollOffFactor) * statBucket.sizeChannelMsg[chanIter->first] + rollOffFactor * chanIter->second;;
			chanIter++;
		}

		chanIter = oneSecBucket.nrChannelMsg.begin();
		while (chanIter != oneSecBucket.nrChannelMsg.end()) {
			statBucket.nrChannelMsg[chanIter->first] = (1 - rollOffFactor) * statBucket.nrChannelMsg[chanIter->first] + rollOffFactor * chanIter->second;;
			chanIter++;
		}

		buckFrameStart++;
	}

	return statBucket;
}
	
void ZeroMQNode::replyWithDebugInfo(const std::string uuid) {
	ScopeLock lock(_mutex);

	StatBucket<double> statBucket = accumulateIntoBucket();

	// return to sender
	zmq_send(_nodeSocket, uuid.c_str(), uuid.length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
	zmq_send(_nodeSocket, "", 0, ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));

	std::stringstream ss;

	// first identify us
	ss << "uuid:" << _uuid;
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
	RESETSS(ss);

	ss << "host:" << hostUUID;
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
	RESETSS(ss);

#ifdef APPLE
	ss << "os: MacOSX";
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
#elif defined CYGWIN
	ss << "os: Cygwin";
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
#elif defined ANDROID
	ss << "os: Android";
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
#elif defined IOS
	ss << "os: iOS";
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
#elif defined UNIX
	ss << "os: Unix";
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
#elif defined WIN32
	ss << "os: Windows";
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
#endif
	RESETSS(ss);

	ss << "proc:" << procUUID;
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
	RESETSS(ss);

	// calculate performance
	
	ss << "sent:msgs:" << statBucket.nrMetaMsgSent;
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
	RESETSS(ss);

	ss << "sent:bytes:" << statBucket.sizeMetaMsgSent;
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
	RESETSS(ss);

	ss << "rcvd:msgs:" << statBucket.nrMetaMsgRcvd;
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
	RESETSS(ss);
	
	ss << "rcvd:bytes:" << statBucket.sizeMetaMsgRcvd;
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
	RESETSS(ss);

	// send our publishers
	std::map<std::string, Publisher>::iterator pubIter = _pubs.begin();
	while (pubIter != _pubs.end()) {
		
		ss << "pub:uuid:" << pubIter->first;
		zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
		RESETSS(ss);

		ss << "pub:channelName:" << pubIter->second.getChannelName();
		zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
		RESETSS(ss);

		ss << "pub:type:" << pubIter->second.getImpl()->implType;
		zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
		RESETSS(ss);

//		std::cout << pubIter->second.getChannelName() << std::endl;
//		std::cout << statBucket.nrChannelMsg[pubIter->second.getChannelName()] << std::endl;

		ss << "pub:sent:msgs:" << statBucket.nrChannelMsg[pubIter->second.getChannelName()];
		zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
		RESETSS(ss);

		ss << "pub:sent:bytes:" << statBucket.sizeChannelMsg[pubIter->second.getChannelName()];
		zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
		RESETSS(ss);
		
		std::map<std::string, SubscriberStub> subs = pubIter->second.getSubscribers();
		std::map<std::string, SubscriberStub>::iterator subIter = subs.begin();

		// send its subscriptions as well
		while (subIter != subs.end()) {
			ss << "pub:sub:uuid:" << subIter->first;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "pub:sub:channelName:" << subIter->second.getChannelName();
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "pub:sub:type:" << subIter->second.getImpl()->implType;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			subIter++;
		}
		pubIter++;
	}

	// send our subscribers
	std::map<std::string, Subscriber>::iterator subIter = _subs.begin();
	while (subIter != _subs.end()) {
		ss << "sub:uuid:" << subIter->first;
		zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
		RESETSS(ss);

		ss << "sub:channelName:" << subIter->second.getChannelName();
		zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
		RESETSS(ss);

		ss << "sub:type:" << subIter->second.getImpl()->implType;
		zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
		RESETSS(ss);

		std::map<std::string, PublisherStub> pubs = subIter->second.getPublishers();
		std::map<std::string, PublisherStub>::iterator pubIter = pubs.begin();
		// send all remote publishers we think this node has
		while (pubIter != pubs.end()) {
			ss << "sub:pub:uuid:" << pubIter->first;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "sub:pub:channelName:" << pubIter->second.getChannelName();
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "sub:pub:type:" << pubIter->second.getImpl()->implType;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			pubIter++;
		}

		subIter++;
	}


	// send all the nodes we know about
	std::map<std::string, boost::shared_ptr<NodeConnection> > connections;
	std::map<std::string, boost::shared_ptr<NodeConnection> >::iterator nodeIter;

	// insert connection to
	nodeIter = _connTo.begin();
	while (nodeIter != _connTo.end()) {
		connections.insert(*nodeIter);
		nodeIter++;
	}

	// insert connection to
	nodeIter = _connFrom.begin();
	while (nodeIter != _connFrom.end()) {
		if (connections.find(nodeIter->first) != connections.end()) {
			assert(connections[nodeIter->first] == nodeIter->second);
		}
		connections.insert(*nodeIter);
		nodeIter++;
	}

	nodeIter = connections.begin();
	while (nodeIter != connections.end()) {
		// only report UUIDs as keys
		if (UUID::isUUID(nodeIter->first)) {
			ss << "conn:uuid:" << nodeIter->first;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "conn:address:" << nodeIter->second->address;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "conn:from:" << nodeIter->second->connectedFrom;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "conn:to:" << nodeIter->second->connectedTo;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "conn:confirmed:" << nodeIter->second->isConfirmed;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "conn:refCount:" << nodeIter->second->refCount;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			ss << "conn:startedAt:" << nodeIter->second->startedAt;
			zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
			RESETSS(ss);

			// send info about all nodes connected to us
			if (nodeIter->second->node) {
				NodeStub& nodeStub = nodeIter->second->node;

				ss << "conn:lastSeen:" << nodeStub.getLastSeen();
				zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
				RESETSS(ss);

				std::map<std::string, PublisherStub> pubs = nodeStub.getPublishers();
				std::map<std::string, PublisherStub>::iterator pubIter = pubs.begin();
				// send all remote publishers we think this node has
				while (pubIter != pubs.end()) {
					ss << "conn:pub:uuid:" << pubIter->first;
					zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
					RESETSS(ss);

					ss << "conn:pub:channelName:" << pubIter->second.getChannelName();
					zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
					RESETSS(ss);

					ss << "conn:pub:type:" << pubIter->second.getImpl()->implType;
					zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
					RESETSS(ss);

					pubIter++;
				}

				std::map<std::string, SubscriberStub> subs = nodeStub.getSubscribers();
				std::map<std::string, SubscriberStub>::iterator subIter = subs.begin();
				// send all remote publishers we think this node has
				while (subIter != subs.end()) {
					ss << "conn:sub:uuid:" << subIter->first;
					zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
					RESETSS(ss);

					ss << "conn:sub:channelName:" << subIter->second.getChannelName();
					zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
					RESETSS(ss);

					ss << "conn:sub:type:" << subIter->second.getImpl()->implType;
					zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_SNDMORE | ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
					RESETSS(ss);

					subIter++;
				}

			}
		}
		nodeIter++;
	}

	ss << "done:" << _uuid;
	zmq_send(_nodeSocket, ss.str().c_str(), ss.str().length(), ZMQ_DONTWAIT) == -1 && UM_LOG_ERR("zmq_send: %s", zmq_strerror(errno));
	RESETSS(ss);

}

ZeroMQNode::NodeConnection::NodeConnection()
	: connectedTo(false), connectedFrom(false), socket(NULL), startedAt(0), refCount(0), isConfirmed(false) {}

ZeroMQNode::NodeConnection::NodeConnection(const std::string& _address,
        const std::string& thisUUID)
	: connectedTo(false), connectedFrom(false), address(_address), refCount(0), isConfirmed(false) {
	startedAt	= Thread::getTimeStampMs();
	socketId = thisUUID;
	socket = zmq_socket(ZeroMQNode::getZeroMQContext(), ZMQ_DEALER);
	if (!socket) {
		UM_LOG_ERR("zmq_socket: %s", zmq_strerror(errno));
	} else {
		zmq_setsockopt(socket, ZMQ_IDENTITY, socketId.c_str(), socketId.length()) && UM_LOG_ERR("zmq_setsockopt: %s", zmq_strerror(errno));
		int err = zmq_connect(socket, address.c_str());
		if (err) {
			UM_LOG_ERR("zmq_connect %s: %s", address.c_str(), zmq_strerror(errno))
			zmq_close(socket);
			socket = NULL;
		}
	}
}

ZeroMQNode::NodeConnection::~NodeConnection() {
	if (socket) {
		zmq_disconnect(socket, address.c_str());
		zmq_close(socket);
	}
}

ZeroMQNode::Subscription::Subscription() :
	isZMQConfirmed(false),
	startedAt(Thread::getTimeStampMs()) {}

}
