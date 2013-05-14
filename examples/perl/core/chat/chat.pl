#!/opt/local/bin/perl
# ^^ make sure the interpreter is the one used while building!

# set to wherever your umundo libraries are
use lib '../../../../build/cli/lib';
use umundo64;

$node = umundoNativePerl64::Node::new();
$pub = umundoNativePerl64::Publisher::new("chat");
$sub = umundoNativePerl64::Subscriber::new("chat");

# TODO: actually implement the chat
sleep 10;