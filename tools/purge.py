#!/usr/bin/python
import pika
import atexit
import sys

if len(sys.argv) < 3:
    print 'usage:', sys.argv[0], '<host>', '<queue>'
    sys.exit(1)
host = sys.argv[1]
conn = pika.BlockingConnection(pika.ConnectionParameters(host))
atexit.register(conn.close)
channel = conn.channel()
channel.queue_purge(queue=sys.argv[2])
