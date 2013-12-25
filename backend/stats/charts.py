#!/usr/bin/python

import rabbit
import pycassa
import json
import datetime

pool = pycassa.ConnectionPool('crashdb', ['localhost'])
daybucketscount_cf = pycassa.ColumnFamily(pool, 'DayBucketsCount')

def jsonify (items):
    ret = [{'Signature' : key, 'Instances' : value } for key, value in items]
    return json.dumps(ret)

def get_total_buckets_by_day ():
    """All of the buckets added to for the past seven days."""
    # TODO accept a date range. Client asks for the number of days it can
    # reasonably fit on the screen.
    # Use DAY=60*60*24; time.strftime('%Y%m%d', time.gmtime(time.time() - DAY))
    date = datetime.date.today()
    delta = datetime.timedelta(days=1)
    for i in range(7):
        date = (date - delta)
        # TODO this should be replaced with pre-calculated values for
        # everything except for today.
        count = daybucketscount_cf.get_count(date.strftime('%Y%m%d'))
        yield date.strftime('%Y%m%d'), count
    
def get_bucket_counts_for_today (batch_size=100, filter_less_than=5):
    """The number of times each bucket has been added to today."""
    today = datetime.date.today().strftime('%Y%m%d')
    start = ''
    while True:
        result = daybucketscount_cf.get(today, column_start=start, column_count=100)
        for column, count in result.items():
            # These are uninteresting and ubiquitous.
            if count >= filter_less_than:
                yield (column, count)
        # We do not want to include the end of the previous batch.
        start = column + '0'
        if len(result) < 100:
            raise StopIteration

def application(environ, start_response):
    messaging = rabbit.RabbitMessaging('stats')
    queue_name = str(messaging.getQueue().name)
    if environ['PATH_INFO'].endswith('today'):
        title = 'Problems seen today'
        data = jsonify(get_bucket_counts_for_today())
    else:
        title = 'Problems seen in the past week'
        data = jsonify(get_total_buckets_by_day())
    start_response('200 OK', [('Content-type', 'text/html')])
    # TODO only include longpoll if the page needs it.
    return ["""
<html><head>
<script src="/static/js/yui/3.4.1/build/yui-base/yui-base-min.js"></script>
<script src="/static/js/longpoll.js"></script>
<link rel="stylesheet" type="text/css" href="http://fonts.googleapis.com/css?family=Ubuntu:regular,bold&subset=Latin">
<script type="text/javascript">
<!--
var dataProvider = %(data)s;

YUI().use('longpoll', 'charts', function(Y) {
    Y.on('load', function() {
        Y.later(0, Y.longpoll, function() {
            var longpollmanager = Y.longpoll.setupLongPollManager(
                '%(queue)s', '/longpoll/');
        });
        var mychart = new Y.Chart({
            dataProvider: dataProvider,
            type: "column",
            render: "#chart",
            categoryKey: "Signature",
        });
        Y.on('day_buckets_count:changed', function(x) {
            mychart.set('dataProvider', x.data);
            mychart.render("#chart");
        });
    });
});
//-->
</script>
<style media="screen" type="text/css">
<!--
body { font-family: Ubuntu, sans-serif; }
#chart {
    width: 800px;
    height: 600px;
}
-->
</style>
</head>
<body>
<h1>%(title)s</h1><div id="chart"></div>
</body>
</html>
""" % {'data': data, 'queue' :queue_name, 'title': title},]

if __name__ == '__main__':
    from wsgiref.simple_server import make_server
    server = make_server('localhost', 8080, application)
    server.serve_forever()
