import sys
import amqplib.client_0_8 as amqp
connection = amqp.Connection(host='localhost')
channel = connection.channel()
channel.exchange_declare('stats', 'fanout')
msg = amqp.Message(' '.join(sys.argv[1:]))
channel.basic_publish(msg, exchange='stats', routing_key='')
connection.close()

