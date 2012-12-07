#include "umundo/core.h"
#include "umundo/util.h"
#include <iostream>
#include <stdio.h>

#define BUFFER_SIZE 1024*1024

using namespace umundo;
using namespace std;

static int nrReceptions = 0;
static int bytesRecvd = 0;
static int nrMissing = 0;
static string hostId;

class TestReceiver : public Receiver {
	void receive(Message* msg) {
		// std::cout << "md5: '" << msg->getMeta("md5") << "'" << std::endl;
		// std::cout << "md5: '" << md5(msg->data(), msg->size()) << "'" << std::endl;
		// std::cout << "equals: " << msg->getMeta("md5").compare(md5(msg->data(), msg->size())) << std::endl;
		if (msg->size() > 0)
			assert(msg->getMeta("md5").compare(md5(msg->data(), msg->size())) == 0);
		if (nrReceptions + nrMissing == strTo<int>(msg->getMeta("seq"))) {
			std::cout << ".";
		} else {
			std::cout << "R" << strTo<int>(msg->getMeta("seq"));
		}
		while (nrReceptions + nrMissing < strTo<int>(msg->getMeta("seq"))) {
			nrMissing++;
			std::cout << "F" << nrReceptions + nrMissing;
		}
		nrReceptions++;
		bytesRecvd += msg->size();
	}
};

bool testMessageTransmission() {
	hostId = Host::getHostId();
	for (int i = 0; i < 2; i++) {
		nrReceptions = 0;
		nrMissing = 0;
		bytesRecvd = 0;

		Node pubNode(hostId + "foo");
		Publisher pub("foo");
		pubNode.addPublisher(pub);

		TestReceiver* testRecv = new TestReceiver();
		Node subNode(hostId + "foo");
		Subscriber sub("foo", testRecv);
		sub.setReceiver(testRecv);
		subNode.addSubscriber(sub);

		pub.waitForSubscribers(1);
		assert(pub.waitForSubscribers(0) == 1);

		int iterations = 1000;

		for (int j = 0; j < iterations; j++) {
			Message* msg = new Message();
			msg->putMeta("seq",toStr(j));
			pub.send(msg);
			delete msg;
		}

		// wait until all messages are delivered
		Thread::sleepMs(500);

		// sometimes there is some weird latency
		for (int i = 0; i < 5; i++) {
			if (nrReceptions < iterations)
				Thread::sleepMs(2000);
		}

		std::cout << "expected " << iterations << " messages, received " << nrReceptions << std::endl;
		assert(nrReceptions == iterations);

		subNode.removeSubscriber(sub);
		pubNode.removePublisher(pub);

	}
	return true;
}

bool testDataTransmission() {
	hostId = Host::getHostId();
	for (int i = 0; i < 2; i++) {
		nrReceptions = 0;
		nrMissing = 0;
		bytesRecvd = 0;

		Node pubNode(hostId + "foo");
		Publisher pub("foo");
		pubNode.addPublisher(pub);

		TestReceiver* testRecv = new TestReceiver();
		Node subNode(hostId + "foo");
		Subscriber sub("foo", testRecv);
		sub.setReceiver(testRecv);
		subNode.addSubscriber(sub);

		pub.waitForSubscribers(1);
		assert(pub.waitForSubscribers(0) == 1);

		char* buffer = (char*)malloc(BUFFER_SIZE);
		memset(buffer, 40, BUFFER_SIZE);

		int iterations = 1000;

		for (int j = 0; j < iterations; j++) {
			Message* msg = new Message(Message(buffer, BUFFER_SIZE));
			msg->putMeta("md5", md5(buffer, BUFFER_SIZE));
			msg->putMeta("seq",toStr(j));
			pub.send(msg);
			delete msg;
		}

		// wait until all messages are delivered
		Thread::sleepMs(500);

		// sometimes there is some weird latency
		for (int i = 0; i < 5; i++) {
			if (nrReceptions < iterations)
				Thread::sleepMs(2000);
		}

		std::cout << "expected " << iterations << " messages, received " << nrReceptions << std::endl;
		assert(nrReceptions == iterations);
		assert(bytesRecvd == nrReceptions * BUFFER_SIZE);

		subNode.removeSubscriber(sub);
		pubNode.removePublisher(pub);

	}
	return true;
}


int main(int argc, char** argv, char** envp) {
	if (!testMessageTransmission())
		return EXIT_FAILURE;
	if (!testDataTransmission())
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
