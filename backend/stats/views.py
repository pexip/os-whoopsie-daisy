from django.shortcuts import render
from django.http import HttpResponse
from stats import cassandra
import json

def jsonify (items):
    ret = [{'signature' : key, 'value' : value } for key, value in items]
    return json.dumps(ret)

def today(request):
    failed = request.GET.get('failed', '').lower() == 'true'
    initial_data = cassandra.get_bucket_counts_for_today(show_failed=failed)
    c = {'title' : 'Problems seen today',
         'graphjs' : 'today',
         'data' : jsonify(initial_data)}
    return render(request, 'index.html', c)

def bucket(request):
    bucketid = request.GET.get('id', '')
    if not bucketid:
        return HttpResponse('')

    oopses = cassandra.get_crashes_for_bucket(bucketid)
    stacktrace = cassandra.get_stacktrace_for_bucket(bucketid)
    c = {'bucket' :  '<br>'.join(bucketid.split(':')),
         'oopses' : oopses, 
         'stacktrace' : stacktrace}
    return render(request, 'bucket.html', c)

def oops(request, oopsid):
    c = {'title' : oopsid,
         'oops' : cassandra.get_crash(oopsid)}
    return render(request, 'oops.html', c)

# TODO maybe replace this with byday, where it puts the number of days it can
# possibly fit on the screen, with arrows to move further back.
def week(request):
    data = cassandra.get_total_buckets_by_day()
    c = {'title' : 'Problems seen in the past week',
         'graphjs' : 'week',
         'data' : jsonify(data)}
    return render(request, 'index.html', c)

def most_common_problems_in_the_past_interval (request):
    buckets = cassandra.get_bucket_counts_for_today(filter_less_than=20)
    rows = []
    for bucket, count in buckets:
        package, version = cassandra.get_package_for_bucket(bucket)
        rows.append({'count': count,
                     'package': package,
                     'seen': version,
                     'function': bucket,
                     'examples': cassandra.get_crashes_for_bucket(bucket, 5),
                     'report':''})
    c = { 'rows': rows }
    return render(request, 'most_common.html', c)

def mean(request):
    return render(request, 'mean.html')
