# Prez

Prez is a distributed system that provides highly reliable, consistent store. It enables reliable distributed coordination. Prez is motivated by systems like [Apache Zookeeper][zookeeper] and [etcd][etcd]. It is written in C and uses [Raft][raft] consensus algorithm internally for replicated state machine.

[zookeeper]: http://zookeeper.apache.org/
[etcd]: https://github.com/coreos/etcd
[raft]: http://raftconsensus.github.io/

Prez is basically forked from [Redis][redis] and so you will notice number of similarities with it. Prez uses RESP (Redis Serialization Protocol) for client interaction. Hence, redis-cli utility can be used with Prez.

[redis]: https://github.com/antirez/redis

### Building
To compile prez, you can clone the repository and build,

```sh
git clone https://github.com/srned/Prez.git
cd Prez
make
```

This will generate a binary named `./src/prez-server`

### Running
To create a prez cluster, a minimal prez configuration file should contain the below which can found at `./prez.conf`,

```sh
name node-9000
port 9000
cluster-port 10000
cluster-config-file nodes.conf
cluster-node-timeout 5000
loglevel debug
```

port 9000 will be the port used for client communication and cluster-port 10000 will be the port used for internal cluster communication.  

For proper functioning, a cluster should have a minimum of 3 nodes.

nodes.conf contains |node-name, ip:port| for the nodes in the cluster. A sample nodes.conf for cluster of 5 nodes that are on the same machine would be,

```sh
node-9000 127.0.0.1:10000
node-9001 127.0.0.1:10001
node-9002 127.0.0.1:10002
node-9003 127.0.0.1:10003
node-9004 127.0.0.1:10004
```

Let us create a cluster of 5 nodes. To do so, enter a new directory, and create the following directories named after the port number of the instance we'll run inside any given directory.
like:

```sh
mkdir cluster-test
cd cluster-test
mkdir 9000 9001 9002 9003 9004
```

Create a prez.conf file inside each of the directories, from 9000 to 9004. As a template for your configuration file just use the small example above. Remember to update communication and cluster ports accordingly.  
Also, create nodes.conf file in each of the directory with the above example.  
Now copy your prez-server executable into the cluster-test directory, and open 6 terminal tabs.  
Start each of the instances like,

```sh
cd 9000
../prez-server ./prez.conf
```

Once started, you should see the below at the leader,

```sh
33503: 22 Jan 23:54:15.763 . Sending heartbeat to port
```

and, below at the followers for each heartbeat,
```sh
33504: 22 Jan 23:57:28.390 . --- Received AppendEntries term 11, name: node-9003, logindex 15, leadercommit 0
```

### Testing
To test the cluster, we will simply use [redis-cli][rediscli]
A sample interaction where node-9000 is the leader,

```sh
○  src |unstable| ✗ → ./redis-cli -p 9000
127.0.0.1:9000> set key value
OK
(0.67s)
127.0.0.1:9000> set key1 value1
OK
127.0.0.1:9000> get key
"value"
127.0.0.1:9000> get key1
"value1"
127.0.0.1:9000>
○ src |unstable| ✗ → ./redis-cli -p 9001
127.0.0.1:9001> get key
"value"
127.0.0.1:9001> get key1
"value1"
127.0.0.1:9001> set key3 value3
(error) ASK node-9000
```

[rediscli]: http://redis.io/download

### Status
The project is in development. Currently, it performs Raft leader election and log replication. Cluster membership changes and log compaction is pending.

Happy Coding!
