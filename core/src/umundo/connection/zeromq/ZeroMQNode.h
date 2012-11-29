/**
 *  @file
 *  @brief      Node implementation with 0MQ.
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

#ifndef ZEROMQDISPATCHER_H_XFMTSVLV
#define ZEROMQDISPATCHER_H_XFMTSVLV

#include <zmq.h>

#include "umundo/common/Common.h"
#include "umundo/thread/Thread.h"
#include "umundo/common/ResultSet.h"
#include "umundo/connection/Node.h"
#include "umundo/discovery/NodeQuery.h"

/// Initialize a zeromq message with a given size
#define ZMQ_PREPARE(msg, size) \
zmq_msg_init(&msg) && LOG_WARN("zmq_msg_init: %s", zmq_strerror(errno)); \
zmq_msg_init_size (&msg, size) && LOG_WARN("zmq_msg_init_size: %s",zmq_strerror(errno));

/// Initialize a zeromq message with a given size and memcpy data
#define ZMQ_PREPARE_DATA(msg, data, size) \
zmq_msg_init(&msg) && LOG_WARN("zmq_msg_init: %s", zmq_strerror(errno)); \
zmq_msg_init_size (&msg, size) && LOG_WARN("zmq_msg_init_size: %s",zmq_strerror(errno)); \
memcpy(zmq_msg_data(&msg), data, size);

/// Initialize a zeromq message with a given null-terminated string
#define ZMQ_PREPARE_STRING(msg, data, size) \
zmq_msg_init(&msg) && LOG_WARN("zmq_msg_init: %s", zmq_strerror(errno)); \
zmq_msg_init_size (&msg, size + 1) && LOG_WARN("zmq_msg_init_size: %s",zmq_strerror(errno)); \
memcpy(zmq_msg_data(&msg), data, size + 1);

/// Size of a publisher info on wire
#define PUB_INFO_SIZE(pub) \
pub.getChannelName().length() + 1 + pub.getUUID().length() + 1 + 2

/// Size of a subcriber info on wire
#define SUB_INFO_SIZE(sub) \
sub.getChannelName().length() + 1 + sub.getUUID().length() + 1

/// send a message to break a zmq_poll and alter some socket
#define ZMQ_INTERNAL_SEND(command, parameter) \
zmq_msg_t socketOp; \
ZMQ_PREPARE_STRING(socketOp, command, strlen(command)); \
zmq_sendmsg(_writeOpSocket, &socketOp, ZMQ_SNDMORE) >= 0 || LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno)); \
zmq_msg_close(&socketOp) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno)); \
zmq_msg_t endPointOp; \
ZMQ_PREPARE_STRING(endPointOp, parameter, strlen(parameter)); \
zmq_sendmsg(_writeOpSocket, &endPointOp, 0) >= 0 || LOG_WARN("zmq_sendmsg: %s",zmq_strerror(errno)); \
zmq_msg_close(&endPointOp) && LOG_WARN("zmq_msg_close: %s",zmq_strerror(errno));

namespace umundo {

class PublisherStub;
class ZeroMQPublisher;
class ZeroMQSubscriber;
class NodeQuery;

/**
 * Concrete node implementor for 0MQ (bridge pattern).
 */
class DLLEXPORT ZeroMQNode : public Thread, public ResultSet<NodeStub>, public NodeImpl {
public:
	virtual ~ZeroMQNode();

	/** @name Implementor */
	//@{
	shared_ptr<Implementation> create();
	void init(shared_ptr<Configuration>);
	void suspend();
	void resume();
	//@}

	/** @name Publish / Subscriber Maintenance */
	//@{
	void addSubscriber(Subscriber&);
	void removeSubscriber(Subscriber&);
	void addPublisher(Publisher&);
	void removePublisher(Publisher&);
	//@}

	/** @name Callbacks from Discovery */
	//@{
	void added(NodeStub);    ///< A node was added, connect to its router socket and list our publishers.
	void removed(NodeStub);  ///< A node was removed, notify local subscribers and clean up.
	void changed(NodeStub);  ///< Never happens.
	//@}

	/** @name uMundo deployment object model */
	//@{
//	set<NodeStub*> getAllNodes();
	//@}

  static uint16_t bindToFreePort(void* socket, const std::string& transport, const std::string& address);
	static void* getZeroMQContext();

protected:
	ZeroMQNode();

	void run(); ///< see Thread

	/** @name Remote publisher / subscriber maintenance */
	//@{
  void sendPubRemoved(void* socket, const umundo::Publisher& pub, bool hasMore);
  void sendPubAdded(void* socket, const umundo::Publisher& pub, bool hasMore);
  void sendSubAdded(void* socket, const umundo::Subscriber& sub, const umundo::PublisherStub& pub, bool hasMore);
  void sendSubRemoved(void* socket, const umundo::Subscriber& sub, const umundo::PublisherStub& pub, bool hasMore);

  void processNodeCommPubAdded(const std::string& nodeUUID, const umundo::PublisherStub& pubStub);
  void processNodeCommPubRemoval(const std::string& nodeUUID, const umundo::PublisherStub& pubStub);
  void confirmPubAdded(umundo::NodeStub& nodeStub, const umundo::PublisherStub& pubStub);

  void processZMQCommSubscriptions(const std::string channel);
  void processNodeCommSubscriptions(const umundo::NodeStub& nodeStub, const umundo::SubscriberStub& subStub);
  void processNodeCommUnsubscriptions(umundo::NodeStub& nodeStub, const umundo::SubscriberStub& subStub);
  bool confirmSubscription(const umundo::NodeStub& nodeStub, const umundo::SubscriberStub& subStub);
	//@}

  /** @name Read / Write to raw byte arrays */
	//@{
  char* writePubInfo(char* buffer, const umundo::PublisherStub& pub);
  char* readPubInfo(char* buffer, uint16_t& port, char*& channelName, char*& uuid);
  char* writeSubInfo(char* buffer, const umundo::Subscriber& sub);
  char* readSubInfo(char* buffer, char*& channelName, char*& uuid);

	//@}

private:
	static void* _zmqContext; ///< global 0MQ context.
  
  void* _pubSocket; ///< public socket for outgoing external communication
  void* _subSocket; ///< umundo internal socket to receive publications from publishers
  void* _nodeSocket; ///< socket for node meta information
  void* _readOpSocket;
  void* _writeOpSocket;
  uint16_t _pubPort; ///< port of our public publisher socket

	shared_ptr<NodeQuery> _nodeQuery; ///< the NodeQuery which we registered for Discovery.
	Mutex _mutex;

	map<string, NodeStub> _nodes; ///< UUIDs to other NodeStub%s - at runtime we store their known Publishera as stubs, but do not know about subscribers.
	map<string, void*> _sockets;  ///< UUIDs to ZeroMQ Node Sockets.

  std::multiset<std::string> _subscriptions; ///< subscriptions as we received them from zeromq
  std::set<std::string> _confirmedSubscribers; ///< subscriptions as we received them from zeromq and confirmed by node communication
  std::multimap<std::string, std::pair<long, std::pair<umundo::NodeStub, umundo::SubscriberStub> > > _pendingSubscriptions; ///< channel to timestamped pending subscribers we got from node communication
  std::map<std::string, std::map<std::string, umundo::PublisherStub> > _pendingRemotePubs; ///< publishers of yet undiscovered nodes.
  
	shared_ptr<NodeConfig> _config;

	friend class Factory;
};

}

#endif /* end of include guard: ZEROMQDISPATCHER_H_XFMTSVLV */
