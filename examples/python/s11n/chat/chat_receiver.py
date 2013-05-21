#!/usr/bin/python
# ^^ make sure the interpreter is the one used while building!

import sys
import time

sys.path.append("../../../../build/lib") # set to wherever your umundo libraries are
sys.path.append("./generated/") # compiled protobuf
sys.path.append("../../../../s11n/src/umundo-python") # s11n python binding
try:
	import umundo64 as umundo
except:
	try:
		import umundo
	except:
		try:
			import umundo64_d as umundo 
		except:
			import umundo_d as umundo	
	
	
import umundoS11n
from ChatS11N_pb2 import ChatMsg


class TestReceiver(umundo.Receiver):
    def receive(self, *args):
        sys.stdout.write("i")
        sys.stdout.flush()

testRcv = TestReceiver()
chatSub = umundoS11n.TypedSubscriber("s11nChat", testRcv)

node = umundo.Node()
node.addSubscriber(chatSub)

while True:
	time.sleep(10)